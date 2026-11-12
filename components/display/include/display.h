#pragma once

#include <stdint.h>
#include "esp_err.h"

#define DISPLAY_W 160
#define DISPLAY_H 80

esp_err_t display_init(void);

// RGB565 160x80 framebuffer. Pixels are stored byte-swapped
// (big-endian), i.e. in the order the controller wants them on the bus:
// always use gfx_color() or DISPLAY_SWAP to write into it.
uint16_t *display_fb(void);

// Sends the whole framebuffer to the panel. Blocking (~8 ms at 27 MHz).
void display_flush(void);

// Screen rotation: 0 or 180 degrees. 90 and 270 are not supported (the UI is
// designed for 160x80 landscape); any other value means 0.
void display_set_rotation(int degrees);

// 0..100. 0 turns the backlight off.
void display_set_brightness(uint8_t percent);

#define DISPLAY_SWAP(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
