#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Gestore WiFi condiviso, con lease a conteggio di riferimenti.
//
// Il WiFi è spento finché nessuno lo chiede. net_acquire() lo accende (o
// riusa la connessione già attiva), net_release() lo spegne all'ultimo
// rilascio. Ogni lease appartiene al task che lo prende: l'app manager, a
// ogni uscita da un'app, chiama net_release_task() sul task dell'app, così
// un'app che dimentica il rilascio non lascia la radio accesa e i lease di
// altri task restano validi. Lease e rilascio vanno quindi fatti dallo
// stesso task.
//
// Le reti note vengono da config.lua (`wifi = { {ssid=, pass=, trusted=}, ... }`),
// l'hotspot da `hotspot = { ssid=, pass=, trusted= }`. Le reti senza password (aperte)
// non sono supportate: le voci senza `pass` vengono ignorate.

typedef enum {
    NET_AUTO, // rete nota (la più forte o scelta dall'utente), in mancanza hotspot
    NET_STA,  // solo rete nota: errore se nessuna è raggiungibile
    NET_AP,   // solo hotspot
    NET_SCAN, // radio STA accesa senza connettersi: serve solo a net_scan()
} net_mode_t;

typedef enum {
    NET_EV_CONNECTING, // tentativo di connessione in corso
    NET_EV_UP,         // IP ottenuto (STA) o hotspot avviato (AP)
    NET_EV_DOWN,       // connessione persa o WiFi spento
    NET_EV_CLIENT,     // AP: un client si è collegato o scollegato
} net_event_t;

typedef struct {
    bool up;
    net_mode_t mode;   // NET_STA o NET_AP quando up (mai NET_AUTO né NET_SCAN)
    char ssid[33];
    char ip[16];
    int8_t rssi;       // solo STA
    int clients;       // solo AP
    // Rete marcata `trusted = true` in config.lua (hotspot: `hotspot.trusted`):
    // le app web non chiedono il PIN. Vale per tutta la durata della connessione.
    bool trusted;
} net_info_t;

// Chiamata dal task di rete: deve solo accodare un evento, non bloccare.
typedef void (*net_event_cb_t)(net_event_t ev);
void net_set_event_cb(net_event_cb_t cb);

// Prende un lease. Blocca fino a `timeout_ms` per la connessione STA;
// con NET_AUTO, scaduto il tempo ripiega sull'hotspot. Se il WiFi è già
// acceso in una modalità incompatibile con quella richiesta restituisce
// ESP_ERR_INVALID_STATE (non si cambia modalità sotto un altro utente).
esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms);

// Rete nota visibile, proposta a chi sceglie.
#define NET_MAX_CANDIDATES 8
typedef struct {
    char ssid[33];
    int8_t rssi;
} net_candidate_t;
// Riceve le reti note visibili (almeno due), la più forte per prima, e
// restituisce l'indice scelto o -1 per annullare. Gira nel task che ha
// chiamato net_acquire_ex, quindi può mostrare un dialogo.
typedef int (*net_choose_fn)(const net_candidate_t *c, int n, void *arg);
// Come net_acquire; se più reti note sono visibili decide `choose` (NULL =
// la più forte). Se `abort` (può essere NULL) diventa true durante l'attesa
// della connessione, smette subito. Con la scelta annullata o l'attesa
// interrotta restituisce ESP_ERR_NOT_FINISHED e non ripiega sull'hotspot.
// Vedi app_net_acquire() in app.h.
esp_err_t net_acquire_ex(net_mode_t mode, uint32_t timeout_ms, net_choose_fn choose, void *arg,
                         const volatile bool *abort);
// Rilascia un lease del task chiamante.
void net_release(void);
// Rilascia tutti i lease di `task` (che di solito è terminato).
void net_release_task(TaskHandle_t task);

void net_get_info(net_info_t *info);
// Interfaccia STA, per leggere gateway, maschera e DNS o inviare pacchetti.
esp_netif_t *net_sta_netif(void);

// Una rete vista da net_scan().
typedef struct {
    char ssid[33]; // vuoto = rete nascosta
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth;  // vedi net_auth_name()
} net_ap_t;

// Scansione bloccante (circa 2 s) delle reti visibili, la più forte per prima.
// Richiede un lease NET_SCAN o NET_STA. Restituisce il numero di reti scritte in
// `out` (al massimo `max`), o un numero negativo (esp_err_t cambiato di segno).
int net_scan(net_ap_t *out, int max);
const char *net_auth_name(uint8_t auth);
// Nome dell'hotspot (config o "Grillo-XXXX") e relativa password, per il QR.
const char *net_ap_ssid(void);
const char *net_ap_pass(void);
