#pragma once

#include <stdint.h>
#include "gfx.h"
#include "net.h"

// Schermate delle app comandate da una pagina web.

// In attesa della rete: icona dell'app e "Looking for a known network".
void webui_draw_connecting(const gfx_icon_t *icon, uint32_t accent);

// QR code a sinistra, rete, indirizzo e PIN a destra. page 0: QR della
// pagina (STA e hotspot); page 1 (solo hotspot): QR delle credenziali WiFi.
// client: IP del telefono collegato, "" se nessuno.
void webui_draw_main(const net_info_t *ni, const char *pin, int page, const char *client, uint32_t accent);
