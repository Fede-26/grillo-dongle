#pragma once

#include <stdbool.h>
#include <time.h>

// Data e ora del dongle. Non c'è una batteria per l'orologio: all'accensione
// l'ora è ignota finché non la imposta SNTP (rete STA attiva) o uno script
// (host.clock(), che la legge dal PC). Resta valida ai riavvii software e si
// perde togliendo l'alimentazione.

// Fuso orario POSIX di Europe/Rome, con il cambio dell'ora legale l'ultima
// domenica di marzo e di ottobre.
#define CLOCK_DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

// Fuso usato da localtime (log, sys.date, nomi dei file). Stringa POSIX.
void clock_set_timezone(const char *tz);
// true se l'ora è stata impostata da quando il dongle è acceso.
bool clock_valid(void);
// Imposta l'ora (secondi UTC dal 1970). false se il valore è precedente al
// 2023: è un errore di chi lo manda, non un'ora vera.
bool clock_set(time_t utc);

// SNTP in polling verso `server` (copiato). Chiamate dal gestore della rete
// quando la STA ottiene l'indirizzo e quando il WiFi si spegne.
void clock_sntp_start(const char *server);
void clock_sntp_stop(void);
