#pragma once

#include <stdbool.h>
#include <time.h>

// Dongle date and time. There is no clock battery: at power-on the time is
// unknown until SNTP sets it (STA network active) or a script does
// (host.clock(), which reads it from the PC). It survives software restarts
// and is lost when power is removed.

// POSIX time zone for Europe/Rome, with the daylight saving change on the last
// Sunday of March and of October.
#define CLOCK_DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// Time zone used by localtime (logs, sys.date, file names). POSIX string.
void clock_set_timezone(const char *tz);
// true if the time has been set since the dongle was powered on.
bool clock_valid(void);
// Sets the time (UTC seconds since 1970). false if the value is earlier than
// 2023: it is a mistake by the sender, not a real time.
bool clock_set(time_t utc);

// SNTP in polling mode against `server` (copied). Called by the network manager
// when the STA gets its address and when WiFi is turned off.
void clock_sntp_start(const char *server);
void clock_sntp_stop(void);
