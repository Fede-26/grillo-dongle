#pragma once

#include <stdbool.h>
#include <stdint.h>

// Primitive grafiche sul framebuffer del display. Nessun lock interno:
// il disegno è serializzato dal livello ui (un solo task disegna alla volta).

typedef struct {
    uint8_t height;
    uint8_t count;
    const uint8_t *bits;        // count * height righe, MSB = colonna sinistra
    const uint8_t *metrics;     // count * {left, width}
    const uint16_t *codepoints; // codepoint Unicode di ogni glifo
} gfx_font_t;

extern const gfx_font_t gfx_font_small; // 8 px
extern const gfx_font_t gfx_font_large; // 16 px

// Icona monocromatica in "pixel art": righe di caratteri, '#' = pixel acceso.
// Disegnata con un fattore di scala intero.
typedef struct {
    uint8_t w, h;
    const char *const *rows;
} gfx_icon_t;

typedef enum { GFX_ALIGN_LEFT, GFX_ALIGN_CENTER, GFX_ALIGN_RIGHT } gfx_align_t;

// Converte un colore 0xRRGGBB in RGB565 già byte-swapped per il framebuffer.
uint16_t gfx_rgb(uint32_t rgb);
// Mescola due colori 0xRRGGBB; t=0 -> a, t=255 -> b.
uint32_t gfx_mix(uint32_t a, uint32_t b, uint8_t t);

void gfx_clear(uint32_t rgb);
void gfx_pixel(int x, int y, uint16_t c);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_rect(int x, int y, int w, int h, uint32_t rgb);
void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb, bool filled);
void gfx_hline(int x, int y, int w, uint32_t rgb);
void gfx_vline(int x, int y, int h, uint32_t rgb);
void gfx_circle(int cx, int cy, int r, uint32_t rgb, bool filled);
// Arco di circonferenza spesso `thick` px, da 0 a `deg` gradi in senso orario
// partendo dalle ore 12. Usato per l'anello di "tieni premuto".
void gfx_arc(int cx, int cy, int r, int thick, int deg, uint32_t rgb);

// Clipping: tutto ciò che cade fuori dal rettangolo non viene disegnato.
void gfx_set_clip(int x, int y, int w, int h);
void gfx_reset_clip(void);

// Testo UTF-8 proporzionale. Restituisce la larghezza disegnata.
int gfx_text(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb);
int gfx_text_width(const gfx_font_t *f, const char *s);
// Come gfx_text, con ogni pixel del glifo ingrandito `scale` volte (interi). Restituisce la larghezza disegnata.
int gfx_text_scaled(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb, int scale);
int gfx_text_scaled_width(const gfx_font_t *f, const char *s, int scale);
void gfx_text_aligned(const gfx_font_t *f, int x, int y, int w, gfx_align_t a, const char *s, uint32_t rgb);
// Disegna il testo troncandolo con "…" se supera max_w.
void gfx_text_fit(const gfx_font_t *f, int x, int y, int max_w, const char *s, uint32_t rgb);

void gfx_icon(const gfx_icon_t *ic, int x, int y, int scale, uint32_t rgb);
