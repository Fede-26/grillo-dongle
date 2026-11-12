#pragma once

#include <stdbool.h>
#include <stddef.h>

// QR code on the display, for the apps reached from the phone (Web HID, and
// in the future the web file browser and the web injector).

// Draws a QR code in the framebuffer (call between ui_begin and ui_end),
// with its light border, in the square of side `box` px with corner (x, y).
// Returns false if the text does not fit in `box` or the encoding fails.
bool qr_draw(const char *text, int x, int y, int box);

// Text of the QR that joins a phone to a WPA network ("WIFI:T:WPA;S:..;P:..;;"),
// with the format's special characters escaped.
void qr_wifi_text(char *out, size_t len, const char *ssid, const char *pass);
