#pragma once

#include <stdbool.h>
#include <stdint.h>

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
