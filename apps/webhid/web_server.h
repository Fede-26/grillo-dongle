#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Comandi del Web HID sopra il server comune (components/websrv).
//
// GET /     pagina di controllo (gzip, incorporata nel firmware)
// GET /ws   WebSocket: dopo l'autenticazione con il PIN, comandi testuali
//           che diventano report HID (vedi docs/reference/protocollo-websocket.md).
//
// Un solo client alla volta può comandare il PC: l'IP è websrv_client_ip().
esp_err_t web_start(const char *pin);
void web_stop(void);
