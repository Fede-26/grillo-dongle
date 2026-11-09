#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Logica pura di Netscan (nessuna dipendenza da ESP-IDF): si prova sul PC
// con test/host.

// --- MAC e vendor ---

// MAC "privato" (locally administered): telefoni e PC con indirizzo casuale,
// quindi il vendor dell'OUI non vuol dire nulla.
bool ns_mac_is_private(const uint8_t mac[6]);
void ns_mac_str(const uint8_t mac[6], char out[18]);

// Cerca il vendor in un file oui.bin (generato da tools/make_oui.py): "OUI1",
// numero di record (u32 little endian), poi record da 32 byte ordinati per
// OUI (3 byte + nome di 29 byte riempito con NUL). false se non c'è.
bool ns_oui_lookup(FILE *f, const uint8_t mac[6], char *out, size_t n);

// --- Nomi host ---

// Richiesta NBNS "node status" (UDP 137) col nome jolly. Restituisce la lunghezza (50).
int ns_nbns_query(uint8_t buf[50]);
// Estrae il nome NetBIOS della macchina dalla risposta. false se non c'è.
bool ns_nbns_parse(const uint8_t *buf, int len, char *name, size_t n);

// Richiesta mDNS "PTR x.x.x.x.in-addr.arpa" da inviare in unicast alla porta
// 5353 dell'host (RFC 6762 §5.5: un responder risponde anche a chi non usa
// la porta 5353). Restituisce la lunghezza.
int ns_mdns_ptr_query(uint8_t *buf, size_t n, const uint8_t ip[4]);
// Estrae il nome (senza ".local") dalla risposta. false se non c'è.
bool ns_mdns_ptr_parse(const uint8_t *buf, int len, char *name, size_t n);

// --- Testo ---

// Copia solo ASCII stampabile (il resto diventa '?', una volta per carattere
// UTF-8): l'SSID e il nome di un host li sceglie chi è sulla rete.
void ns_display_text(char *dst, size_t n, const char *src);
// Campo CSV: virgolette se serve e un apice davanti a =,+,-,@ (Excel li
// eseguirebbe come formule: SSID e nomi arrivano dalla rete).
// Restituisce la lunghezza scritta (sempre terminato con NUL).
size_t ns_csv_field(char *dst, size_t n, const char *src);
// Frammento di nome file: solo [A-Za-z0-9_-], al massimo n-1 caratteri.
void ns_file_slug(char *dst, size_t n, const char *src);

// --- Sweep ---

// Intervallo di host da interrogare (ordine host, estremi inclusi) per
// l'indirizzo `ip` e la maschera `mask`: la rete reale, ma mai più grande
// della /24 che contiene `ip`. false se la rete non ha host utili.
bool ns_sweep_range(uint32_t ip, uint32_t mask, uint32_t *first, uint32_t *last);
