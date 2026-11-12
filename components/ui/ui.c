#include "ui.h"

#include <string.h>
#include "display.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_lock;
static char s_title[24] = "Grillo";
static uint32_t s_accent = UI_GRILLO;
static uint8_t s_ind[UI_IND_COUNT];
static uint8_t s_hold;
static bool s_armed;
static int64_t s_flash_until;
static esp_timer_handle_t s_flash_timer;
static char s_toast[40];
static uint32_t s_toast_color;
static int64_t s_toast_until;
static bool s_bar_visible = true;
static esp_timer_handle_t s_toast_timer;

static const char *const ICON_SD[] = {
    "..####", ".#####", "######", "######", "######", "######", "######", "######",
};
static const gfx_icon_t s_icon_sd = {6, 8, ICON_SD};

static void lock(void)
{
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

void ui_lock(void)
{
    lock();
}

void ui_unlock(void)
{
    unlock();
}

static void draw_wifi(int x, int y, uint8_t v)
{
    if (v == 6) { // hotspot active
        gfx_text(&gfx_font_small, x, y, "AP", UI_TEXT);
        return;
    }
    int bars = v >= 2 ? v - 1 : 0; // 2..5 -> 1..4 bars
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        uint32_t c = v == 1 ? ((esp_timer_get_time() / 300000) % 4 == i ? UI_TEXT : UI_FAINT)
                            : (i < bars ? UI_TEXT : UI_FAINT);
        gfx_fill_rect(x + i * 3, y + 8 - h, 2, h, c);
    }
}

// Draws the status bar in the framebuffer (lock already held).
static void draw_bar(void)
{
    if (!s_bar_visible) return;
    gfx_reset_clip();
    int64_t now = esp_timer_get_time();
    bool toast = s_toast_until > now;
    uint32_t bg = toast ? gfx_mix(UI_PANEL, s_toast_color, 70) : UI_PANEL;
    // The flash lightens the background without covering the text, which stays readable
    if (s_flash_until > now) bg = gfx_mix(UI_PANEL, UI_TEXT, 110);
    gfx_fill_rect(0, 0, UI_W, UI_BAR_H, bg);

    int x = UI_W - 3;
    // Indicators from right to left
    uint8_t sd = s_ind[UI_IND_SD];
    if (sd) {
        uint32_t c = sd == 1 ? UI_TEXT : (sd == 2 ? UI_ERR : UI_WARN);
        x -= 6;
        gfx_icon(&s_icon_sd, x, 2, 1, c);
        x -= 4;
    }
    uint8_t usb = s_ind[UI_IND_USB];
    if (usb) {
        const char *t = usb == 1 ? "HID" : (usb == 2 ? "MSC" : "HID+D");
        int w = gfx_text_width(&gfx_font_small, t);
        x -= w;
        gfx_text(&gfx_font_small, x, 2, t, UI_DIM);
        x -= 4;
    }
    uint8_t wifi = s_ind[UI_IND_WIFI];
    if (wifi) {
        x -= 11;
        draw_wifi(x, 2, wifi);
        x -= 4;
    }

    if (toast) {
        gfx_text_fit(&gfx_font_small, 3, 2, x - 5, s_toast, UI_TEXT);
    } else {
        gfx_text_fit(&gfx_font_small, 3, 2, x - 5, s_title, s_accent);
    }

    // Accent line below the bar; during the hold it becomes the exit
    // progress bar.
    gfx_fill_rect(0, UI_BAR_H, UI_W, 2, UI_BG);
    if (s_hold) {
        int w = UI_W * s_hold / 100;
        gfx_fill_rect(0, UI_BAR_H, UI_W, 2, UI_FAINT);
        gfx_fill_rect(0, UI_BAR_H, w, 2, UI_ERR);
    } else if (s_armed) {
        gfx_fill_rect(0, UI_BAR_H, UI_W, 2, UI_TEXT);
    } else {
        gfx_hline(0, UI_BAR_H, UI_W, gfx_mix(UI_BG, s_accent, 160));
    }
}

static void refresh_bar(void)
{
    lock();
    draw_bar();
    display_flush();
    unlock();
}

static void toast_expired(void *arg)
{
    refresh_bar();
}

void ui_init(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    esp_timer_create_args_t t = {.callback = toast_expired, .name = "toast"};
    esp_timer_create(&t, &s_toast_timer);
    // Same callback as the toast: once the flash expires, redrawing the bar is enough
    esp_timer_create_args_t f = {.callback = toast_expired, .name = "barflash"};
    esp_timer_create(&f, &s_flash_timer);
}

void ui_begin(void)
{
    lock();
    gfx_reset_clip();
    int y = s_bar_visible ? UI_CONTENT_Y : 0;
    gfx_fill_rect(0, y, UI_W, UI_H - y, UI_BG);
    gfx_set_clip(0, y, UI_W, UI_H - y);
}

void ui_end(void)
{
    draw_bar();
    display_flush();
    unlock();
}

void ui_begin_full(void)
{
    lock();
    gfx_reset_clip();
}

void ui_end_full(void)
{
    gfx_reset_clip();
    display_flush();
    unlock();
}

void ui_set_title(const char *title, uint32_t accent)
{
    lock();
    strlcpy(s_title, title, sizeof(s_title));
    s_accent = accent;
    unlock();
    refresh_bar();
}

void ui_get_title(char *buf, size_t len)
{
    lock();
    strlcpy(buf, s_title, len);
    unlock();
}

uint32_t ui_accent(void)
{
    return s_accent;
}

void ui_set_indicator(ui_indicator_t ind, uint8_t value)
{
    if (s_ind[ind] == value) return;
    s_ind[ind] = value;
    refresh_bar();
}

void ui_set_hold(uint8_t progress)
{
    if (s_hold == progress) return;
    s_hold = progress;
    refresh_bar();
}

void ui_set_armed(bool armed)
{
    if (s_armed == armed) return;
    s_armed = armed;
    refresh_bar();
}

void ui_flash_bar(uint32_t ms)
{
    lock();
    s_flash_until = esp_timer_get_time() + ms * 1000LL;
    unlock();
    esp_timer_stop(s_flash_timer);
    esp_timer_start_once(s_flash_timer, ms * 1000ULL + 1000);
    refresh_bar();
}

void ui_toast(const char *msg, uint32_t color, uint32_t ms)
{
    lock();
    strlcpy(s_toast, msg, sizeof(s_toast));
    s_toast_color = color;
    s_toast_until = esp_timer_get_time() + ms * 1000LL;
    unlock();
    esp_timer_stop(s_toast_timer);
    esp_timer_start_once(s_toast_timer, ms * 1000ULL + 1000);
    refresh_bar();
}

void ui_bar_visible(bool visible)
{
    s_bar_visible = visible;
}

// ---------------------------------------------------------------------------
// Layout

int ui_list_visible_rows(void)
{
    return UI_CONTENT_H / UI_ROW_H;
}

int ui_list_scroll(int top, int sel, int count)
{
    int vis = ui_list_visible_rows();
    if (sel < top) top = sel;
    if (sel >= top + vis) top = sel - vis + 1;
    if (top > count - vis) top = count - vis;
    return top < 0 ? 0 : top;
}

// Horizontal scrolling of a selected row that does not fit: pause at the
// start, scroll to the end, pause, then from the top.
#define MARQUEE_PAUSE_MS 1000
#define MARQUEE_PX_S     30
static const char *s_mq_text;
static int64_t s_mq_t0;
static bool s_mq_active;

void ui_marquee_reset(void)
{
    s_mq_text = NULL;
}

bool ui_marquee_active(void)
{
    return s_mq_active;
}

// Pixel offset for a text that exceeds the space by `over` pixels. The cycle
// restarts when the selected row changes (the pointer changes).
static int marquee_offset(const char *text, int over)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (text != s_mq_text) {
        s_mq_text = text;
        s_mq_t0 = now;
    }
    s_mq_active = true;
    int run = over * 1000 / MARQUEE_PX_S;
    int t = (now - s_mq_t0) % (MARQUEE_PAUSE_MS + run + MARQUEE_PAUSE_MS);
    if (t < MARQUEE_PAUSE_MS) return 0;
    if (t >= MARQUEE_PAUSE_MS + run) return over;
    return (t - MARQUEE_PAUSE_MS) * MARQUEE_PX_S / 1000;
}

void ui_draw_row(int y, const char *text, const char *right, bool selected, bool enabled)
{
    int w = UI_W - 6;
    if (selected) {
        gfx_round_rect(2, y, w, UI_ROW_H, 3, gfx_mix(UI_BG, s_accent, 70), true);
        gfx_fill_rect(2, y + 2, 2, UI_ROW_H - 4, s_accent);
    }
    uint32_t c = !enabled ? UI_FAINT : (selected ? UI_TEXT : gfx_mix(UI_TEXT, UI_DIM, 120));
    int rw = 0;
    if (right && *right) {
        rw = gfx_text_width(&gfx_font_small, right);
        gfx_text(&gfx_font_small, 2 + w - rw - 4, y + 2, right, selected ? s_accent : UI_DIM);
        rw += 6;
    }
    int avail = w - 10 - rw, over = gfx_text_width(&gfx_font_small, text) - avail;
    if (!selected || over <= 0) {
        gfx_text_fit(&gfx_font_small, 8, y + 2, avail, text, c);
        return;
    }
    gfx_set_clip(8, y, avail, UI_ROW_H);
    gfx_text(&gfx_font_small, 8 - marquee_offset(text, over), y + 2, text, c);
    // Back to ui_begin's clipping
    int cy = s_bar_visible ? UI_CONTENT_Y : 0;
    gfx_set_clip(0, cy, UI_W, UI_H - cy);
}

void ui_draw_list(int top, int sel, int count, ui_label_fn label, void *arg)
{
    s_mq_active = false; // ui_draw_row turns it back on if the selected row scrolls
    int vis = ui_list_visible_rows();
    int y0 = UI_CONTENT_Y + (UI_CONTENT_H - vis * UI_ROW_H) / 2;
    for (int i = 0; i < vis && top + i < count; i++) {
        const char *right = NULL;
        bool enabled = true;
        const char *t = label(top + i, arg, &right, &enabled);
        ui_draw_row(y0 + i * UI_ROW_H, t, right, top + i == sel, enabled);
    }
    if (count > vis) {
        // Thin scrollbar on the right
        int h = vis * UI_ROW_H;
        int th = h * vis / count;
        if (th < 4) th = 4;
        int ty = y0 + (h - th) * top / (count - vis);
        gfx_fill_rect(UI_W - 2, y0, 1, h, UI_FAINT);
        gfx_fill_rect(UI_W - 2, ty, 1, th, s_accent);
    }
}

void ui_draw_progress(int x, int y, int w, int h, int value, int max, uint32_t color)
{
    gfx_round_rect(x, y, w, h, h / 2, UI_PANEL_HI, true);
    if (max <= 0) return;
    int fw = (int)((int64_t)w * (value < max ? value : max) / max);
    if (fw >= h) gfx_round_rect(x, y, fw, h, h / 2, color, true);
    else if (fw > 0) gfx_fill_rect(x, y + 1, fw, h - 2, color);
}

void ui_draw_pager(int y, int count, int current, uint32_t color)
{
    int gap = 7, w = (count - 1) * gap;
    int x0 = (UI_W - w) / 2;
    for (int i = 0; i < count; i++) {
        if (i == current) gfx_fill_rect(x0 + i * gap - 2, y - 1, 5, 3, color);
        else gfx_fill_rect(x0 + i * gap - 1, y - 1, 3, 3, UI_FAINT);
    }
}

int ui_draw_wrapped(const gfx_font_t *f, int x, int y, int w, int max_lines, const char *text, uint32_t rgb)
{
    char line[64];
    int lines = 0;
    const char *p = text;
    while (*p && lines < max_lines) {
        // Takes words while the line fits in `w`
        int len = 0, last_space = -1;
        while (p[len] && p[len] != '\n' && len < (int)sizeof(line) - 1) {
            memcpy(line, p, len + 1);
            line[len + 1] = 0;
            if (gfx_text_width(f, line) > w) break;
            if (p[len] == ' ') last_space = len;
            len++;
        }
        int take = len;
        bool at_end = !p[len] || p[len] == '\n';
        if (!at_end && last_space > 0) take = last_space;
        if (take == 0) take = 1; // word longer than the line: break it
        memcpy(line, p, take);
        line[take] = 0;
        if (lines == max_lines - 1 && !at_end) gfx_text_fit(f, x, y, w, p, rgb);
        else gfx_text(f, x, y, line, rgb);
        p += take;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
        y += f->height + 2;
        lines++;
    }
    return lines;
}

int ui_draw_wrapped_scroll(const gfx_font_t *f, int x, int y, int w, int max_lines, int skip, const char *text,
                           uint32_t rgb)
{
    char line[64];
    int lines = 0;
    const char *p = text;
    while (*p) {
        // Same line breaking as ui_draw_wrapped
        int len = 0, last_space = -1;
        while (p[len] && p[len] != '\n' && len < (int)sizeof(line) - 1) {
            memcpy(line, p, len + 1);
            line[len + 1] = 0;
            if (gfx_text_width(f, line) > w) break;
            if (p[len] == ' ') last_space = len;
            len++;
        }
        int take = len;
        bool at_end = !p[len] || p[len] == '\n';
        if (!at_end && last_space > 0) take = last_space;
        if (take == 0) take = 1;
        if (lines >= skip && lines < skip + max_lines) {
            memcpy(line, p, take);
            line[take] = 0;
            gfx_text(f, x, y + (lines - skip) * (f->height + 2), line, rgb);
        }
        p += take;
        while (*p == ' ') p++;
        if (*p == '\n') p++;
        lines++;
    }
    return lines;
}

void ui_draw_hint(const char *hint)
{
    gfx_fill_rect(0, UI_H - 9, UI_W, 9, UI_BG);
    gfx_text_aligned(&gfx_font_small, 0, UI_H - 8, UI_W, GFX_ALIGN_CENTER, hint, UI_FAINT);
}

// ---------------------------------------------------------------------------
// Scrolling text

#define SCROLL_FIRST_MS 2000 // time to read the first lines
#define SCROLL_STEP_MS  1100 // one more line
#define SCROLL_LAST_MS  2500 // pause on the last lines before starting over

void ui_scroll_init(ui_scroll_t *s, const gfx_font_t *f, int x, int y, int w, int lines, const char *text)
{
    *s = (ui_scroll_t){.font = f, .text = text ? text : "", .x = x, .y = y, .w = w, .lines = lines};
    // With max_lines = 0 the function only counts
    s->total = ui_draw_wrapped_scroll(f, 0, 0, w, 0, 0, s->text, 0);
    s->next_us = esp_timer_get_time() + SCROLL_FIRST_MS * 1000LL;
}

bool ui_scroll_active(const ui_scroll_t *s)
{
    return s->total > s->lines;
}

static void scroll_advance(ui_scroll_t *s)
{
    int last = s->total - s->lines;
    if (s->top >= last) {
        s->top = 0;
        s->cycles++;
        s->next_us = esp_timer_get_time() + SCROLL_FIRST_MS * 1000LL;
    } else {
        s->top++;
        s->next_us = esp_timer_get_time() + (s->top == last ? SCROLL_LAST_MS : SCROLL_STEP_MS) * 1000LL;
    }
}

bool ui_scroll_tick(ui_scroll_t *s)
{
    if (!ui_scroll_active(s) || esp_timer_get_time() < s->next_us) return false;
    scroll_advance(s);
    return true;
}

void ui_scroll_step(ui_scroll_t *s)
{
    if (!ui_scroll_active(s)) return;
    scroll_advance(s);
    // Whoever scrolls by hand is reading: give them more time
    if (s->top) s->next_us = esp_timer_get_time() + SCROLL_LAST_MS * 1000LL;
}

uint32_t ui_scroll_wait_ms(const ui_scroll_t *s)
{
    if (!ui_scroll_active(s)) return portMAX_DELAY;
    int64_t left = s->next_us - esp_timer_get_time();
    return left > 0 ? (uint32_t)(left / 1000) + 1 : 0;
}

void ui_scroll_draw(const ui_scroll_t *s, uint32_t rgb)
{
    ui_draw_wrapped_scroll(s->font, s->x, s->y, s->w, s->lines, s->top, s->text, rgb);
    if (!ui_scroll_active(s)) return;
    int lh = s->font->height + 2, h = s->lines * lh - 2;
    int th = h * s->lines / s->total;
    if (th < 3) th = 3;
    int ty = s->y + (h - th) * s->top / (s->total - s->lines);
    int bx = s->x + s->w + 2;
    if (bx >= UI_W) bx = UI_W - 1;
    gfx_fill_rect(bx, s->y, 1, h, UI_FAINT);
    gfx_fill_rect(bx, ty, 1, th, s_accent);
}
