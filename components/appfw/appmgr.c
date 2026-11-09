#include "appmgr.h"

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "input.h"
#include "led.h"
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
    usb_profile_t p = usbdev_profile();
    ui_set_indicator(UI_IND_USB, p == USB_PROFILE_HID ? 1 : (p == USB_PROFILE_MSC ? 2 : 0));
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
        break;
    case GESTURE_LONG:
        app_post(c, APP_EV_LONG, 0);
        break;
    case GESTURE_HOLD_PROGRESS:
        // Nel launcher non c'è niente da cui uscire: niente barra
        if (!in_launcher) ui_set_hold(progress + 1);
        break;
    case GESTURE_HOLD_CANCEL:
        ui_set_hold(0);
        break;
    case GESTURE_HOLD_EXIT:
        ui_set_hold(0);
        if (!in_launcher) {
            c->exit_requested = true;
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

    // Profilo USB: per l'MSC la SD passa dal firmware all'host
    sdmmc_card_t *card = NULL;
    if (app->usb == USB_PROFILE_MSC) {
        syslog_flush();
        card = storage_lend();
    }
    if (usbdev_apply(app->usb, card) != ESP_OK) {
        ui_toast("USB init failed", UI_ERR, 2500);
        if (card) storage_reclaim();
        refresh_indicators();
        return;
    }
    refresh_indicators();

    app_ctx_t ctx = {
        .app = app,
        .arg = arg,
        .queue = xQueueCreate(16, sizeof(app_event_t)),
    };
    portENTER_CRITICAL(&s_mux);
    s_current = &ctx;
    portEXIT_CRITICAL(&s_mux);

    TaskHandle_t t;
    if (xTaskCreate(app_task, app->id, app->stack, &ctx, 4, &t) != pdPASS) {
        ESP_LOGE(TAG, "task %s: memoria insufficiente", app->id);
        ui_toast("Out of memory", UI_ERR, 2500);
    } else {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    portENTER_CRITICAL(&s_mux);
    s_current = NULL;
    portEXIT_CRITICAL(&s_mux);

    // Pulizia d'ufficio delle risorse condivise
    usbdev_apply(USB_PROFILE_NONE, NULL);
    if (storage_state() == STORAGE_LENT) storage_reclaim();
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
    // EXIT non deve mai andare perso: se la coda è piena aspettiamo un po'
    xQueueSend(ctx->queue, &ev, type == APP_EV_EXIT ? pdMS_TO_TICKS(100) : 0);
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
    c->exit_requested = true;
    app_post(c, APP_EV_EXIT, 0);
}

void appmgr_inject(gesture_t g, uint8_t progress)
{
    on_gesture(g, progress);
}
