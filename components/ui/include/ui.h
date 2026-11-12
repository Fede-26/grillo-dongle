#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"

// Grillo UI toolkit: theme, system status bar, layout primitives.
//
// The screen is split into two zones:
//  - status bar (y 0..UI_BAR_H-1) + accent line: owned by the system;
//  - content area (UI_CONTENT_Y..79): owned by the foreground app.
// System notifications (toasts, the "hold to exit" bar) live in the status
// bar, so they never dirty the app's content.

#define UI_W          160
#define UI_H          80
#define UI_BAR_H      12
#define UI_CONTENT_Y  (UI_BAR_H + 2)
#define UI_CONTENT_H  (UI_H - UI_CONTENT_Y)

// Palette (0xRRGGBB)
#define UI_BG       0x0B0F14
#define UI_PANEL    0x1A2029
#define UI_PANEL_HI 0x252D38
#define UI_TEXT     0xE6EDF3
#define UI_DIM      0x7D8590
#define UI_FAINT    0x3A424D
#define UI_OK       0x3FB950
#define UI_WARN     0xD29922
#define UI_ERR      0xF85149
#define UI_GRILLO   0x7CFC00 // logo green

typedef enum {
    UI_IND_SD,   // 0 absent, 1 mounted, 2 not FAT, 3 lent to the host
    UI_IND_USB,  // 0 no profile, 1 HID, 2 MSC, 3 HID + disk
    UI_IND_WIFI, // 0 off, 1 connecting, 2..5 RSSI bars, 6 hotspot
    UI_IND_COUNT,
} ui_indicator_t;

void ui_init(void);

// Framebuffer lock, for whoever reads it from outside (screenshot).
void ui_lock(void);
void ui_unlock(void);

// Drawing of the content area by the app:
//   ui_begin();  ...gfx_*...  ui_end();
// ui_begin takes the lock, clears the content and sets the clipping;
// ui_end redraws the status bar, sends the frame and releases the lock.
void ui_begin(void);
void ui_end(void);

// Like ui_begin/ui_end but without clearing or limiting to the content area
// (full-screen splash).
void ui_begin_full(void);
void ui_end_full(void);

// State shown in the status bar (redrawn immediately).
void ui_set_title(const char *title, uint32_t accent);
// Copies the current title (to restore it after a dialog).
void ui_get_title(char *buf, size_t len);
uint32_t ui_accent(void);
void ui_set_indicator(ui_indicator_t ind, uint8_t value);
// Progress of the exit hold 0..100; 0 = hidden.
void ui_set_hold(uint8_t progress);
// "Armed" long press: the line below the status bar turns white while
// releasing counts as confirmation.
void ui_set_armed(bool armed);
// Short flash of the status bar (long press recognized).
void ui_flash_bar(uint32_t ms);
// Temporary message in place of the title.
void ui_toast(const char *msg, uint32_t color, uint32_t ms);
// Hides the status bar (splash) or restores it.
void ui_bar_visible(bool visible);

// --- Reusable layout elements (call between begin/end) ---

// List row UI_ROW_H tall; `selected` highlights it with the accent.
#define UI_ROW_H 11
void ui_draw_row(int y, const char *text, const char *right, bool selected, bool enabled);
// A selected row that does not fit scrolls horizontally. Whoever draws the
// list keeps redrawing it while ui_marquee_active() is true (it refers to the
// last ui_draw_list); ui_marquee_reset() restarts the initial pause.
void ui_marquee_reset(void);
bool ui_marquee_active(void);
// Scrollable list: draws `visible` rows starting from `top`.
typedef const char *(*ui_label_fn)(int index, void *arg, const char **right, bool *enabled);
void ui_draw_list(int top, int sel, int count, ui_label_fn label, void *arg);
int ui_list_visible_rows(void);
int ui_list_scroll(int top, int sel, int count); // new `top` that keeps `sel` visible

void ui_draw_progress(int x, int y, int w, int h, int value, int max, uint32_t color);
// Centered page dots
void ui_draw_pager(int y, int count, int current, uint32_t color);
// Multi-line text with automatic wrapping; returns the lines used.
int ui_draw_wrapped(const gfx_font_t *f, int x, int y, int w, int max_lines, const char *text, uint32_t rgb);
// Like ui_draw_wrapped but without truncating: skips the first `skip` lines, draws
// at most `max_lines` whole lines and returns the TOTAL number of lines of the
// text (with max_lines = 0 it draws nothing: used to count, for scrolling).
int ui_draw_wrapped_scroll(const gfx_font_t *f, int x, int y, int w, int max_lines, int skip, const char *text,
                           uint32_t rgb);
// Bottom bar with gesture hints, e.g. "click next · hold back"
void ui_draw_hint(const char *hint);

// --- Auto-scrolling text ---
//
// Multi-line text in an area of `lines` lines. If it does not fit, it scrolls
// by itself one line at a time: initial pause, one step at a time, pause on
// the last lines, then it starts over from the beginning. This way long
// errors and descriptions can be read in full without pressing anything. The
// caller:
//   ui_scroll_init(&s, ...);
//   for (;;) { ui_begin(); ui_scroll_draw(&s, color); ui_end();
//              app_next_event(ctx, &ev, ui_scroll_wait_ms(&s)); ui_scroll_tick(&s); ... }
typedef struct {
    const gfx_font_t *font;
    const char *text; // not copied: must stay valid while the scroller is in use
    int x, y, w, lines;
    int total;   // lines of the text once wrapped
    int top;     // first visible line
    int cycles;  // full cycles already shown (useful to close after the first one)
    int64_t next_us;
} ui_scroll_t;

void ui_scroll_init(ui_scroll_t *s, const gfx_font_t *f, int x, int y, int w, int lines, const char *text);
// true if the text does not fit in the area and therefore scrolls.
bool ui_scroll_active(const ui_scroll_t *s);
// Advances one step if it is time; true if the content changed.
bool ui_scroll_tick(ui_scroll_t *s);
// Manual step (e.g. click): advances immediately and postpones the automatic step.
void ui_scroll_step(ui_scroll_t *s);
// Milliseconds to the next automatic step, portMAX_DELAY if the text does not scroll.
uint32_t ui_scroll_wait_ms(const ui_scroll_t *s);
// Draws the visible lines and, if it scrolls, a thin bar to the right of the area.
void ui_scroll_draw(const ui_scroll_t *s, uint32_t rgb);
