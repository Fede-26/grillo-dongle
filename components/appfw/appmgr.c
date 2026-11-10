#include "appmgr.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/task.h"
#include "input.h"
#include "led.h"
#include "net.h"
#include "storage.h"
#include "syslog.h"
#include "ui.h"

static const char *TAG = "appmgr";

#define MAX_CLEANUPS 8

static const app_desc_t *s_launcher;
static const app_desc_t *const *s_apps;
static size_t s_count;

static const app_desc_t *s_next;
static char s_next_arg[128];
static bool s_has_arg;

// Contesto dell'app in primo piano. Letto dal task del pulsante e dalle
// callback USB: lo proteggiamo con una sezione critica.
static app_ctx_t *volatile s_current;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_mgr_task;

static void (*s_cleanups[MAX_CLEANUPS])(void);
static int s_ncleanups;

size_t appmgr_count(void)
{
    return s_count;
}

const app_desc_t *appmgr_get(size_t i)
{
    return i < s_count ? s_apps[i] : NULL;
}

const app_desc_t *appmgr_find(const char *id)
{
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_apps[i]->id, id) == 0) return s_apps[i];
    }
    return NULL;
}

void appmgr_launch(const app_desc_t *app, const char *arg)
{
    s_next = app;
    s_has_arg = arg != NULL;
    if (arg) strlcpy(s_next_arg, arg, sizeof(s_next_arg));
}

void appmgr_add_cleanup(void (*fn)(void))
{
    if (s_ncleanups < MAX_CLEANUPS) s_cleanups[s_ncleanups++] = fn;
}

const char *appmgr_unavailable(const app_desc_t *app)
{
    storage_state_t st = storage_state();
    if ((app->requires & APP_REQ_SD) && st != STORAGE_MOUNTED) {
        return st == STORAGE_UNSUPPORTED ? "SD not FAT32" : "no SD card";
    }
    if ((app->requires & APP_REQ_CARD) && st == STORAGE_ABSENT) return "no SD card";
    if (app->requires & APP_REQ_BLE) return "BLE not available";
    return NULL;
}

static void refresh_indicators(void)
{
    static const uint8_t sd_map[] = {[STORAGE_ABSENT] = 0, [STORAGE_MOUNTED] = 1, [STORAGE_UNSUPPORTED] = 2,
                                     [STORAGE_LENT] = 3};
    ui_set_indicator(UI_IND_SD, sd_map[storage_state()]);
    // Dalle interfacce, non dal nome del profilo: un profilo nuovo ha
    // l'indicatore giusto senza toccare questa funzione.
    usb_profile_t p = usbdev_profile();
    bool hid = p & (USB_F_KBD | USB_F_MOUSE);
    uint8_t usb = 0;
    if (hid && (p & USB_F_DISK)) usb = 3;
    else if (hid) usb = 1;
    else if (p & USB_F_DISK) usb = 2;
    ui_set_indicator(UI_IND_USB, usb);
}

esp_err_t app_usb_apply(usb_profile_t p)
{
    if (p == usbdev_profile()) return ESP_OK;
    // Prima l'host perde il disco, poi la SD torna al firmware, solo dopo la
    // si presta di nuovo: passare da un modo all'altro senza staccare l'host
    // smonterebbe la FAT mentre la sta leggendo.
    usbdev_apply(USB_PROFILE_NONE, NULL);
    storage_unshare();
    if (storage_state() == STORAGE_LENT) storage_reclaim();
    sdmmc_card_t *card = NULL;
    if (p & USB_F_DISK) syslog_flush(); // da qui il firmware non scrive più sulla SD
    if (p & USB_F_DISK_RW) card = storage_lend();
    else if (p & USB_F_DISK_RO) card = storage_share_ro();
    esp_err_t err = usbdev_apply(p, card);
    if (err != ESP_OK) {
        storage_unshare();
        if (storage_state() == STORAGE_LENT) storage_reclaim();
    }
    refresh_indicators();
    return err;
}

static void post_current(app_ev_type_t type, int32_t arg)
{
    portENTER_CRITICAL(&s_mux);
    app_ctx_t *c = s_current;
    portEXIT_CRITICAL(&s_mux);
    if (c) app_post(c, type, arg);
}

static void on_gesture(gesture_t g, uint8_t progress)
{
    portENTER_CRITICAL(&s_mux);
    app_ctx_t *c = s_current;
    portEXIT_CRITICAL(&s_mux);
    if (!c) return;
    bool in_launcher = c->app == s_launcher;
    uint32_t accent = c->app->accent;

    switch (g) {
    case GESTURE_CLICK:
        led_flash(gfx_mix(0, accent, 140), 40);
        app_post(c, APP_EV_CLICK, 0);
        break;
    case GESTURE_DOUBLE:
        led_flash(gfx_mix(0, accent, 140), 40);
        app_post(c, APP_EV_DOUBLE, 0);
        break;
    case GESTURE_LONG_ARMED:
        // Segnala che il rilascio adesso vale come "conferma"
        led_flash(0xFFFFFF, 50);
        ui_set_armed(true);
        break;
    case GESTURE_LONG:
        ui_set_armed(false);
        ui_flash_bar(150);
        app_post(c, APP_EV_LONG, 0);
        break;
    case GESTURE_HOLD_PROGRESS:
        // Da qui il rilascio annulla: la linea bianca non vale più
        ui_set_armed(false);
        // Nel launcher non c'è niente da cui uscire: niente barra
        if (!in_launcher) ui_set_hold(progress + 1);
        break;
    case GESTURE_HOLD_CANCEL:
        ui_set_armed(false);
        ui_set_hold(0);
        break;
    case GESTURE_HOLD_EXIT:
        ui_set_armed(false); // già spenta dall'avanzamento, salvo `btn armed` + `btn hold`
        ui_set_hold(0);
        if (!in_launcher && c->exit_requested) {
            // L'uscita era già stata chiesta e l'app è ancora qui: un task
            // FreeRTOS non si può terminare dall'esterno in modo sicuro, quindi
            // l'unica uscita garantita è il riavvio.
            ESP_LOGW(TAG, "%s non esce: riavvio", c->app->id);
            ui_toast("Restarting…", UI_ERR, 2000);
            syslog_flush();
            esp_restart();
        }
        if (!in_launcher) {
            app_post(c, APP_EV_EXIT, 0);
            ui_toast("Exiting…", UI_WARN, 1500);
        }
        break;
    default: break;
    }
}

static void on_usb(usbdev_event_t ev)
{
    refresh_indicators();
    post_current(APP_EV_USB, ev);
}

// Aggiorna l'indicatore WiFi della status bar e avvisa l'app in primo piano.
static void on_net(net_event_t ev)
{
    net_info_t info;
    net_get_info(&info);
    uint8_t v = 0;
    if (ev == NET_EV_CONNECTING) v = 1;
    else if (info.up && info.mode == NET_AP) v = 6;
    else if (info.up) v = info.rssi > -55 ? 5 : info.rssi > -65 ? 4 : info.rssi > -75 ? 3 : 2;
    ui_set_indicator(UI_IND_WIFI, v);
    post_current(APP_EV_NET, ev);
}

static void app_task(void *arg)
{
    app_ctx_t *ctx = arg;
    ctx->app->run(ctx);
    xTaskNotifyGive(s_mgr_task);
    vTaskDelete(NULL);
}

static void run_app(const app_desc_t *app, const char *arg)
{
    const char *why = appmgr_unavailable(app);
    if (why) {
        ui_toast(why, UI_ERR, 2000);
        led_flash(UI_ERR, 300);
        return;
    }
    size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "avvio %s (heap libero %u)", app->id, (unsigned)heap_before);

    ui_set_title(app->name, app->accent);
    led_set(LED_LAYER_AMBIENT, LED_FX_PULSE, gfx_mix(0, app->accent, 90), 4000);

    if (app_usb_apply(app->usb) != ESP_OK) {
        ui_toast("USB init failed", UI_ERR, 2500);
        return;
    }

    app_ctx_t ctx = {
        .app = app,
        .arg = arg,
        .queue = xQueueCreate(16, sizeof(app_event_t)),
    };
    portENTER_CRITICAL(&s_mux);
    s_current = &ctx;
    portEXIT_CRITICAL(&s_mux);

    TaskHandle_t t = NULL;
    if (xTaskCreate(app_task, app->id, app->stack, &ctx, 4, &t) != pdPASS) {
        ESP_LOGE(TAG, "task %s: memoria insufficiente", app->id);
        ui_toast("Out of memory", UI_ERR, 2500);
    } else {
        // Se l'app non esce entro 3 s dalla richiesta, si dice all'utente
        // che un secondo hold riavvia il dongle (vedi on_gesture).
        int waited = 0;
        while (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500))) {
            waited = ctx.exit_requested ? waited + 1 : 0;
            if (waited == 6) ui_toast("Hold again: reboot", UI_ERR, 4000);
        }
    }

    portENTER_CRITICAL(&s_mux);
    s_current = NULL;
    portEXIT_CRITICAL(&s_mux);

    // Pulizia d'ufficio delle risorse condivise. Torna al profilo NONE anche
    // se l'app l'ha cambiato con app_usb_apply, e riprende la SD prestata o
    // condivisa (senza, syslog non scriveva più sulla SD fino al riavvio).
    usbdev_apply(USB_PROFILE_NONE, NULL);
    if (storage_state() == STORAGE_LENT) storage_reclaim();
    storage_unshare();
    // Solo i lease del task dell'app, che a questo punto è terminato
    if (t) net_release_task(t);
    for (int i = 0; i < s_ncleanups; i++) s_cleanups[i]();
    led_clear(LED_LAYER_APP);
    ui_set_hold(0);
    vQueueDelete(ctx.queue);
    refresh_indicators();

    size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "fine %s (heap libero %u, delta %d)", app->id, (unsigned)heap_after,
             (int)heap_after - (int)heap_before);
}

void appmgr_run(const app_desc_t *launcher, const app_desc_t *const *apps, size_t count)
{
    s_launcher = launcher;
    s_apps = apps;
    s_count = count;
    s_mgr_task = xTaskGetCurrentTaskHandle();
    usbdev_set_event_cb(on_usb);
    net_set_event_cb(on_net);
    input_init(on_gesture);
    refresh_indicators();

    for (;;) {
        const app_desc_t *app = s_next ? s_next : s_launcher;
        char arg[sizeof(s_next_arg)];
        bool has_arg = s_has_arg;
        if (has_arg) strlcpy(arg, s_next_arg, sizeof(arg));
        s_next = NULL;
        s_has_arg = false;
        run_app(app, has_arg ? arg : NULL);
    }
}

// ---------------------------------------------------------------------------
// API per le app

bool app_next_event(app_ctx_t *ctx, app_event_t *ev, uint32_t timeout_ms)
{
    if (ctx->exit_lost) {
        ctx->exit_lost = false;
        *ev = (app_event_t){APP_EV_EXIT, 0};
        return true;
    }
    TickType_t t = timeout_ms == portMAX_DELAY ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xQueueReceive(ctx->queue, ev, t) == pdTRUE;
}

bool app_should_exit(app_ctx_t *ctx)
{
    return ctx->exit_requested;
}

void app_post(app_ctx_t *ctx, app_ev_type_t type, int32_t arg)
{
    app_event_t ev = {type, arg};
    if (type != APP_EV_EXIT) {
        xQueueSend(ctx->queue, &ev, 0);
        return;
    }
    ctx->exit_requested = true;
    // EXIT non deve mai andare perso: con la coda piena (es. click accumulati
    // durante uno sweep) lo consegna app_next_event alla prima chiamata.
    if (xQueueSend(ctx->queue, &ev, pdMS_TO_TICKS(100)) != pdTRUE) ctx->exit_lost = true;
}

static volatile bool s_config_changed;

void app_config_changed(void)
{
    s_config_changed = true;
}

bool appmgr_take_config_changed(void)
{
    bool c = s_config_changed;
    s_config_changed = false;
    return c;
}

void app_cancel_exit(app_ctx_t *ctx)
{
    ctx->exit_requested = false;
    ctx->exit_lost = false;
    // Una coda FreeRTOS non permette di togliere un solo elemento: la si
    // svuota e si rimette ciò che non è un EXIT, nello stesso ordine.
    app_event_t keep[16], ev;
    int n = 0;
    while (n < 16 && xQueueReceive(ctx->queue, &ev, 0) == pdTRUE) {
        if (ev.type != APP_EV_EXIT) keep[n++] = ev;
    }
    for (int i = 0; i < n; i++) xQueueSend(ctx->queue, &keep[i], 0);
}

// ---------------------------------------------------------------------------
// Console di debug

const char *appmgr_current_id(void)
{
    portENTER_CRITICAL(&s_mux);
    app_ctx_t *c = s_current;
    portEXIT_CRITICAL(&s_mux);
    return c ? c->app->id : NULL;
}

void appmgr_request_exit(void)
{
    portENTER_CRITICAL(&s_mux);
    app_ctx_t *c = s_current;
    portEXIT_CRITICAL(&s_mux);
    if (!c) return;
    app_post(c, APP_EV_EXIT, 0);
}

void appmgr_inject(gesture_t g, uint8_t progress)
{
    on_gesture(g, progress);
}
