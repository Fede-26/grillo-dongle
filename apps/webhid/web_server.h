#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Web HID commands on top of the shared server (components/websrv).
//
// GET /     control page (gzip, embedded in the firmware)
// GET /ws   WebSocket: after PIN authentication, text commands
//           that become HID reports (see docs/reference/protocollo-websocket.md).
//
// Only one client at a time can control the PC: its IP is websrv_client_ip().
esp_err_t web_start(const char *pin);
void web_stop(void);
