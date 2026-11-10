#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gfx.h"
#include "net.h"
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
    volatile bool exit_lost; // l'EXIT non è entrato nella coda piena: lo consegna app_next_event
} app_ctx_t;

struct app_desc {
    const char *id;                 // identificatore breve, usato in config.lua (autorun)
    const char *name;               // nome mostrato nel launcher
    const gfx_icon_t *icon;         // icona pixel art 12x12 (disegnata 2x)
    uint32_t accent;                // colore d'accento: UI e LED
    uint32_t requires;              // maschera di app_req_t
    usb_profile_t usb;              // profilo USB attivo mentre l'app gira (maschera di USB_F_*)
    uint32_t stack;                 // stack del task dell'app in byte
    void (*run)(app_ctx_t *ctx);
    // Facoltativa: riga informativa sotto il nome nel launcher.
    void (*subtitle)(char *buf, size_t len);
};

// Attende il prossimo evento. false se scade il timeout (portMAX_DELAY per
// attendere per sempre). Un APP_EV_EXIT viene sempre consegnato, anche se
// quando è stato chiesto la coda era piena.
//
// Consegnare l'evento non basta a far uscire un'app: serve che l'app torni
// ad attenderne uno. Se non lo fa (calcolo lungo, chiamata bloccante), dopo
// 3 s il sistema propone il riavvio con un secondo hold (vedi appmgr.c).
bool app_next_event(app_ctx_t *ctx, app_event_t *ev, uint32_t timeout_ms);
bool app_should_exit(app_ctx_t *ctx);
// Accoda un evento (utile da timer o task secondari dell'app). Con
// APP_EV_EXIT imposta anche exit_requested: è il modo di chiedere l'uscita
// dall'interno dell'app (es. un timeout).
void app_post(app_ctx_t *ctx, app_ev_type_t type, int32_t arg);
// Annulla una richiesta di uscita già ricevuta (es. l'hold ha solo
// interrotto uno script, oppure l'utente ha risposto "No" a "Uscire?") e
// scarta gli EXIT ancora in coda, così il dialogo successivo non si chiude
// da solo.
void app_cancel_exit(app_ctx_t *ctx);

// --- Dialoghi bloccanti pronti all'uso ---

// I dialoghi consumano solo i gesti: gli altri eventi che arrivano mentre
// sono aperti (USB, rete, TICK, USER) tornano in coda alla chiusura.
//
// Menu a lista: click = giù, doppio = su, lungo = scegli.
// Restituisce l'indice scelto o -1 se l'app deve uscire.
int app_menu(app_ctx_t *ctx, const char *title, const char *const *items, int count, int selected);
// Come app_menu, con una colonna di testo a destra di ogni riga (rights può
// essere NULL, e ogni elemento può essere NULL).
int app_menu_ex(app_ctx_t *ctx, const char *title, const char *const *items, const char *const *rights, int count,
                int selected);
// Come app_menu_ex; se non c'è alcun gesto entro timeout_ms (0 = mai)
// restituisce `selected`. Il conto alla rovescia compare nel titolo, un
// click o un doppio click lo fermano.
int app_menu_timeout(app_ctx_t *ctx, const char *title, const char *const *items, const char *const *rights, int count,
                     int selected, uint32_t timeout_ms);
// Domanda sì/no: click alterna, lungo conferma. false anche in caso di uscita.
bool app_confirm(app_ctx_t *ctx, const char *title, const char *text, bool default_yes);
// Come app_confirm; se non c'è alcun gesto entro timeout_ms (0 = mai) restituisce
// default_yes. Un click disattiva la scadenza.
bool app_confirm_timeout(app_ctx_t *ctx, const char *title, const char *text, bool default_yes, uint32_t timeout_ms);
// Messaggio: attende click o lungo. Un testo che non entra scorre da solo.
void app_message(app_ctx_t *ctx, const char *title, const char *text, uint32_t color);

// --- USB ---

// Cambia il profilo USB mentre l'app gira (es. per collegare il disco a
// metà script) e passa la SD all'host o la riprende secondo USB_F_DISK_RW/RO.
// All'uscita dall'app il manager torna comunque al profilo NONE. Se il
// profilo nuovo fallisce TinyUSB resta spento e la SD torna al firmware.
esp_err_t app_usb_apply(usb_profile_t profile);

// --- Rete ---

// net_acquire con la scelta della rete affidata all'utente: se più reti
// note sono visibili compare un menu (con RSSI) e, senza risposta entro
// pochi secondi, vale la più forte. Restituisce ESP_ERR_NOT_FINISHED se
// l'utente esce durante la scelta o durante l'attesa della connessione.
esp_err_t app_net_acquire(app_ctx_t *ctx, net_mode_t mode, uint32_t timeout_ms);
