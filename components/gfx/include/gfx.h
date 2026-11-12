#pragma once

#include <stdbool.h>
#include <stdint.h>

// Graphics primitives on the display framebuffer. No internal lock:
// drawing is serialized by the ui layer (only one task draws at a time).

typedef struct {
    uint8_t height;
    uint8_t count;
    const uint8_t *bits;        // count * height rows, MSB = left column
    const uint8_t *metrics;     // count * {left, width}
    const uint16_t *codepoints; // Unicode codepoint of each glyph
} gfx_font_t;

extern const gfx_font_t gfx_font_small; // 8 px
extern const gfx_font_t gfx_font_large; // 16 px

// Monochrome "pixel art" icon: rows of characters, '#' = pixel on.
// Drawn with an integer scale factor.
typedef struct {
    uint8_t w, h;
    const char *const *rows;
} gfx_icon_t;

typedef enum { GFX_ALIGN_LEFT, GFX_ALIGN_CENTER, GFX_ALIGN_RIGHT } gfx_align_t;

// Converts a 0xRRGGBB color to RGB565, already byte-swapped for the framebuffer.
uint16_t gfx_rgb(uint32_t rgb);
// Blends two 0xRRGGBB colors; t=0 -> a, t=255 -> b.
uint32_t gfx_mix(uint32_t a, uint32_t b, uint8_t t);

void gfx_clear(uint32_t rgb);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb, bool filled);
void gfx_hline(int x, int y, int w, uint32_t rgb);
void gfx_vline(int x, int y, int h, uint32_t rgb);
void gfx_circle(int cx, int cy, int r, uint32_t rgb, bool filled);
// Circular arc `thick` px thick, from 0 to `deg` degrees clockwise starting
// from 12 o'clock. Used for the "hold down" ring.
void gfx_arc(int cx, int cy, int r, int thick, int deg, uint32_t rgb);

// Clipping: anything that falls outside the rectangle is not drawn.
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);

// Proportional UTF-8 text. Returns the drawn width.
int gfx_text(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb);
int gfx_text_width(const gfx_font_t *f, const char *s);
// Like gfx_text, with each glyph pixel enlarged `scale` times (integer). Returns the drawn width.
int gfx_text_scaled(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb, int scale);
int gfx_text_scaled_width(const gfx_font_t *f, const char *s, int scale);
void gfx_text_aligned(const gfx_font_t *f, int x, int y, int w, gfx_align_t a, const char *s, uint32_t rgb);
// Draws the text, truncating it with "…" if it exceeds max_w.
void gfx_text_fit(const gfx_font_t *f, int x, int y, int max_w, const char *s, uint32_t rgb);

void gfx_icon(const gfx_icon_t *ic, int x, int y, int scale, uint32_t rgb);
