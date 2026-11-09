#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

// Toolkit UI di Grillo: tema, status bar di sistema, primitive di layout.
//
// Lo schermo è diviso in due zone:
//  - status bar (y 0..UI_BAR_H-1) + linea d'accento: di proprietà del sistema;
//  - area contenuto (UI_CONTENT_Y..79): dell'app in primo piano.
// Le notifiche di sistema (toast, barra di "tieni premuto per uscire")
// vivono nella status bar, così non sporcano mai il contenuto dell'app.

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
#define UI_GRILLO   0x7CFC00 // verde del logo

typedef enum {
    UI_IND_SD,   // 0 assente, 1 montata, 2 non FAT, 3 prestata all'host
    UI_IND_USB,  // 0 nessun profilo, 1 HID, 2 MSC, 3 HID + disco
    UI_IND_WIFI, // 0 spento, 1 connessione in corso, 2..5 RSSI a barre, 6 hotspot
    UI_IND_COUNT,
} ui_indicator_t;

void ui_init(void);

// Lock del framebuffer, per chi lo legge dall'esterno (screenshot).
void ui_lock(void);
void ui_unlock(void);

// Disegno dell'area contenuto da parte dell'app:
//   ui_begin();  ...gfx_*...  ui_end();
// ui_begin prende il lock, pulisce il contenuto e imposta il clipping;
// ui_end ridisegna la status bar, invia il frame e rilascia il lock.
void ui_begin(void);
void ui_end(void);

// Come ui_begin/ui_end ma senza pulire né limitare all'area contenuto
// (splash a schermo intero).
void ui_begin_full(void);
void ui_end_full(void);

// Stato mostrato nella status bar (ridisegnata subito).
void ui_set_title(const char *title, uint32_t accent);
uint32_t ui_accent(void);
void ui_set_indicator(ui_indicator_t ind, uint8_t value);
// Avanzamento dell'hold di uscita 0..100; 0 = nascosto.
void ui_set_hold(uint8_t progress);
// Messaggio temporaneo al posto del titolo.
void ui_toast(const char *msg, uint32_t color, uint32_t ms);
// Nasconde la status bar (splash) o la ripristina.
void ui_bar_visible(bool visible);

// --- Elementi di layout riutilizzabili (da chiamare tra begin/end) ---

// Riga di lista alta UI_ROW_H; `selected` la evidenzia con l'accento.
#define UI_ROW_H 11
void ui_draw_row(int y, const char *text, const char *right, bool selected, bool enabled);
// Lista scrollabile: disegna `visible` righe a partire da `top`.
typedef const char *(*ui_label_fn)(int index, void *arg, const char **right, bool *enabled);
void ui_draw_list(int top, int sel, int count, ui_label_fn label, void *arg);
int ui_list_visible_rows(void);
int ui_list_scroll(int top, int sel, int count); // nuovo `top` che mantiene `sel` visibile

void ui_draw_progress(int x, int y, int w, int h, int value, int max, uint32_t color);
// Pallini di pagina centrati
void ui_draw_pager(int y, int count, int current, uint32_t color);
// Testo su più righe con a capo automatico; restituisce le righe usate.
int ui_draw_wrapped(const gfx_font_t *f, int x, int y, int w, int max_lines, const char *text, uint32_t rgb);
// Barra in basso con suggerimenti per i gesti, es. "click next · hold back"
void ui_draw_hint(const char *hint);
