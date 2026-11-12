#include "web_server.h"

#include <stdio.h>
#include <string.h>
#include "hid.h"
#include "led.h"
#include "websrv.h"
#include "webtheme.h"

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

#define MAX_FRAME      2048 // the page splits pastes into smaller chunks
#define MAX_ERASE      500
#define ACCENT         0x4070FF

// Length in bytes of the UTF-8 character at `s`, 0 if the sequence is
// invalid or truncated (end of frame): this avoids reading past the buffer.
static int utf8_len(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

static uint32_t utf8_first(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    switch (utf8_len(s)) {
    case 1: return p[0];
    case 2: return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3: return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    case 4: return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    default: return 0xFFFD;
    }
}

// Types the text one character at a time. A character the layout lacks
// becomes '?': previously typing stopped there and lost the rest of the
// chunk, and in live typing the page counted a character that was not on
// the PC (the following backspaces deleted one too many).
static hid_result_t type_text(const char *s, int *skipped)
{
    char one[5];
    while (*s) {
        int n = utf8_len(s);
        if (n == 0) n = 1; // invalid byte: treated as a character of its own
        memcpy(one, s, n);
        one[n] = 0;
        s += n;
        hid_result_t r = hid_type(one, NULL);
        if (r == HID_UNTYPEABLE) {
            (*skipped)++;
            r = hid_type("?", NULL);
            if (r == HID_UNTYPEABLE) r = HID_OK;
        }
        if (r != HID_OK) return r;
    }
    return HID_OK;
}

static bool keep_typing(void *arg)
{
    return !websrv_stopping();
}

static int clamp(int v, int lim)
{
    return v < -lim ? -lim : (v > lim ? lim : v);
}

// Commands (text, one type character + arguments):
//   m dx dy     moves the mouse       b mask      button state
//   w dy dx     wheel (dx optional)
//   t text      types the text in the host's layout
//   c mods ch   chord: modifiers + character (e.g. Ctrl+C)
//   k mods key  chord with a raw HID usage (special keys, F-keys)
//   e n         n backspaces in a row, 1..MAX_ERASE (this is how live typing deletes)
//   l name      host layout (it, us, colemak)
//   o name      host OS (windows, linux, mac): picks the fallbacks for missing characters
static hid_result_t command(const char *m, int *skipped)
{
    int a = 0, b = 0;
    switch (m[0]) {
    case 'm':
        // A huge dx kept the httpd task busy for hours of reports
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) return hid_mouse_move(clamp(a, 2000), clamp(b, 2000));
        break;
    case 'b':
        if (sscanf(m + 1, "%d", &a) == 1) return hid_mouse_buttons(a);
        break;
    case 'w':
        sscanf(m + 1, "%d %d", &a, &b);
        return hid_mouse_scroll(clamp(a, 127), clamp(b, 127));
    case 't': return type_text(m + 1, skipped);
    case 'c':
        if (sscanf(m + 1, "%d", &a) == 1) {
            const char *ch = strchr(m + 1, ' ');
            uint8_t key, mods;
            if (ch && ch[1] && hid_char_key(utf8_first(ch + 1), &key, &mods)) return hid_chord(a | mods, &key, 1);
        }
        break;
    case 'e':
        // The page splits long deletions into MAX_ERASE chunks
        if (sscanf(m + 1, "%d", &a) == 1 && a > 0 && a <= MAX_ERASE) {
            uint8_t bs = 0x2A; // Backspace
            for (int i = 0; i < a; i++) {
                hid_result_t r = hid_chord(0, &bs, 1);
                if (r != HID_OK) return r;
            }
        }
        break;
    case 'l': {
        const hid_layout_t *l = hid_layout_find(m + 1);
        if (l) hid_set_layout(l);
        break;
    }
    case 'o': {
        hid_os_t os;
        if (hid_os_parse(m + 1, &os)) hid_set_os(os);
        break;
    }
    case 'k':
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) {
            uint8_t key = b;
            return hid_chord(a, &key, b ? 1 : 0);
        }
        break;
    default: break;
    }
    return HID_OK;
}

// The session ends (page closed, phone reconnecting, app
// exit): no key or mouse button must stay pressed on the PC.
static void release_all(void)
{
    hid_release_all();
    hid_mouse_buttons(0);
}

// Current layout and OS, to sync the page's selectors
static void on_auth(httpd_req_t *req)
{
    static const char *const os_names[] = {"windows", "linux", "mac"};
    char st[48];
    snprintf(st, sizeof(st), "L%s %s", hid_get_layout()->name, os_names[hid_get_os()]);
    websrv_reply(req, st);
}

static void on_message(httpd_req_t *req, char *msg, size_t len)
{
    led_flash(ACCENT, 15);
    int skipped = 0;
    hid_result_t r = command(msg, &skipped);
    // "hostok" only when the PC comes back, not on every command: it clears
    // the "PC not ready" warning from the page, which previously stayed forever.
    static bool host_bad;
    if (r == HID_NO_HOST) websrv_reply(req, "nohost");
    else if (r == HID_OK && host_bad) websrv_reply(req, "hostok");
    host_bad = r == HID_NO_HOST;
    if (skipped) {
        char m[24];
        snprintf(m, sizeof(m), "untypeable %d", skipped);
        websrv_reply(req, m);
    }
}

esp_err_t web_start(const char *pin)
{
    hid_set_poll(keep_typing, NULL);
    websrv_config_t cfg = {
        .pin = pin,
        .stack_size = 6144, // hid_type uses key arrays on the stack
        .max_frame = MAX_FRAME,
        .on_message = on_message,
        .on_auth = on_auth,
        .on_session_end = release_all,
    };
    esp_err_t err = websrv_start(&cfg);
    if (err == ESP_OK) {
        err = websrv_add_page("/", "text/html", index_html_gz_start, index_html_gz_end - index_html_gz_start);
    }
    if (err == ESP_OK) err = webtheme_register();
    if (err != ESP_OK) web_stop();
    return err;
}

void web_stop(void)
{
    websrv_stop();
    hid_set_poll(NULL, NULL);
}
