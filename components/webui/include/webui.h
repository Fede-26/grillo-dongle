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

// Quale QR mostrare. In hotspot si parte dalle credenziali WiFi (il QR
// dell'indirizzo non serve finché il telefono non è sulla rete del dongle) e,
// appena un dispositivo si unisce all'hotspot, si passa da soli
// all'indirizzo: è il passo successivo. Un click alterna le due pagine. Su
// una rete nota c'è solo l'indirizzo.
typedef struct {
    int page;    // -1 finché la rete non è su
    int clients; // dispositivi sull'hotspot all'ultimo controllo
} webui_qr_t;
#define WEBUI_QR_INIT {-1, 0}
// A ogni giro del ciclo dell'app, con lo stato attuale della rete.
void webui_qr_update(webui_qr_t *q, const net_info_t *ni);
// Click del pulsante.
void webui_qr_toggle(webui_qr_t *q, const net_info_t *ni);
