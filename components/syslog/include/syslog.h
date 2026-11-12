#pragma once

// Copies warning and error level logs to SD (/sd/logs/grillo.log), plus
// the info lines with tag "script" (sys.log of the injector scripts), in
// addition to the normal console. Useful when USB is taken by HID/MSC and
// the serial port is not available. Simple rotation: above 256 KB the file
// becomes grillo.1.log. When the time is known (clock component) every line
// starts with the local date and time.
void syslog_init(void);

// Writes the queued lines to SD immediately (e.g. before lending the SD to the host).
void syslog_flush(void);
