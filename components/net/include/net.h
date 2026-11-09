#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// Gestore WiFi condiviso, con lease a conteggio di riferimenti.
//
// Il WiFi è spento finché nessuno lo chiede. net_acquire() lo accende (o
// riusa la connessione già attiva), net_release() lo spegne all'ultimo
// rilascio. L'app manager chiama net_release_all() a ogni uscita da
// un'app: un'app che dimentica il rilascio non lascia la radio accesa.
//
// Le reti note vengono da config.lua (`wifi = { {ssid=, pass=}, ... }`),
// l'hotspot da `hotspot = { ssid=, pass= }`.

typedef enum {
    NET_AUTO, // reti note per RSSI, in mancanza hotspot
    NET_STA,  // solo rete nota: errore se nessuna è raggiungibile
    NET_AP,   // solo hotspot
} net_mode_t;

typedef enum {
    NET_EV_CONNECTING, // tentativo di connessione in corso
    NET_EV_UP,         // IP ottenuto (STA) o hotspot avviato (AP)
    NET_EV_DOWN,       // connessione persa o WiFi spento
    NET_EV_CLIENT,     // AP: un client si è collegato o scollegato
} net_event_t;

typedef struct {
    bool up;
    net_mode_t mode;   // NET_STA o NET_AP quando up (mai NET_AUTO)
    char ssid[33];
    char ip[16];
    int8_t rssi;       // solo STA
    int clients;       // solo AP
} net_info_t;

// Chiamata dal task di rete: deve solo accodare un evento, non bloccare.
typedef void (*net_event_cb_t)(net_event_t ev);
void net_set_event_cb(net_event_cb_t cb);

// Prende un lease. Blocca fino a `timeout_ms` per la connessione STA;
// con NET_AUTO, scaduto il tempo ripiega sull'hotspot. Se il WiFi è già
// acceso in una modalità incompatibile con quella richiesta restituisce
// ESP_ERR_INVALID_STATE (non si cambia modalità sotto un altro utente).
esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms);
void net_release(void);
void net_release_all(void);

void net_get_info(net_info_t *info);
// Nome dell'hotspot (config o "Grillo-XXXX") e relativa password, per il QR.
const char *net_ap_ssid(void);
const char *net_ap_pass(void);
