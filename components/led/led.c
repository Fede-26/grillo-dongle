#include "led.h"

#include <math.h>
#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    led_fx_t fx;
    uint32_t rgb;
    uint16_t period;
    int64_t until_us; // scadenza, usata solo dal lampo
} layer_t;

static layer_t s_layers[LED_LAYER_COUNT];
// Il lampo sta sopra a tutti i livelli in uno slot separato, così quando
// scade riappare lo stato SYSTEM precedente invece di essere cancellato.
static layer_t s_flash;
static uint8_t s_brightness = 30;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// APA102: un solo LED, quindi bit-bang su GPIO (128 bit per frame) è più
// semplice che occupare un bus SPI.
static void apa_byte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(BOARD_LED_DI, (b >> i) & 1);
        gpio_set_level(BOARD_LED_CI, 1);
        gpio_set_level(BOARD_LED_CI, 0);
    }
}

static void apa_write(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 4; i++) apa_byte(0x00); // start frame
    apa_byte(0xE0 | 31);                         // luminosità globale al massimo: scaliamo noi i canali
    apa_byte(b);                                 // ordine nativo BGR
    apa_byte(g);
    apa_byte(r);
    for (int i = 0; i < 4; i++) apa_byte(0xFF); // end frame
}

static uint32_t hue(uint32_t t)
{
    uint8_t h = t & 0xFF, x = (h % 85) * 3;
    if (h < 85) return ((255 - x) << 16) | (x << 8);
    if (h < 170) return ((255 - x) << 8) | x;
    return (x << 16) | (255 - x);
}

static void led_task(void *arg)
{
    uint32_t last = 0xFFFFFFFF;
    for (;;) {
        int64_t now = esp_timer_get_time();
        uint32_t ms = now / 1000;
        layer_t top = {.fx = LED_FX_BLACK};
        portENTER_CRITICAL(&s_lock);
        if (s_flash.fx != LED_FX_OFF && now >= s_flash.until_us) s_flash.fx = LED_FX_OFF;
        if (s_flash.fx != LED_FX_OFF) top = s_flash;
        for (int i = LED_LAYER_COUNT - 1; i >= 0; i--) {
            layer_t *l = &s_layers[i];
            if (top.fx != LED_FX_BLACK) break; // il lampo ha già vinto
            if (l->fx != LED_FX_OFF) {
                top = *l;
                break;
            }
        }
        portEXIT_CRITICAL(&s_lock);

        uint32_t c = 0;
        uint32_t period = top.period ? top.period : 1000;
        switch (top.fx) {
        case LED_FX_SOLID: c = top.rgb; break;
        case LED_FX_BLINK: c = (ms % period) < period / 2 ? top.rgb : 0; break;
        case LED_FX_RAINBOW: c = hue(ms * 256 / period); break;
        case LED_FX_PULSE: {
            float k = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (ms % period) / period);
            uint8_t t = (uint8_t)(k * 255);
            c = (((top.rgb >> 16 & 0xFF) * t / 255) << 16) | (((top.rgb >> 8 & 0xFF) * t / 255) << 8) |
                ((top.rgb & 0xFF) * t / 255);
            break;
        }
        default: c = 0; break;
        }

        // Correzione gamma approssimata (quadratica) e luminosità globale:
        // senza, le dissolvenze sembrano "saltare" vicino al nero.
        uint8_t ch[3];
        for (int i = 0; i < 3; i++) {
            uint32_t v = (c >> (16 - 8 * i)) & 0xFF;
            ch[i] = (uint8_t)(v * v / 255 * s_brightness / 100);
        }
        uint32_t packed = (ch[0] << 16) | (ch[1] << 8) | ch[2];
        if (packed != last) {
            apa_write(ch[0], ch[1], ch[2]);
            last = packed;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void led_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_LED_DI) | (1ULL << BOARD_LED_CI),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    gpio_set_level(BOARD_LED_CI, 0);
    apa_write(0, 0, 0);
    xTaskCreate(led_task, "led", 2560, NULL, 3, NULL);
}

void led_set(led_layer_t layer, led_fx_t fx, uint32_t rgb, uint16_t period_ms)
{
    portENTER_CRITICAL(&s_lock);
    s_layers[layer] = (layer_t){.fx = fx, .rgb = rgb, .period = period_ms};
    portEXIT_CRITICAL(&s_lock);
}

void led_clear(led_layer_t layer)
{
    led_set(layer, LED_FX_OFF, 0, 0);
}

void led_flash(uint32_t rgb, uint16_t ms)
{
    portENTER_CRITICAL(&s_lock);
    s_flash = (layer_t){
        .fx = LED_FX_SOLID, .rgb = rgb, .until_us = esp_timer_get_time() + ms * 1000LL};
    portEXIT_CRITICAL(&s_lock);
}

void led_set_brightness(uint8_t percent)
{
    s_brightness = percent > 100 ? 100 : percent;
}
