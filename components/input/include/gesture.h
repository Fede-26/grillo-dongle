#pragma once

#include <stdbool.h>
#include <stdint.h>

// Riconoscitore di gesti per un singolo pulsante. Logica pura, senza
// dipendenze da ESP-IDF, così è testabile sul PC (test/host).

#define GESTURE_LONG_MS        600  // da qui il rilascio è "lungo" (conferma)
// Da qui compare la barra di uscita. Fino a qui il rilascio vale ancora come
// "lungo": 1,5 s lasciano tempo a chi rilascia con calma dopo il segnale.
#define GESTURE_HOLD_START_MS  1500
#define GESTURE_HOLD_MS        3000 // tenuto fin qui = esci dall'app
#define GESTURE_DOUBLE_MS      250  // finestra per il secondo click

typedef enum {
    GESTURE_NONE,
    GESTURE_CLICK,
    GESTURE_DOUBLE,
    GESTURE_LONG,          // rilascio tra LONG_MS e HOLD_START_MS
    GESTURE_LONG_ARMED,    // superata la soglia LONG mentre è ancora premuto (feedback)
    GESTURE_HOLD_PROGRESS, // tra HOLD_START e HOLD: valore 0..100 in *progress
    GESTURE_HOLD_CANCEL,   // rilasciato durante l'anello: nessuna azione
    GESTURE_HOLD_EXIT,
} gesture_t;

typedef struct {
    int state;
    uint32_t t0;
    bool second;
    bool armed;
    uint8_t last_progress;
} gesture_ctx_t;

void gesture_init(gesture_ctx_t *g);
// Da chiamare periodicamente (es. ogni 10 ms) con lo stato già "debounced".
gesture_t gesture_feed(gesture_ctx_t *g, bool pressed, uint32_t now_ms, uint8_t *progress);
