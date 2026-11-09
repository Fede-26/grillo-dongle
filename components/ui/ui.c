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
    if (v == 6) { // hotspot attivo
        gfx_text(&gfx_font_small, x, y, "AP", UI_TEXT);
        return;
    }
    int bars = v >= 2 ? v - 1 : 0; // 2..5 -> 1..4 barre
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        uint32_t c = v == 1 ? ((esp_timer_get_time() / 300000) % 4 == i ? UI_TEXT : UI_FAINT)
                            : (i < bars ? UI_TEXT : UI_FAINT);
        gfx_fill_rect(x + i * 3, y + 8 - h, 2, h, c);
    }
}

// Disegna la status bar nel framebuffer (lock già preso).
static void draw_bar(void)
{
    if (!s_bar_visible) return;
    gfx_reset_clip();
    bool toast = s_toast_until > esp_timer_get_time();
    uint32_t bg = toast ? gfx_mix(UI_PANEL, s_toast_color, 70) : UI_PANEL;
    gfx_fill_rect(0, 0, UI_W, UI_BAR_H, bg);

    int x = UI_W - 3;
    // Indicatori da destra verso sinistra
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
        x -= wifi == 6 ? 11 : 11;
        draw_wifi(x, 2, wifi);
        x -= 4;
    }

    if (toast) {
        gfx_text_fit(&gfx_font_small, 3, 2, x - 5, s_toast, UI_TEXT);
    } else {
        gfx_text_fit(&gfx_font_small, 3, 2, x - 5, s_title, s_accent);
    }

    // Linea d'accento sotto la barra; durante l'hold diventa la barra di
    // avanzamento dell'uscita.
    gfx_fill_rect(0, UI_BAR_H, UI_W, 2, UI_BG);
    if (s_hold) {
        int w = UI_W * s_hold / 100;
        gfx_fill_rect(0, UI_BAR_H, UI_W, 2, UI_FAINT);
        gfx_fill_rect(0, UI_BAR_H, w, 2, UI_ERR);
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
    gfx_text_fit(&gfx_font_small, 8, y + 2, w - 10 - rw, text, c);
}

void ui_draw_list(int top, int sel, int count, ui_label_fn label, void *arg)
{
    int vis = ui_list_visible_rows();
    int y0 = UI_CONTENT_Y + (UI_CONTENT_H - vis * UI_ROW_H) / 2;
    for (int i = 0; i < vis && top + i < count; i++) {
        const char *right = NULL;
        bool enabled = true;
        const char *t = label(top + i, arg, &right, &enabled);
        ui_draw_row(y0 + i * UI_ROW_H, t, right, top + i == sel, enabled);
    }
    if (count > vis) {
        // Scrollbar sottile a destra
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
        // Prende parole finché la riga entra in `w`
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
        if (take == 0) take = 1; // parola più lunga della riga: spezza
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
        // Stessa spezzatura di ui_draw_wrapped
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
