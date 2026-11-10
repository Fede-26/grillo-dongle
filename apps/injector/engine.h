#pragma once

#include <stdbool.h>
#include "app.h"
#include "payload.h"
#include "ui.h"

// Esecuzione di uno script dell'injector.
//
// Un "engine" sa eseguire un tipo di file (oggi solo Lua; DuckyScript in
// futuro). L'injector prepara un inj_run_t, sceglie l'engine con can_run()
// e chiama run(): lo script gira nel task dell'app, quindi i dialoghi
// (ui.menu, ui.confirm) possono usare direttamente la coda eventi.

typedef struct {
    app_ctx_t *ctx;
    const payload_meta_t *meta;

    // Stato mostrato dalla schermata di esecuzione, aggiornato dallo script
    char status[48];
    ui_scroll_t status_scroll; // stato lungo: scorre da solo (vedi inj_redraw)
    int64_t redraw_us;         // prossimo ridisegno periodico da inj_pump
    int progress, progress_max; // barra nascosta se progress_max == 0
    bool waiting_host;
    int64_t started_us;

    // Eventi raccolti da inj_pump() mentre lo script gira
    uint32_t clicks, doubles, longs;
    bool ejected; // l'host ha espulso il disco (profilo HID_DISK)

    // Esito
    bool ok, aborted, timed_out;
    char error[160];
} inj_run_t;

typedef struct {
    const char *name;
    bool (*can_run)(const char *path);
    void (*run)(inj_run_t *r);
} inj_engine_t;

extern const inj_engine_t engine_lua;

// --- Fornite da injector.c ---

// Ridisegna la schermata di esecuzione.
void inj_redraw(inj_run_t *r);
// Svuota la coda eventi dell'app (contando click, eject...) e aggiorna lo
// schermo se serve. false = lo script deve fermarsi (hold 3 s o timeout).
bool inj_pump(inj_run_t *r);
