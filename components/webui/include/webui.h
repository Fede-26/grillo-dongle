#pragma once

#include <stddef.h>
#include <stdint.h>
#include "gfx.h"
#include "net.h"

// Screens of the apps driven by a web page.

// Session PIN in `pin` (at least 7 bytes): 6 random digits, or "" (no PIN)
// if the config `pin_key` (e.g. "webhid.pin") is false or if the current
// network is marked trusted in config.lua. Call with the network already up.
void webui_make_pin(const char *pin_key, char *pin, size_t len);

// Waiting for the network: app icon and "Looking for a known network".
void webui_draw_connecting(const gfx_icon_t *icon, uint32_t accent);

// QR code on the left, network, address and PIN on the right. page 0: QR of
// the page (STA and hotspot); page 1 (hotspot only): QR of the WiFi
// credentials. client: IP of the connected phone, "" if none.
void webui_draw_main(const net_info_t *ni, const char *pin, int page, const char *client, uint32_t accent);

// Which QR to show. In hotspot mode it starts from the WiFi credentials (the
// address QR is useless until the phone is on the dongle's network) and, as
// soon as a device joins the hotspot, it switches by itself to the address:
// that is the next step. A click toggles between the two pages. On a known
// network there is only the address.
typedef struct {
    int page;    // -1 until the network is up
    int clients; // devices on the hotspot at the last check
} webui_qr_t;
#define WEBUI_QR_INIT {-1, 0}
// On every iteration of the app loop, with the current network state.
void webui_qr_update(webui_qr_t *q, const net_info_t *ni);
// Button click.
void webui_qr_toggle(webui_qr_t *q, const net_info_t *ni);
