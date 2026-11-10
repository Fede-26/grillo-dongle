#pragma once

#include <stdbool.h>
#include "app.h"
#include "payload.h"
#include "ui.h"

// Esecuzione di uno script dell'injector, comune alle app che la usano
// (Injector dal display, Web Injector dal telefono).
//
// Un "engine" sa eseguire un tipo di file (oggi solo Lua; DuckyScript in
// futuro). inj_run_payload prepara un inj_run_t, sceglie l'engine con
// can_run() e chiama run(): lo script gira nel task dell'app, quindi i
// dialoghi (ui.menu, ui.confirm) possono usare direttamente la coda eventi.

typedef struct inj_run inj_run_t;

// Agganci facoltativi del front-end: il Web Injector li usa per rispecchiare
// sul telefono ciò che lo script mostra sul display. Tutti nel task dell'app;
// ogni campo può essere NULL.
typedef struct {
    void *arg;
    // Stato, avanzamento o attesa dell'host cambiati
    void (*state)(void *arg, const inj_run_t *r);
    // Riga di sys.log
    void (*log)(void *arg, const char *text);
    // Dialogo aperto sul display (il front-end può rispondere con
    // APP_EV_ANSWER); chiuso da dialog_end, chiunque abbia risposto, con la
    // risposta come testo (voce del menu, "Yes"/"No", gesto del pulsante) o
    // NULL se non ce n'è una (abort, chiusura dell'esito).
    void (*menu)(void *arg, const char *title, const char *const *items, int n);
    void (*confirm)(void *arg, const char *title, const char *text, bool yes);
    void (*wait_button)(void *arg);
    // Esito, mostrato poi sul display fino a un click o a un APP_EV_ANSWER
    void (*done)(void *arg, const inj_run_t *r);
    void (*dialog_end)(void *arg, const char *answer);
} inj_hooks_t;

struct inj_run {
    app_ctx_t *ctx;
    const payload_meta_t *meta;
    const inj_hooks_t *hooks;

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
};

typedef struct {
    const char *name;
    bool (*can_run)(const char *path);
    void (*run)(inj_run_t *r);
} inj_engine_t;

extern const inj_engine_t engine_lua;

// Esegue lo script e mostra l'esito sul display. hooks può essere NULL.
void inj_run_payload(app_ctx_t *ctx, const payload_meta_t *m, const inj_hooks_t *hooks);

// --- Per gli engine ---

// Ridisegna la schermata di esecuzione.
void inj_redraw(inj_run_t *r);
// Svuota la coda eventi dell'app (contando click, eject...) e aggiorna lo
// schermo se serve. false = lo script deve fermarsi (hold 3 s o timeout).
bool inj_pump(inj_run_t *r);
// Chiama un aggancio del front-end, se c'è.
#define INJ_HOOK(r, fn, ...)                                                          \
    do {                                                                              \
        if ((r)->hooks && (r)->hooks->fn) (r)->hooks->fn((r)->hooks->arg, ##__VA_ARGS__); \
    } while (0)
