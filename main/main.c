// Firmware di bring-up hardware (fase 2): verifica display, pulsante, LED e SD.
// Verrà sostituito dalla sequenza di boot del framework ad app.

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gfx.h"
#include "input.h"
#include "led.h"
#include "storage.h"

static const char *TAG = "bringup";
static QueueHandle_t s_q;

typedef struct {
    gesture_t g;
    uint8_t p;
} ev_t;

static void on_gesture(gesture_t g, uint8_t p)
{
    ev_t e = {g, p};
    xQueueSend(s_q, &e, 0);
}

static const char *gname(gesture_t g)
{
    switch (g) {
    case GESTURE_CLICK: return "CLICK";
    case GESTURE_DOUBLE: return "DOUBLE";
    case GESTURE_LONG: return "LONG";
    case GESTURE_LONG_ARMED: return "LONG armed";
    case GESTURE_HOLD_PROGRESS: return "HOLD";
    case GESTURE_HOLD_CANCEL: return "HOLD cancel";
    case GESTURE_HOLD_EXIT: return "HOLD EXIT";
    default: return "-";
    }
}

static const uint32_t s_colors[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
static const char *s_color_names[] = {"red", "green", "blue", "white"};

static void draw(const char *last, int color_idx, uint8_t hold, const char *sd_line)
{
    gfx_clear(0x000000);
    // Barre colore: verificano ordine RGB/BGR e inversione del pannello
    gfx_fill_rect(0, 0, 40, 10, 0xFF0000);
    gfx_fill_rect(40, 0, 40, 10, 0x00FF00);
    gfx_fill_rect(80, 0, 40, 10, 0x0000FF);
    gfx_fill_rect(120, 0, 40, 10, 0xFFFFFF);
    // Cornice ai bordi: verifica gli offset (deve vedersi tutta, 1 px)
    gfx_rect(0, 0, DISPLAY_W, DISPLAY_H, 0xFFFF00);

    gfx_text(&gfx_font_large, 4, 13, "Grillo", 0x7CFC00);
    gfx_text(&gfx_font_small, 70, 17, "bring-up", 0x808080);
    char buf[48];
    snprintf(buf, sizeof(buf), "btn: %s", last);
    gfx_text(&gfx_font_small, 4, 32, buf, 0xFFFFFF);
    snprintf(buf, sizeof(buf), "led: %s", s_color_names[color_idx]);
    gfx_text(&gfx_font_small, 4, 42, buf, 0xFFFFFF);
    gfx_text_fit(&gfx_font_small, 4, 52, 150, sd_line, 0x00FFCC);
    gfx_round_rect(4, 63, 60, 13, 4, 0x00FFCC, false);
    gfx_text(&gfx_font_small, 10, 66, "àèé ✓✗•", 0xFFFFFF);
    if (hold) gfx_arc(140, 60, 12, 3, hold * 360 / 100, 0xFF4040);
    display_flush();
}

void app_main(void)
{
    s_q = xQueueCreate(16, sizeof(ev_t));
    ESP_ERROR_CHECK(display_init());
    led_init();
    input_init(on_gesture);

    char sd_line[64];
    storage_init();
    if (storage_state() == STORAGE_MOUNTED) {
        uint64_t tot = 0, fr = 0;
        storage_info(&tot, &fr);
        snprintf(sd_line, sizeof(sd_line), "SD %llu/%llu MB", fr >> 20, tot >> 20);
        DIR *d = opendir(STORAGE_ROOT);
        struct dirent *e;
        while (d && (e = readdir(d))) ESP_LOGI(TAG, "SD: %s", e->d_name);
        if (d) closedir(d);
    } else {
        snprintf(sd_line, sizeof(sd_line), "SD: %s", storage_state_name(storage_state()));
    }

    int color = 0;
    led_set(LED_LAYER_APP, LED_FX_SOLID, s_colors[color], 0);
    const char *last = "-";
    uint8_t hold = 0;
    draw(last, color, hold, sd_line);

    ev_t e;
    for (;;) {
        if (xQueueReceive(s_q, &e, portMAX_DELAY) != pdTRUE) continue;
        ESP_LOGI(TAG, "gesture %s %u", gname(e.g), e.p);
        last = gname(e.g);
        hold = e.g == GESTURE_HOLD_PROGRESS ? e.p : 0;
        if (e.g == GESTURE_CLICK) color = (color + 1) % 4;
        if (e.g == GESTURE_DOUBLE) color = (color + 3) % 4;
        if (e.g == GESTURE_LONG) led_set(LED_LAYER_APP, LED_FX_PULSE, s_colors[color], 1200);
        else if (e.g == GESTURE_LONG_ARMED) led_flash(0xFFFFFF, 60);
        else if (e.g == GESTURE_HOLD_EXIT) led_set(LED_LAYER_APP, LED_FX_RAINBOW, 0, 3000);
        else if (e.g == GESTURE_CLICK || e.g == GESTURE_DOUBLE) led_set(LED_LAYER_APP, LED_FX_SOLID, s_colors[color], 0);
        draw(last, color, hold, sd_line);
    }
}
