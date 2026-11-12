#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Shared WiFi manager, with reference-counted leases.
//
// WiFi is off until someone asks for it. net_acquire() turns it on (or
// reuses the connection already active), net_release() turns it off on the
// last release. Each lease belongs to the task that takes it: every time an
// app exits, the app manager calls net_release_task() on the app's task, so
// an app that forgets to release does not leave the radio on and the leases
// of other tasks stay valid. Lease and release must therefore be done by the
// same task.
//
// Known networks come from config.lua (`wifi = { {ssid=, pass=, trusted=}, ... }`),
// the hotspot from `hotspot = { ssid=, pass=, trusted= }`. Networks without a password
// (open) are not supported: entries without `pass` are ignored.

typedef enum {
    NET_AUTO, // known network (the strongest or the one chosen by the user), otherwise hotspot
    NET_STA,  // known network only: error if none is reachable
    NET_AP,   // hotspot only
    NET_SCAN, // STA radio on without connecting: only used for net_scan()
} net_mode_t;

typedef enum {
    NET_EV_CONNECTING, // connection attempt in progress
    NET_EV_UP,         // IP obtained (STA) or hotspot started (AP)
    NET_EV_DOWN,       // connection lost or WiFi off
    NET_EV_CLIENT,     // AP: a client connected or disconnected
} net_event_t;

typedef struct {
    bool up;
    net_mode_t mode;   // NET_STA or NET_AP when up (never NET_AUTO or NET_SCAN)
    char ssid[33];
    char ip[16];
    int8_t rssi;       // STA only
    int clients;       // AP only
    // Network marked `trusted = true` in config.lua (hotspot: `hotspot.trusted`):
    // the web apps do not ask for the PIN. Holds for the whole connection.
    bool trusted;
} net_info_t;

// Called from the network task: it must only queue an event, not block.
typedef void (*net_event_cb_t)(net_event_t ev);
void net_set_event_cb(net_event_cb_t cb);

// Takes a lease. Blocks up to `timeout_ms` for the STA connection; with
// NET_AUTO, once the time expires it falls back to the hotspot. If WiFi is
// already on in a mode incompatible with the requested one it returns
// ESP_ERR_INVALID_STATE (the mode is not changed under another user).
esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms);

// Visible known network, offered to whoever chooses.
#define NET_MAX_CANDIDATES 8
typedef struct {
    char ssid[33];
    int8_t rssi;
} net_candidate_t;
// Receives the visible known networks (at least two), the strongest first,
// and returns the chosen index or -1 to cancel. Runs in the task that called
// net_acquire_ex, so it can show a dialog.
typedef int (*net_choose_fn)(const net_candidate_t *c, int n, void *arg);
// Like net_acquire; if several known networks are visible `choose` decides
// (NULL = the strongest). If `abort` (may be NULL) becomes true while waiting
// for the connection, it stops immediately. With the choice canceled or the
// wait interrupted it returns ESP_ERR_NOT_FINISHED and does not fall back to
// the hotspot. See app_net_acquire() in app.h.
esp_err_t net_acquire_ex(net_mode_t mode, uint32_t timeout_ms, net_choose_fn choose, void *arg,
                         const volatile bool *abort);
// Releases a lease of the calling task.
void net_release(void);
// Releases all leases of `task` (which has usually terminated).
void net_release_task(TaskHandle_t task);

void net_get_info(net_info_t *info);
// STA interface, to read gateway, netmask and DNS or to send packets.
esp_netif_t *net_sta_netif(void);

// A network seen by net_scan().
typedef struct {
    char ssid[33]; // empty = hidden network
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t auth;  // see net_auth_name()
} net_ap_t;

// Blocking scan (about 2 s) of the visible networks, the strongest first.
// Requires a NET_SCAN or NET_STA lease. Returns the number of networks written to
// `out` (at most `max`), or a negative number (esp_err_t with its sign flipped).
int net_scan(net_ap_t *out, int max);
const char *net_auth_name(uint8_t auth);
// Hotspot name (config or "Grillo-XXXX") and its password, for the QR.
const char *net_ap_ssid(void);
const char *net_ap_pass(void);
