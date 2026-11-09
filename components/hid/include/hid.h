#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "hid_layout.h"

// Motore di digitazione HID sopra usbdev: layout, tempi, Caps Lock
// dell'host, ripieghi per i caratteri mancanti, mouse.

typedef struct {
    uint16_t key_delay;  // ms tra un tasto e il successivo
    uint16_t hold;       // ms di pressione di ogni tasto
    uint16_t jitter;     // ± ms casuali sulla pausa
    uint16_t line_delay; // ms in più dopo ogni "\n" di hid_type
} hid_timing_t;

typedef enum {
    HID_OK,
    HID_ABORTED,    // la funzione di poll ha chiesto di interrompere
    HID_NO_HOST,    // host non collegato o endpoint bloccato
    HID_UNTYPEABLE, // carattere non digitabile con layout/OS correnti
} hid_result_t;

// Chiamata tra un tasto e l'altro e durante le pause: false = interrompi.
// Serve all'injector per l'abort e per aggiornare lo schermo.
typedef bool (*hid_poll_fn)(void *arg);

void hid_reset(void); // default: layout us, os windows, tempi 8/5/0/0
void hid_set_layout(const hid_layout_t *l);
const hid_layout_t *hid_get_layout(void);
void hid_set_os(hid_os_t os);
hid_os_t hid_get_os(void);
hid_timing_t *hid_timing(void);
void hid_set_poll(hid_poll_fn fn, void *arg);

// Pausa interrompibile
hid_result_t hid_sleep(uint32_t ms);
// Attende che l'host abbia configurato la tastiera
hid_result_t hid_wait_host(uint32_t timeout_ms);

// Digita una stringa UTF-8. In caso di HID_UNTYPEABLE, *bad_cp è il carattere.
hid_result_t hid_type(const char *utf8, uint32_t *bad_cp);
// Premi insieme modificatori e tasti, poi rilascia tutto (es. Ctrl+Alt+Canc).
hid_result_t hid_chord(uint8_t mods, const uint8_t *keys, int nkeys);
// Tiene premuto / rilascia (per combinazioni lunghe o drag)
hid_result_t hid_down(uint8_t mods, uint8_t key);
hid_result_t hid_up(uint8_t mods, uint8_t key);
hid_result_t hid_release_all(void);
// Converte un singolo carattere nel tasto del layout corrente (per chord)
bool hid_char_key(uint32_t cp, uint8_t *key, uint8_t *mods);

// Mouse (movimenti grandi spezzati in passi da ±127)
#define HID_BTN_LEFT   0x01
#define HID_BTN_RIGHT  0x02
#define HID_BTN_MIDDLE 0x04
hid_result_t hid_mouse_move(int dx, int dy);
hid_result_t hid_mouse_buttons(uint8_t buttons);
hid_result_t hid_mouse_scroll(int wheel, int pan);
hid_result_t hid_mouse_click(uint8_t button);
