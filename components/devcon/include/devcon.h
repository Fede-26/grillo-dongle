#pragma once

// Console di debug a riga di comando.
//
// Segue il trasporto USB disponibile: USB-Serial-JTAG quando TinyUSB è
// spento (profilo NONE), CDC-ACM di debug quando è attivo un profilo
// HID/MSC. Porta anche i log di ESP-IDF sullo stesso trasporto.
//
// Protocollo: una riga = un comando ("launch usbdisk"). L'output del
// comando termina sempre con la riga ">>> rc=N" (N = 0 se ok), così uno
// script sul PC sa quando ha finito (vedi tools/grillo.py).

typedef int (*devcon_cmd_fn)(int argc, char **argv);

void devcon_init(void);
// Le app e i componenti possono aggiungere comandi propri.
void devcon_register(const char *name, const char *usage, devcon_cmd_fn fn);
int devcon_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void devcon_write(const char *buf, int len);
