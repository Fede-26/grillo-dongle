#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gfx.h"
#include "usbdev.h"

// Contratto tra il sistema e le app.
//
// Un'app è un descrittore statico (app_desc_t) con una funzione run() che
// gira in un task dedicato. Quando run() ritorna l'app è terminata e il
// sistema rilascia d'ufficio tutte le risorse condivise (profilo USB, SD
// prestata, livello LED dell'app, rete): un'app non deve ricordarsi di
// farlo, anche se può farlo prima per conto suo.
//
// Per aggiungere un'app: creare un componente in apps/, esportare il suo
// app_desc_t e aggiungerlo alla tabella in main/app_list.c.

typedef enum {
    APP_REQ_SD = 1 << 0,   // SD montata (FAT) su /sd
    APP_REQ_CARD = 1 << 1, // basta che la scheda sia presente (anche non FAT)
    APP_REQ_NET = 1 << 2,  // userà la rete (WiFi)
    APP_REQ_BLE = 1 << 3,  // riservato: BLE non ancora implementato
} app_req_t;

typedef enum {
    APP_EV_CLICK,
    APP_EV_DOUBLE,
    APP_EV_LONG,
    APP_EV_EXIT,   // l'utente ha tenuto premuto 3 s: terminare al più presto
    APP_EV_USB,    // arg = usbdev_event_t
    APP_EV_NET,    // arg = evento di rete (vedi net.h)
    APP_EV_TICK,   // generato dall'app stessa con app_post (timer, task)
    APP_EV_USER,   // libero per l'app
} app_ev_type_t;

typedef struct {
    app_ev_type_t type;
    int32_t arg;
} app_event_t;

typedef struct app_desc app_desc_t;

typedef struct {
    const app_desc_t *app;
    const char *arg;       // argomento di avvio (es. payload dell'autorun), può essere NULL
    QueueHandle_t queue;
    volatile bool exit_requested;
} app_ctx_t;

struct app_desc {
    const char *id;                 // identificatore breve, usato in config.lua (autorun)
    const char *name;               // nome mostrato nel launcher
    const gfx_icon_t *icon;         // icona pixel art 12x12 (disegnata 2x)
    uint32_t accent;                // colore d'accento: UI e LED
    uint32_t requires;              // maschera di app_req_t
    usb_profile_t usb;              // profilo USB attivo mentre l'app gira
    uint32_t stack;                 // stack del task dell'app in byte
    void (*run)(app_ctx_t *ctx);
    // Facoltativa: riga informativa sotto il nome nel launcher.
    void (*subtitle)(char *buf, size_t len);
};

// Attende il prossimo evento. false se scade il timeout (portMAX_DELAY per
// attendere per sempre). Un APP_EV_EXIT viene sempre consegnato.
bool app_next_event(app_ctx_t *ctx, app_event_t *ev, uint32_t timeout_ms);
bool app_should_exit(app_ctx_t *ctx);
// Accoda un evento (utile da timer o task secondari dell'app).
void app_post(app_ctx_t *ctx, app_ev_type_t type, int32_t arg);

// --- Dialoghi bloccanti pronti all'uso ---

// Menu a lista: click = giù, doppio = su, lungo = scegli.
// Restituisce l'indice scelto o -1 se l'app deve uscire.
int app_menu(app_ctx_t *ctx, const char *title, const char *const *items, int count, int selected);
// Come app_menu, con una colonna di testo a destra di ogni riga (rights può
// essere NULL, e ogni elemento può essere NULL).
int app_menu_ex(app_ctx_t *ctx, const char *title, const char *const *items, const char *const *rights, int count,
                int selected);
// Domanda sì/no: click alterna, lungo conferma. false anche in caso di uscita.
bool app_confirm(app_ctx_t *ctx, const char *title, const char *text, bool default_yes);
// Messaggio: attende click o lungo.
void app_message(app_ctx_t *ctx, const char *title, const char *text, uint32_t color);
