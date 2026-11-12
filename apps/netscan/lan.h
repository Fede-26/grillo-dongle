#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "netscan_util.h"

// Local network: ARP sweep, host names and reachability probes.
// Everything requires an already obtained NET_STA lease.

#define LAN_MAX_HOSTS 254

#define LAN_SELF 1 // the dongle itself
#define LAN_GW   2 // network gateway

typedef struct {
    uint8_t ip[4];
    uint8_t mac[6];
    uint8_t flags;
    char name[24];
    char vendor[20];
} lan_host_t;

// Called at every step: false aborts. `phase` 0 = ARP, 1 = names.
typedef bool (*lan_progress_fn)(int phase, int done, int total, int found, void *arg);

// Finds the hosts on the dongle's /24 (or smaller network) and looks up
// their name and vendor. Fills `out` in IP order and returns how many it
// found, or -1 if the network has no IP or the scan was aborted.
int lan_sweep(lan_host_t *out, int max, lan_progress_fn cb, void *arg);

// Probes for the "Network info" page. All return the milliseconds
// taken, or -1 on failure.
int probe_ping(uint32_t ip_be, int timeout_ms);
int probe_dns(const char *host, uint32_t *ip_be);
// GET /generate_204: `code` receives the HTTP code (204 = open internet, 200 or 30x = captive portal)
int probe_http(uint32_t ip_be, const char *host, const char *path, int *code);

// DHCP servers that answered a DHCPDISCOVER from the dongle.
typedef struct {
    ns_dhcp_offer_t offer;
    uint8_t mac[6]; // from the ARP table; all zeros if the server did not answer ARP
    char vendor[20];
} lan_dhcp_server_t;

// Called roughly every 100 ms: false aborts.
typedef bool (*lan_dhcp_progress_fn)(int elapsed_ms, int found, void *arg);

// Broadcasts a DHCPDISCOVER and collects OFFERs for `listen_ms`, one
// entry per server. No address is requested or taken: without a REQUEST
// the offer expires on its own. Returns the servers found, -1 if the network has
// no IP, the socket cannot be opened or the collection was aborted.
int lan_dhcp_probe(lan_dhcp_server_t *out, int max, int listen_ms, lan_dhcp_progress_fn cb, void *arg);
