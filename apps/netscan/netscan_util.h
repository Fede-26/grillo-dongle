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

// --- DHCP ---

// Un'offerta DHCP ricevuta. Indirizzi in ordine di rete (come sul filo).
typedef struct {
    uint8_t server[4];  // server identifier (opzione 54), se manca l'IP sorgente
    uint8_t offered[4]; // yiaddr
    uint8_t mask[4], router[4], dns[4]; // zero se l'opzione manca
    uint32_t lease_s;   // 0 se l'opzione manca
} ns_dhcp_offer_t;

// DHCPDISCOVER con il flag broadcast (le OFFER arrivano a 255.255.255.255,
// quindi le vede anche un socket raw senza IP assegnato dal server), già
// dentro un header UDP 68 -> 67 con checksum per `src` -> 255.255.255.255.
// Da inviare su un socket raw IPPROTO_UDP. Restituisce la lunghezza (0 se `n`
// non basta).
int ns_dhcp_discover(uint8_t *buf, size_t n, const uint8_t mac[6], uint32_t xid, const uint8_t src[4]);
// Interpreta un pacchetto IPv4 intero (come lo restituisce un socket raw): true
// se è una DHCPOFFER 67 -> 68 con questo `xid`.
bool ns_dhcp_parse_offer(const uint8_t *pkt, int len, uint32_t xid, ns_dhcp_offer_t *out);

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
