#pragma once

// Command-line debug console.
//
// Follows the available USB transport: USB-Serial-JTAG when TinyUSB is
// off (profile NONE), debug CDC-ACM when an HID/MSC profile is active.
// Also carries the ESP-IDF logs on the same transport.
//
// Protocol: one line = one command ("launch usbdisk"). The command output
// always ends with the line ">>> rc=N" (N = 0 if ok), so a script on the
// PC knows when it has finished (see tools/grillo.py).

typedef int (*devcon_cmd_fn)(int argc, char **argv);

void devcon_init(void);
// Apps and components can add their own commands.
void devcon_register(const char *name, const char *usage, devcon_cmd_fn fn);
int devcon_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void devcon_write(const char *buf, int len);

// Filter for incoming lines: if it returns true the line is consumed
// and is not interpreted as a command (used by the host.ask channel).
typedef bool (*devcon_line_filter_t)(const char *line);
void devcon_set_line_filter(devcon_line_filter_t filter);
// Writes to the CDC serial regardless of the debug console.
void devcon_cdc_write(const char *buf, int len);
