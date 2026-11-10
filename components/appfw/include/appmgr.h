#pragma once

#include "app.h"
#include "gesture.h"

// App manager: esegue un'app alla volta, il launcher quando nessun'altra
// è in primo piano.

// Avvia il manager (non ritorna mai). `apps` non include il launcher.
void appmgr_run(const app_desc_t *launcher, const app_desc_t *const *apps, size_t count);

size_t appmgr_count(void);
const app_desc_t *appmgr_get(size_t index);
const app_desc_t *appmgr_find(const char *id);

// Richiede l'avvio di `app` appena l'app corrente termina (tipicamente
// chiamata dal launcher subito prima di ritornare da run()).
void appmgr_launch(const app_desc_t *app, const char *arg);

// Motivo per cui l'app non può partire ora (es. "no SD card"), NULL se ok.
const char *appmgr_unavailable(const app_desc_t *app);

// Funzione di pulizia chiamata a ogni uscita da un'app. I servizi (rete,
// ecc.) la registrano per rilasciare ciò che l'app ha lasciato acceso.
void appmgr_add_cleanup(void (*fn)(void));
// true (una volta) se un'app o la console hanno chiamato app_config_changed().
bool appmgr_take_config_changed(void);

// --- Usate dalla console di debug ---

// Id dell'app in primo piano ("launcher" se nessuna), NULL durante i cambi.
const char *appmgr_current_id(void);
// Come tenere premuto 3 s (vale anche per il launcher, che allora ritorna).
void appmgr_request_exit(void);
// Simula un gesto del pulsante.
void appmgr_inject(gesture_t g, uint8_t progress);
