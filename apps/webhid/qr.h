#pragma once

// Disegna un QR code nel framebuffer (da chiamare tra ui_begin e ui_end),
// con il suo bordo chiaro, nel quadrato di lato `box` px con angolo (x, y).
// Restituisce false se il testo non entra in `box` o la codifica fallisce.
bool qr_draw(const char *text, int x, int y, int box);
