#pragma once

#include <stdbool.h>
#include <stdint.h>

// Keyboard layout tables and character -> keys translation.
// Pure logic, no ESP-IDF dependencies: testable on the PC (test/host).

typedef enum { HID_OS_WINDOWS, HID_OS_LINUX, HID_OS_MAC } hid_os_t;

#define HID_MOD_LCTRL  0x01
#define HID_MOD_LSHIFT 0x02
#define HID_MOD_LALT   0x04
#define HID_MOD_LGUI   0x08
#define HID_MOD_RCTRL  0x10
#define HID_MOD_RSHIFT 0x20
#define HID_MOD_RALT   0x40
#define HID_MOD_RGUI   0x80

typedef struct {
    uint16_t cp;  // Unicode codepoint
    uint8_t key;  // HID usage
    uint8_t mods;
} hid_map_t;

// Character composed of a dead key followed by a letter (e.g. à =
// AltGr+r, then a on Linux Colemak). First stroke: dead key, second: letter.
typedef struct {
    uint16_t cp;
    uint8_t key1, mods1;
    uint8_t key2, mods2;
} hid_dead_t;

typedef struct {
    const char *name;
    const hid_map_t *map; // sorted by cp
    int count;
    const hid_map_t *linux_extra; // extra characters on Linux, terminated by cp 0
    const hid_dead_t *linux_dead; // dead key sequences on Linux, terminated by cp 0
    const hid_map_t *mac_extra;   // Option levels on macOS, terminated by cp 0
    const hid_dead_t *mac_dead;   // dead key sequences on macOS, terminated by cp 0
    const hid_map_t *win_extra;   // AltGr levels on Windows, terminated by cp 0
    const hid_dead_t *win_dead;   // dead key sequences on Windows, terminated by cp 0
} hid_layout_t;

extern const hid_layout_t hid_layouts[];
extern const int hid_layout_count;

typedef struct {
    uint8_t key, mods;
} hid_stroke_t;

const hid_layout_t *hid_layout_find(const char *name);
bool hid_os_parse(const char *name, hid_os_t *os);

// Translates a codepoint into a key sequence for layout and OS. If the
// layout lacks the character it uses the OS fallback: Alt + numeric keypad
// on Windows (codepage 1252), Ctrl+Shift+U + hex on Linux.
// `numpad` is true if the sequence requires NumLock on.
// Returns the number of keys (0 = character not typeable).
int hid_translate(const hid_layout_t *l, hid_os_t os, uint32_t cp, hid_stroke_t *out, int max, bool *numpad);

// Names of special keys ("ENTER", "F5", "CTRL"...). For modifiers it
// returns key = 0 and mods set.
bool hid_key_by_name(const char *name, uint8_t *key, uint8_t *mods);
