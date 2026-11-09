#pragma once

#include <stdbool.h>
#include <stdint.h>

// Tabelle dei layout di tastiera e traduzione carattere -> tasti.
// Logica pura, senza dipendenze da ESP-IDF: testabile sul PC (test/host).

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
    uint16_t cp;  // codepoint Unicode
    uint8_t key;  // usage HID
    uint8_t mods;
} hid_map_t;

// Carattere composto da un tasto morto seguito da una lettera (es. à =
// AltGr+r, poi a sul Colemak di Linux). Primo stroke: tasto morto, secondo: lettera.
typedef struct {
    uint16_t cp;
    uint8_t key1, mods1;
    uint8_t key2, mods2;
} hid_dead_t;

typedef struct {
    const char *name;
    const hid_map_t *map; // ordinata per cp
    int count;
    const hid_map_t *linux_extra; // caratteri in più su Linux, terminata da cp 0
    const hid_dead_t *linux_dead; // sequenze con tasto morto su Linux, terminata da cp 0
    const hid_map_t *mac_extra;   // livelli Option su macOS, terminata da cp 0
    const hid_dead_t *mac_dead;   // sequenze con tasto morto su macOS, terminata da cp 0
    const hid_map_t *win_extra;   // livelli AltGr su Windows, terminata da cp 0
    const hid_dead_t *win_dead;   // sequenze con tasto morto su Windows, terminata da cp 0
} hid_layout_t;

extern const hid_layout_t hid_layouts[];
extern const int hid_layout_count;

typedef struct {
    uint8_t key, mods;
} hid_stroke_t;

const hid_layout_t *hid_layout_find(const char *name);
bool hid_os_parse(const char *name, hid_os_t *os);

// Traduce un codepoint in una sequenza di tasti per layout e OS. Se il
// layout non ha il carattere usa il ripiego dell'OS: Alt + tastierino
// numerico su Windows (codepage 1252), Ctrl+Shift+U + esadecimale su Linux.
// `numpad` vale true se la sequenza richiede NumLock acceso.
// Restituisce il numero di tasti (0 = carattere non digitabile).
int hid_translate(const hid_layout_t *l, hid_os_t os, uint32_t cp, hid_stroke_t *out, int max, bool *numpad);

// Nomi dei tasti speciali ("ENTER", "F5", "CTRL"...). Per i modificatori
// restituisce key = 0 e mods valorizzato.
bool hid_key_by_name(const char *name, uint8_t *key, uint8_t *mods);
