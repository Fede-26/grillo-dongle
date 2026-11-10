#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "netscan_util.h"

// Rete locale: sweep ARP, nomi degli host e sonde di raggiungibilità.
// Tutto richiede un lease NET_STA già ottenuto.

#define LAN_MAX_HOSTS 254

#define LAN_SELF 1 // il dongle stesso
#define LAN_GW   2 // gateway della rete

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    uint8_t flags;
    char name[24];
    char vendor[20];
} lan_host_t;

// Chiamata a ogni passo: false interrompe. `phase` 0 = ARP, 1 = nomi.
typedef bool (*lan_progress_fn)(int phase, int done, int total, int found, void *arg);

// Trova gli host della /24 (o della rete più piccola) del dongle e ne cerca
// nome e vendor. Riempie `out` in ordine di IP e restituisce quanti ne ha
// trovati, o -1 se la rete non ha un IP o la scansione è stata interrotta.
int lan_sweep(lan_host_t *out, int max, lan_progress_fn cb, void *arg);

// Sonde per la pagina "Network info". Tutte restituiscono i millisecondi
// impiegati, o -1 se falliscono.
int probe_ping(uint32_t ip_be, int timeout_ms);
int probe_dns(const char *host, uint32_t *ip_be);
// GET /generate_204: in `code` il codice HTTP (204 = internet libero, 200 o 30x = captive portal)
int probe_http(uint32_t ip_be, const char *host, const char *path, int *code);

// Server DHCP che hanno risposto a un DHCPDISCOVER del dongle.
typedef struct {
    ns_dhcp_offer_t offer;
    uint8_t mac[6]; // dalla tabella ARP; tutto zero se il server non ha risposto all'ARP
    char vendor[20];
} lan_dhcp_server_t;

// Chiamata ogni 100 ms circa: false interrompe.
typedef bool (*lan_dhcp_progress_fn)(int elapsed_ms, int found, void *arg);

// Invia un DHCPDISCOVER in broadcast e raccoglie le OFFER per `listen_ms`, una
// voce per server. Nessun indirizzo viene chiesto né preso: senza REQUEST
// l'offerta decade da sola. Restituisce i server trovati, -1 se la rete non ha
// un IP, il socket non si apre o la raccolta è stata interrotta.
int lan_dhcp_probe(lan_dhcp_server_t *out, int max, int listen_ms, lan_dhcp_progress_fn cb, void *arg);
