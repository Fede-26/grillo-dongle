#pragma once

#include <stdint.h>

// LED RGB condiviso a livelli: si vede il livello attivo più alto.
// AMBIENT = colore di sfondo (accento dell'app), APP = controllato dall'app
// o dallo script, SYSTEM = notifiche del sistema (errori, flash dei gesti).
typedef enum {
    LED_LAYER_AMBIENT,
    LED_LAYER_APP,
    LED_LAYER_SYSTEM,
    LED_LAYER_COUNT,
} led_layer_t;

typedef enum {
    LED_FX_OFF,     // livello trasparente
    LED_FX_SOLID,
    LED_FX_PULSE,   // respiro sinusoidale, periodo = period_ms
    LED_FX_BLINK,   // acceso/spento al 50%
    LED_FX_RAINBOW, // ciclo di tinta, il colore viene ignorato
    LED_FX_BLACK,   // LED spento ma livello opaco (copre quelli sotto)
} led_fx_t;

void led_init(void);
void led_set(led_layer_t layer, led_fx_t fx, uint32_t rgb, uint16_t period_ms);
void led_clear(led_layer_t layer);
// Lampo breve sopra tutti i livelli, poi torna a ciò che c'era sotto.
void led_flash(uint32_t rgb, uint16_t ms);
// Luminosità globale 0..100
void led_set_brightness(uint8_t percent);
