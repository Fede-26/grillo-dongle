#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Server HTTP + WebSocket del Web HID.
//
// GET /     pagina di controllo (gzip, incorporata nel firmware)
// GET /ws   WebSocket: il primo messaggio deve essere "p<PIN>"; poi
//           comandi testuali che il server traduce in report HID.
//
// Un solo client alla volta può comandare il PC; un secondo riceve "busy".
esp_err_t web_start(const char *pin);
void web_stop(void);
// IP del client autenticato, stringa vuota se nessuno.
const char *web_client_ip(void);
