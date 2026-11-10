// Sequenza di boot di Grillo: hardware, SD, config, splash, poi app manager.

#include "app_list.h"
#include "appmgr.h"
#include "clock.h"
#include "config.h"
#include "console_cmds.h"
#include "devcon.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "icons.h"
#include "launcher.h"
#include "led.h"
#include "storage.h"
#include "syslog.h"
#include "ui.h"

static const char *TAG = "main";

static void splash(void)
{
    ui_bar_visible(false);
    ui_begin_full();
    gfx_clear(UI_BG);
    gfx_icon(&icon_grillo, 14, 22, 3, UI_GRILLO);
    gfx_text(&gfx_font_large, 70, 24, "Grillo", UI_TEXT);
    gfx_text(&gfx_font_small, 70, 44, esp_app_get_description()->version, UI_DIM);
    ui_end_full();
    ui_bar_visible(true);
}

static void show_config_error(const char *err)
{
    ui_bar_visible(false);
    ui_begin_full();
    gfx_clear(UI_BG);
    gfx_text(&gfx_font_small, 4, 3, "config.lua error", UI_ERR);
    ui_end_full();
    led_set(LED_LAYER_SYSTEM, LED_FX_BLINK, UI_ERR, 400);
    // Il pulsante non è ancora attivo: l'errore resta almeno 4 s e, se è
    // lungo, finché non è scorso tutto una volta (al massimo 30 s).
    ui_scroll_t sc;
    ui_scroll_init(&sc, &gfx_font_small, 4, 15, UI_W - 10, 6, err);
    int64_t start = esp_timer_get_time();
    for (;;) {
        ui_begin_full();
        gfx_fill_rect(0, 14, UI_W, UI_H - 14, UI_BG);
        ui_scroll_draw(&sc, UI_TEXT);
        ui_end_full();
        int64_t elapsed = esp_timer_get_time() - start;
        if (elapsed >= 30000000LL || (elapsed >= 4000000LL && (!ui_scroll_active(&sc) || sc.cycles > 0))) break;
        uint32_t wait = ui_scroll_wait_ms(&sc);
        vTaskDelay(pdMS_TO_TICKS(wait > 250 ? 250 : wait));
        ui_scroll_tick(&sc);
    }
    ui_bar_visible(true);
    led_clear(LED_LAYER_SYSTEM);
}

static void apply_settings(void)
{
    display_set_rotation(config_int("ui.rotation", 0));
    display_set_brightness(config_int("ui.brightness", 80));
    led_set_brightness(config_int("led.brightness", 30));
    clock_set_timezone(config_str("timezone", CLOCK_DEFAULT_TZ));
}

void main_request_config_reload(void)
{
    app_config_changed();
}

// Chiamata a ogni uscita da un'app, quando nessun'altra app gira: è l'unico
// momento in cui ricaricare è sicuro, perché config_load libera le stringhe
// restituite da config_str che un'app potrebbe ancora usare. Si ricarica se
// la SD è stata prestata al PC (USB Disk: config.lua può essere cambiato) o
// su richiesta della console o di un'app (app_config_changed).
static void reload_config(void)
{
    static uint32_t last_lend;
    bool changed = appmgr_take_config_changed();
    if (storage_lend_count() == last_lend && !changed) return;
    last_lend = storage_lend_count();
    if (storage_state() != STORAGE_MOUNTED) return;
    if (config_load(CONFIG_PATH) == ESP_FAIL) ui_toast("config.lua error", UI_ERR, 3000);
    apply_settings();
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());
    ui_init();
    led_init();
    led_set(LED_LAYER_SYSTEM, LED_FX_PULSE, UI_GRILLO, 1200);
    splash();

    // La console va inizializzata prima di syslog: syslog si aggancia ai log
    // a valle e inoltra a devcon, che li scrive sul trasporto USB attivo.
    devcon_init();
    console_cmds_register();
    storage_init();
    syslog_init();
    if (storage_state() == STORAGE_MOUNTED && config_load(CONFIG_PATH) == ESP_FAIL) {
        show_config_error(config_error());
    }
    apply_settings();

    vTaskDelay(pdMS_TO_TICKS(600));
    led_clear(LED_LAYER_SYSTEM);
    appmgr_add_cleanup(reload_config);
    ESP_LOGI(TAG, "%u app registrate", (unsigned)g_app_count);
    appmgr_run(&launcher_app, g_apps, g_app_count);
}
