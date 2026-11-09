#pragma once

#include <stdint.h>
#include "esp_err.h"

#define DISPLAY_W 160
#define DISPLAY_H 80

esp_err_t display_init(void);

// Framebuffer RGB565 160x80. I pixel sono memorizzati byte-swapped
// (big-endian), cioè nell'ordine in cui il controller li vuole sul bus:
// usare sempre gfx_color() o DISPLAY_SWAP per scriverci.
uint16_t *display_fb(void);

// Invia l'intero framebuffer al pannello. Bloccante (~8 ms a 27 MHz).
void display_flush(void);

// 0..100. 0 spegne la retroilluminazione.
void display_set_brightness(uint8_t percent);

#define DISPLAY_SWAP(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
