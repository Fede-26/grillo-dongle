#include "hid_layout.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const hid_layout_t *hid_layout_find(const char *name)
{
    for (int i = 0; i < hid_layout_count; i++) {
        if (strcasecmp(hid_layouts[i].name, name) == 0) return &hid_layouts[i];
    }
    return NULL;
}

bool hid_os_parse(const char *name, hid_os_t *os)
{
    if (!strcasecmp(name, "windows") || !strcasecmp(name, "win")) *os = HID_OS_WINDOWS;
    else if (!strcasecmp(name, "linux")) *os = HID_OS_LINUX;
    else if (!strcasecmp(name, "mac") || !strcasecmp(name, "macos")) *os = HID_OS_MAC;
    else return false;
    return true;
}

static const hid_map_t *lookup(const hid_layout_t *l, hid_os_t os, uint32_t cp)
{
    if (os == HID_OS_LINUX && l->linux_extra) {
        for (const hid_map_t *m = l->linux_extra; m->cp; m++) {
            if (m->cp == cp) return m;
        }
    }
    int lo = 0, hi = l->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (l->map[mid].cp == cp) return &l->map[mid];
        if (l->map[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

// Byte del codepage Windows-1252 per un codepoint, 0 se non rappresentabile
static int cp1252(uint32_t cp)
{
    if ((cp >= 0x20 && cp < 0x7F) || (cp >= 0xA0 && cp <= 0xFF)) return cp;
    static const uint16_t hi[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                    0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013,
                                    0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    for (int i = 0; i < 32; i++) {
        if (hi[i] == cp) return 0x80 + i;
    }
    return 0;
}

int hid_translate(const hid_layout_t *l, hid_os_t os, uint32_t cp, hid_stroke_t *out, int max, bool *numpad)
{
    *numpad = false;
    const hid_map_t *m = lookup(l, os, cp);
    if (m) {
        if (max < 1) return 0;
        out[0] = (hid_stroke_t){m->key, m->mods};
        return 1;
    }
    if (os == HID_OS_WINDOWS) {
        // Alt tenuto premuto + "0" + codice decimale sul tastierino:
        // il tasto Alt resta nei modificatori di ogni stroke e viene
        // rilasciato solo alla fine dal motore di digitazione.
        int code = cp1252(cp);
        if (!code || max < 4) return 0;
        char digits[5];
        snprintf(digits, sizeof(digits), "0%03d", code);
        static const uint8_t kp[10] = {0x62, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61};
        for (int i = 0; i < 4; i++) out[i] = (hid_stroke_t){kp[digits[i] - '0'], HID_MOD_LALT};
        *numpad = true;
        return 4;
    }
    if (os == HID_OS_LINUX) {
        // Ctrl+Shift+U, cifre esadecimali, spazio (input method GTK/IBus)
        char hex[8];
        int n = snprintf(hex, sizeof(hex), "%x", (unsigned)cp);
        if (max < n + 2) return 0;
        int k = 0;
        out[k++] = (hid_stroke_t){0x18, HID_MOD_LCTRL | HID_MOD_LSHIFT}; // 'u'
        for (int i = 0; i < n; i++) {
            bool dummy;
            hid_stroke_t s;
            if (hid_translate(l, HID_OS_MAC, (uint8_t)hex[i], &s, 1, &dummy) != 1) return 0;
            out[k++] = s;
        }
        out[k++] = (hid_stroke_t){0x2C, 0};
        return k;
    }
    return 0;
}

typedef struct {
    const char *name;
    uint8_t key, mods;
} named_t;

static const named_t s_names[] = {
    {"CTRL", 0, HID_MOD_LCTRL}, {"CONTROL", 0, HID_MOD_LCTRL}, {"SHIFT", 0, HID_MOD_LSHIFT},
    {"ALT", 0, HID_MOD_LALT}, {"OPTION", 0, HID_MOD_LALT}, {"GUI", 0, HID_MOD_LGUI},
    {"WIN", 0, HID_MOD_LGUI}, {"CMD", 0, HID_MOD_LGUI}, {"COMMAND", 0, HID_MOD_LGUI},
    {"SUPER", 0, HID_MOD_LGUI}, {"RCTRL", 0, HID_MOD_RCTRL}, {"RSHIFT", 0, HID_MOD_RSHIFT},
    {"RALT", 0, HID_MOD_RALT}, {"ALTGR", 0, HID_MOD_RALT}, {"RGUI", 0, HID_MOD_RGUI},
    {"ENTER", 0x28, 0}, {"RETURN", 0x28, 0}, {"ESC", 0x29, 0}, {"ESCAPE", 0x29, 0},
    {"BACKSPACE", 0x2A, 0}, {"TAB", 0x2B, 0}, {"SPACE", 0x2C, 0}, {"CAPSLOCK", 0x39, 0},
    {"PRINTSCREEN", 0x46, 0}, {"SCROLLLOCK", 0x47, 0}, {"PAUSE", 0x48, 0}, {"BREAK", 0x48, 0},
    {"INSERT", 0x49, 0}, {"HOME", 0x4A, 0}, {"PAGEUP", 0x4B, 0}, {"DELETE", 0x4C, 0},
    {"DEL", 0x4C, 0}, {"END", 0x4D, 0}, {"PAGEDOWN", 0x4E, 0}, {"RIGHT", 0x4F, 0},
    {"LEFT", 0x50, 0}, {"DOWN", 0x51, 0}, {"UP", 0x52, 0}, {"NUMLOCK", 0x53, 0},
    {"MENU", 0x65, 0}, {"APP", 0x65, 0},
};

bool hid_key_by_name(const char *name, uint8_t *key, uint8_t *mods)
{
    for (size_t i = 0; i < sizeof(s_names) / sizeof(s_names[0]); i++) {
        if (strcasecmp(s_names[i].name, name) == 0) {
            *key = s_names[i].key;
            *mods = s_names[i].mods;
            return true;
        }
    }
    // F1..F24
    if ((name[0] == 'F' || name[0] == 'f') && isdigit((unsigned char)name[1])) {
        int n = atoi(name + 1);
        if (n >= 1 && n <= 12) *key = 0x3A + n - 1;
        else if (n >= 13 && n <= 24) *key = 0x68 + n - 13;
        else return false;
        *mods = 0;
        return true;
    }
    return false;
}
