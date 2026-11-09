// Sequenza di boot di Grillo: hardware, SD, config, splash, poi app manager.

#include "app_list.h"
#include "appmgr.h"
#include "config.h"
#include "console_cmds.h"
#include "devcon.h"
#include "display.h"
#include "esp_app_desc.h"
#include "esp_log.h"
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
    ui_draw_wrapped(&gfx_font_small, 4, 15, UI_W - 8, 6, err, UI_TEXT);
    ui_end_full();
    ui_bar_visible(true);
    led_set(LED_LAYER_SYSTEM, LED_FX_BLINK, UI_ERR, 400);
    vTaskDelay(pdMS_TO_TICKS(4000));
    led_clear(LED_LAYER_SYSTEM);
}

// Chiamata a ogni uscita da un'app: se la SD è stata prestata al PC
// (USB Disk) config.lua potrebbe essere cambiato, quindi lo rileggiamo.
static void reload_config(void)
{
    static uint32_t last_lend;
    if (storage_lend_count() == last_lend) return;
    last_lend = storage_lend_count();
    if (storage_state() != STORAGE_MOUNTED) return;
    if (config_load(CONFIG_PATH) == ESP_FAIL) ui_toast("config.lua error", UI_ERR, 3000);
    display_set_brightness(config_int("ui.brightness", 80));
    led_set_brightness(config_int("led.brightness", 30));
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
    display_set_brightness(config_int("ui.brightness", 80));
    led_set_brightness(config_int("led.brightness", 30));

    vTaskDelay(pdMS_TO_TICKS(600));
    led_clear(LED_LAYER_SYSTEM);
    appmgr_add_cleanup(reload_config);
    ESP_LOGI(TAG, "%u app registrate", (unsigned)g_app_count);
    appmgr_run(&launcher_app, g_apps, g_app_count);
}
