#include "hid.h"

#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usbdev.h"

#define REPORT_TIMEOUT_MS 500

static const hid_layout_t *s_layout;
static hid_os_t s_os;
static hid_timing_t s_timing;
static hid_poll_fn s_poll;
static void *s_poll_arg;

// Current state of the virtual keyboard (for hid_down/hid_up)
static uint8_t s_mods;
static uint8_t s_keys[6];
static uint8_t s_buttons;

void hid_reset(void)
{
    s_layout = hid_layout_find("us");
    s_os = HID_OS_WINDOWS;
    s_timing = (hid_timing_t){.key_delay = 8, .hold = 5};
    s_mods = 0;
    memset(s_keys, 0, sizeof(s_keys));
    s_buttons = 0;
}

void hid_set_layout(const hid_layout_t *l)
{
    if (l) s_layout = l;
}

const hid_layout_t *hid_get_layout(void)
{
    if (!s_layout) hid_reset();
    return s_layout;
}

void hid_set_os(hid_os_t os)
{
    s_os = os;
}

hid_os_t hid_get_os(void)
{
    return s_os;
}

hid_timing_t *hid_timing(void)
{
    return &s_timing;
}

void hid_set_poll(hid_poll_fn fn, void *arg)
{
    s_poll = fn;
    s_poll_arg = arg;
}

hid_result_t hid_sleep(uint32_t ms)
{
    int64_t end = esp_timer_get_time() + ms * 1000LL;
    for (;;) {
        if (s_poll && !s_poll(s_poll_arg)) return HID_ABORTED;
        int64_t left = (end - esp_timer_get_time()) / 1000;
        if (left <= 0) return HID_OK;
        vTaskDelay(pdMS_TO_TICKS(left < 10 ? left : 10) ?: 1);
    }
}

hid_result_t hid_wait_host(uint32_t timeout_ms)
{
    int64_t end = esp_timer_get_time() + timeout_ms * 1000LL;
    while (!usbdev_hid_ready()) {
        if (s_poll && !s_poll(s_poll_arg)) return HID_ABORTED;
        if (esp_timer_get_time() > end) return HID_NO_HOST;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return HID_OK;
}

static hid_result_t send(void)
{
    return usbdev_kbd_report(s_mods, s_keys, REPORT_TIMEOUT_MS) ? HID_OK : HID_NO_HOST;
}

static hid_result_t pause_between(void)
{
    int d = s_timing.key_delay;
    if (s_timing.jitter) d += (int)(esp_random() % (2 * s_timing.jitter + 1)) - s_timing.jitter;
    return hid_sleep(d > 0 ? d : 0);
}

// Caps Lock active on the host swaps upper and lower case of letters: we
// compensate by inverting Shift. It must be decided from the character, not
// the keycode: on Colemak the 'o' is on the ';' key (0x33) and the ';' on the
// P key (0x13), so checking the 0x04..0x1D range got it wrong.
static bool cased_letter(uint32_t cp)
{
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) return true;
    // Latin-1 letters with upper and lower case (excluding × ÷ ß ÿ)
    return cp >= 0xC0 && cp <= 0xFE && cp != 0xD7 && cp != 0xF7 && cp != 0xDF;
}

static hid_result_t tap(uint8_t key, uint8_t mods, bool letter)
{
    uint8_t saved = s_mods;
    if (letter && (usbdev_kbd_leds() & USBDEV_LED_CAPSLOCK)) mods ^= HID_MOD_LSHIFT;
    s_mods = saved | mods;
    s_keys[0] = key;
    hid_result_t r = send();
    if (r == HID_OK) r = hid_sleep(s_timing.hold);
    s_keys[0] = 0;
    s_mods = saved;
    hid_result_t r2 = send(); // the release must be sent anyway
    if (r == HID_OK) r = r2;
    if (r == HID_OK) r = pause_between();
    return r;
}

// Alt held down during all the keypad digits (Windows)
static hid_result_t type_altcode(const hid_stroke_t *s, int n)
{
    bool restore_numlock = false;
    if (!(usbdev_kbd_leds() & USBDEV_LED_NUMLOCK)) {
        hid_result_t r = tap(0x53, 0, false);
        if (r != HID_OK) return r;
        restore_numlock = true;
    }
    uint8_t saved = s_mods;
    s_mods = saved | HID_MOD_LALT;
    hid_result_t r = send();
    for (int i = 0; i < n && r == HID_OK; i++) {
        s_keys[0] = s[i].key;
        r = send();
        if (r == HID_OK) r = hid_sleep(s_timing.hold);
        s_keys[0] = 0;
        if (r == HID_OK) r = send();
        if (r == HID_OK) r = pause_between();
    }
    s_mods = saved;
    hid_result_t r2 = send();
    if (r == HID_OK) r = r2;
    if (r == HID_OK) r = pause_between();
    if (restore_numlock && r == HID_OK) r = tap(0x53, 0, false);
    return r;
}

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
        if ((p[i] & 0xC0) != 0x80) { *s += i; return 0xFFFD; }
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return cp;
}

hid_result_t hid_type(const char *s, uint32_t *bad_cp)
{
    hid_get_layout();
    while (*s) {
        uint32_t cp = utf8_next(&s);
        if (cp == '\r') continue; // Windows files have \r\n: the Enter for \n is enough
        hid_stroke_t st[8];
        bool numpad;
        int n = hid_translate(s_layout, s_os, cp, st, 8, &numpad);
        if (n == 0) {
            if (bad_cp) *bad_cp = cp;
            return HID_UNTYPEABLE;
        }
        hid_result_t r = HID_OK;
        if (numpad) {
            r = type_altcode(st, n);
        } else {
            // With a dead key the letter is the second key: Caps Lock only
            // affects that one. Longer sequences (Linux Ctrl+Shift+U) do not
            // produce letters to compensate.
            bool letter = cased_letter(cp) && n <= 2;
            for (int i = 0; i < n && r == HID_OK; i++) r = tap(st[i].key, st[i].mods, letter && i == n - 1);
        }
        if (r != HID_OK) return r;
        if (cp == '\n' && s_timing.line_delay) {
            r = hid_sleep(s_timing.line_delay);
            if (r != HID_OK) return r;
        }
    }
    return HID_OK;
}

bool hid_char_key(uint32_t cp, uint8_t *key, uint8_t *mods)
{
    hid_stroke_t st[8];
    bool numpad;
    if (hid_translate(hid_get_layout(), s_os, cp, st, 8, &numpad) != 1 || numpad) return false;
    *key = st[0].key;
    *mods = st[0].mods;
    return true;
}

hid_result_t hid_chord(uint8_t mods, const uint8_t *keys, int nkeys)
{
    uint8_t saved_mods = s_mods;
    s_mods |= mods;
    // Modifiers alone first: some applications ignore the combination
    // if modifier and key arrive in the same report.
    hid_result_t r = send();
    if (r == HID_OK) r = hid_sleep(s_timing.hold);
    for (int i = 0; i < nkeys && i < 6 && r == HID_OK; i++) {
        s_keys[i] = keys[i];
        r = send();
    }
    if (r == HID_OK) r = hid_sleep(s_timing.hold);
    memset(s_keys, 0, sizeof(s_keys));
    s_mods = saved_mods;
    hid_result_t r2 = send();
    if (r == HID_OK) r = r2;
    if (r == HID_OK) r = pause_between();
    return r;
}

hid_result_t hid_down(uint8_t mods, uint8_t key)
{
    s_mods |= mods;
    if (key) {
        for (int i = 0; i < 6; i++) {
            if (s_keys[i] == key) break;
            if (!s_keys[i]) {
                s_keys[i] = key;
                break;
            }
        }
    }
    return send();
}

hid_result_t hid_up(uint8_t mods, uint8_t key)
{
    s_mods &= ~mods;
    for (int i = 0; key && i < 6; i++) {
        if (s_keys[i] == key) s_keys[i] = 0;
    }
    return send();
}

hid_result_t hid_release_all(void)
{
    s_mods = 0;
    memset(s_keys, 0, sizeof(s_keys));
    if (s_buttons) {
        s_buttons = 0;
        usbdev_mouse_report(0, 0, 0, 0, 0, REPORT_TIMEOUT_MS);
    }
    return send();
}

static hid_result_t mouse(int8_t x, int8_t y, int8_t w, int8_t p)
{
    if (!usbdev_has_mouse()) return HID_NO_HOST;
    return usbdev_mouse_report(s_buttons, x, y, w, p, REPORT_TIMEOUT_MS) ? HID_OK : HID_NO_HOST;
}

hid_result_t hid_mouse_move(int dx, int dy)
{
    while (dx || dy) {
        int sx = dx > 127 ? 127 : (dx < -127 ? -127 : dx);
        int sy = dy > 127 ? 127 : (dy < -127 ? -127 : dy);
        hid_result_t r = mouse(sx, sy, 0, 0);
        if (r != HID_OK) return r;
        dx -= sx;
        dy -= sy;
        // At most one report per ms (bInterval 1): no need to wait
        if (s_poll && !s_poll(s_poll_arg)) return HID_ABORTED;
    }
    return HID_OK;
}

hid_result_t hid_mouse_buttons(uint8_t buttons)
{
    s_buttons = buttons;
    return mouse(0, 0, 0, 0);
}

hid_result_t hid_mouse_scroll(int wheel, int pan)
{
    return mouse(0, 0, wheel, pan);
}

hid_result_t hid_mouse_click(uint8_t button)
{
    hid_result_t r = hid_mouse_buttons(s_buttons | button);
    if (r == HID_OK) r = hid_sleep(s_timing.hold + 20);
    hid_result_t r2 = hid_mouse_buttons(s_buttons & ~button);
    if (r == HID_OK) r = r2;
    if (r == HID_OK) r = pause_between();
    return r;
}
