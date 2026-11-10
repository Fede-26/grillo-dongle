#pragma once

#include <stdbool.h>
#include <stddef.h>

// QR code sul display, per le app che si raggiungono dal telefono (Web HID,
// e in futuro il file browser web e l'injector web).

// Disegna un QR code nel framebuffer (da chiamare tra ui_begin e ui_end),
// con il suo bordo chiaro, nel quadrato di lato `box` px con angolo (x, y).
// Restituisce false se il testo non entra in `box` o la codifica fallisce.
bool qr_draw(const char *text, int x, int y, int box);

// Testo del QR che collega un telefono a una rete WPA ("WIFI:T:WPA;S:..;P:..;;"),
// con l'escape dei caratteri speciali del formato.
void qr_wifi_text(char *out, size_t len, const char *ssid, const char *pass);
