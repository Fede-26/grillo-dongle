#include "gfx.h"

#include <math.h>
#include <string.h>
#include "display.h"

static int s_cx0 = 0, s_cy0 = 0, s_cx1 = DISPLAY_W, s_cy1 = DISPLAY_H;

uint16_t gfx_rgb(uint32_t rgb)
{
    uint16_t c = ((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F);
    return DISPLAY_SWAP(c);
}

uint32_t gfx_mix(uint32_t a, uint32_t b, uint8_t t)
{
    uint32_t out = 0;
    for (int sh = 0; sh <= 16; sh += 8) {
        int ca = (a >> sh) & 0xFF, cb = (b >> sh) & 0xFF;
        out |= (uint32_t)((ca * (255 - t) + cb * t) / 255) << sh;
    }
    return out;
}

void gfx_set_clip(int x, int y, int w, int h)
{
    s_cx0 = x < 0 ? 0 : x;
    s_cy0 = y < 0 ? 0 : y;
    s_cx1 = x + w > DISPLAY_W ? DISPLAY_W : x + w;
    s_cy1 = y + h > DISPLAY_H ? DISPLAY_H : y + h;
}

void gfx_reset_clip(void)
{
    gfx_set_clip(0, 0, DISPLAY_W, DISPLAY_H);
}

void gfx_pixel(int x, int y, uint16_t c)
{
    if (x < s_cx0 || y < s_cy0 || x >= s_cx1 || y >= s_cy1) return;
    display_fb()[y * DISPLAY_W + x] = c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t rgb)
{
    int x0 = x < s_cx0 ? s_cx0 : x, y0 = y < s_cy0 ? s_cy0 : y;
    int x1 = x + w > s_cx1 ? s_cx1 : x + w, y1 = y + h > s_cy1 ? s_cy1 : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    uint16_t c = gfx_rgb(rgb);
    uint16_t *fb = display_fb();
    for (int yy = y0; yy < y1; yy++) {
        uint16_t *p = fb + yy * DISPLAY_W + x0;
        for (int xx = x0; xx < x1; xx++) *p++ = c;
    }
}

void gfx_clear(uint32_t rgb)
{
    gfx_fill_rect(0, 0, DISPLAY_W, DISPLAY_H, rgb);
}

void gfx_hline(int x, int y, int w, uint32_t rgb)
{
    gfx_fill_rect(x, y, w, 1, rgb);
}

void gfx_vline(int x, int y, int h, uint32_t rgb)
{
    gfx_fill_rect(x, y, 1, h, rgb);
}

void gfx_rect(int x, int y, int w, int h, uint32_t rgb)
{
    gfx_hline(x, y, w, rgb);
    gfx_hline(x, y + h - 1, w, rgb);
    gfx_vline(x, y, h, rgb);
    gfx_vline(x + w - 1, y, h, rgb);
}

void gfx_round_rect(int x, int y, int w, int h, int r, uint32_t rgb, bool filled)
{
    uint16_t c = gfx_rgb(rgb);
    for (int yy = 0; yy < h; yy++) {
        // Vertical distance from the nearest corner, to work out how
        // many pixels to "cut" at the sides on this row.
        int dy = yy < r ? r - yy - 1 : (yy >= h - r ? yy - (h - r) : -1);
        int inset = 0;
        if (dy >= 0) {
            float dx = sqrtf((float)(r * r) - (dy + 0.5f) * (dy + 0.5f));
            inset = r - (int)(dx + 0.5f);
        }
        if (filled || yy == 0 || yy == h - 1) {
            for (int xx = x + inset; xx < x + w - inset; xx++) gfx_pixel(xx, y + yy, c);
        } else {
            gfx_pixel(x + inset, y + yy, c);
            gfx_pixel(x + w - 1 - inset, y + yy, c);
            // Curved rows need more pixels to close the border
            if (dy >= 0) {
                int prev = yy < r ? yy - 1 : yy + 1;
                int pdy = prev < r ? r - prev - 1 : (prev >= h - r ? prev - (h - r) : -1);
                int pin = 0;
                if (pdy >= 0) {
                    float pdx = sqrtf((float)(r * r) - (pdy + 0.5f) * (pdy + 0.5f));
                    pin = r - (int)(pdx + 0.5f);
                }
                for (int k = pin; k < inset; k++) {
                    gfx_pixel(x + k, y + yy, c);
                    gfx_pixel(x + w - 1 - k, y + yy, c);
                }
            }
        }
    }
}

void gfx_circle(int cx, int cy, int r, uint32_t rgb, bool filled)
{
    uint16_t c = gfx_rgb(rgb);
    int r2o = r * r + r, r2i = (r - 1) * (r - 1) + (r - 1);
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= r2o && (filled || d > r2i)) gfx_pixel(cx + dx, cy + dy, c);
        }
    }
}

void gfx_arc(int cx, int cy, int r, int thick, int deg, uint32_t rgb)
{
    uint16_t c = gfx_rgb(rgb);
    int ro2 = r * r, ri = r - thick, ri2 = ri * ri;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d > ro2 || d < ri2) continue;
            // Clockwise angle from 12 o'clock
            float a = atan2f((float)dx, (float)-dy) * 57.29578f;
            if (a < 0) a += 360.0f;
            if (a <= deg) gfx_pixel(cx + dx, cy + dy, c);
        }
    }
}

// Minimal UTF-8 decoding: returns the codepoint and advances *s.
static uint32_t utf8_next(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t cp;
    int n;
    if (p[0] < 0x80) { cp = p[0]; n = 1; }
    else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; n = 2; }
    else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; n = 3; }
    else { cp = p[0] & 0x07; n = 4; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *s += i; return '?'; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return cp;
}

static int glyph_index(const gfx_font_t *f, uint32_t cp)
{
    if (cp >= 0x20 && cp < 0x7F) return cp - 0x20; // ASCII: direct access
    for (int i = 0x7F - 0x20; i < f->count; i++) {
        if (f->codepoints[i] == cp) return i;
    }
    return '?' - 0x20;
}

int gfx_text(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb)
{
    uint16_t c = gfx_rgb(rgb);
    int x0 = x;
    while (*s) {
        int gi = glyph_index(f, utf8_next(&s));
        const uint8_t *rows = f->bits + gi * f->height;
        int left = f->metrics[gi * 2], w = f->metrics[gi * 2 + 1];
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = rows[r] << left;
            for (int col = 0; col < w; col++) {
                if (bits & (0x80 >> col)) gfx_pixel(x + col, y + r, c);
            }
        }
        x += w + 1;
    }
    return x - x0;
}

int gfx_text_scaled(const gfx_font_t *f, int x, int y, const char *s, uint32_t rgb, int scale)
{
    uint16_t c = gfx_rgb(rgb);
    int x0 = x;
    while (*s) {
        int gi = glyph_index(f, utf8_next(&s));
        const uint8_t *rows = f->bits + gi * f->height;
        int left = f->metrics[gi * 2], w = f->metrics[gi * 2 + 1];
        for (int r = 0; r < f->height; r++) {
            uint8_t bits = rows[r] << left;
            for (int col = 0; col < w; col++) {
                if (!(bits & (0x80 >> col))) continue;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) gfx_pixel(x + col * scale + dx, y + r * scale + dy, c);
                }
            }
        }
        x += (w + 1) * scale;
    }
    return x - x0;
}

int gfx_text_scaled_width(const gfx_font_t *f, const char *s, int scale)
{
    int w = gfx_text_width(f, s);
    return w > 0 ? (w + 1) * scale - 1 : 0;
}

int gfx_text_width(const gfx_font_t *f, const char *s)
{
    int w = 0;
    while (*s) w += f->metrics[glyph_index(f, utf8_next(&s)) * 2 + 1] + 1;
    return w > 0 ? w - 1 : 0;
}

void gfx_text_aligned(const gfx_font_t *f, int x, int y, int w, gfx_align_t a, const char *s, uint32_t rgb)
{
    int tw = gfx_text_width(f, s);
    if (a == GFX_ALIGN_CENTER) x += (w - tw) / 2;
    else if (a == GFX_ALIGN_RIGHT) x += w - tw;
    gfx_text(f, x, y, s, rgb);
}

void gfx_text_fit(const gfx_font_t *f, int x, int y, int max_w, const char *s, uint32_t rgb)
{
    if (gfx_text_width(f, s) <= max_w) {
        gfx_text(f, x, y, s, rgb);
        return;
    }
    // Truncate character by character until text + "…" fits.
    char buf[96];
    size_t n = strnlen(s, sizeof(buf) - 4);
    memcpy(buf, s, n);
    int ell = gfx_text_width(f, "…") + 1;
    while (n > 0) {
        // Step back by a whole UTF-8 character
        do { n--; } while (n > 0 && ((uint8_t)buf[n] & 0xC0) == 0x80);
        buf[n] = 0;
        if (gfx_text_width(f, buf) + ell <= max_w) break;
    }
    strcpy(buf + n, "…");
    gfx_text(f, x, y, buf, rgb);
}

void gfx_icon(const gfx_icon_t *ic, int x, int y, int scale, uint32_t rgb)
{
    for (int r = 0; r < ic->h; r++) {
        for (int col = 0; col < ic->w; col++) {
            if (ic->rows[r][col] == '#') gfx_fill_rect(x + col * scale, y + r * scale, scale, scale, rgb);
        }
    }
}
