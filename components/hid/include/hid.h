#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hid_layout.h"

// HID typing engine on top of usbdev: layouts, timing, host Caps Lock,
// fallbacks for missing characters, mouse.

typedef struct {
    uint16_t key_delay;  // ms between one key and the next
    uint16_t hold;       // ms each key is held down
    uint16_t jitter;     // ± random ms on the pause
    uint16_t line_delay; // extra ms after every "\n" in hid_type
} hid_timing_t;

typedef enum {
    HID_OK,
    HID_ABORTED,    // the poll function asked to stop
    HID_NO_HOST,    // host not connected or endpoint stalled
    HID_UNTYPEABLE, // character not typeable with current layout/OS
} hid_result_t;

// Called between keys and during pauses: false = stop.
// Used by the injector for abort and to update the screen.
typedef bool (*hid_poll_fn)(void *arg);

void hid_reset(void); // default: layout us, os windows, timing 8/5/0/0
void hid_set_layout(const hid_layout_t *l);
const hid_layout_t *hid_get_layout(void);
void hid_set_os(hid_os_t os);
hid_os_t hid_get_os(void);
hid_timing_t *hid_timing(void);
void hid_set_poll(hid_poll_fn fn, void *arg);

// Interruptible pause
hid_result_t hid_sleep(uint32_t ms);
// Waits until the host has configured the keyboard
hid_result_t hid_wait_host(uint32_t timeout_ms);

// Types a UTF-8 string. On HID_UNTYPEABLE, *bad_cp is the character.
hid_result_t hid_type(const char *utf8, uint32_t *bad_cp);
// Presses modifiers and keys together, then releases everything (e.g. Ctrl+Alt+Del).
hid_result_t hid_chord(uint8_t mods, const uint8_t *keys, int nkeys);
// Holds down / releases (for long combinations or drag)
hid_result_t hid_down(uint8_t mods, uint8_t key);
hid_result_t hid_up(uint8_t mods, uint8_t key);
hid_result_t hid_release_all(void);
// Converts a single character into the key of the current layout (for chord)
bool hid_char_key(uint32_t cp, uint8_t *key, uint8_t *mods);

// Mouse (large movements split into steps of ±127)
#define HID_BTN_LEFT   0x01
#define HID_BTN_RIGHT  0x02
#define HID_BTN_MIDDLE 0x04
hid_result_t hid_mouse_move(int dx, int dy);
hid_result_t hid_mouse_buttons(uint8_t buttons);
hid_result_t hid_mouse_scroll(int wheel, int pan);
hid_result_t hid_mouse_click(uint8_t button);
