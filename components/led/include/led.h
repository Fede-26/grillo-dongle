#pragma once

#include <stdint.h>

// Shared RGB LED with layers: the highest active layer is shown.
// AMBIENT = background color (the app's accent), APP = controlled by the app
// or the script, SYSTEM = system notifications (errors, gesture flashes).
typedef enum {
    LED_LAYER_AMBIENT,
    LED_LAYER_APP,
    LED_LAYER_SYSTEM,
    LED_LAYER_COUNT,
} led_layer_t;

typedef enum {
    LED_FX_OFF,     // transparent layer
    LED_FX_SOLID,
    LED_FX_PULSE,   // sinusoidal breathing, period = period_ms
    LED_FX_BLINK,   // on/off at 50%
    LED_FX_RAINBOW, // hue cycle, the color is ignored
    LED_FX_BLACK,   // LED off but opaque layer (covers the ones below)
} led_fx_t;

void led_init(void);
void led_set(led_layer_t layer, led_fx_t fx, uint32_t rgb, uint16_t period_ms);
void led_clear(led_layer_t layer);
// Short flash above all layers, then back to whatever was below.
void led_flash(uint32_t rgb, uint16_t ms);
// Global brightness 0..100
void led_set_brightness(uint8_t percent);
