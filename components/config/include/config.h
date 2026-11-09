#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Configurazione da /sd/config.lua.
//
// Il file è uno script Lua che restituisce una tabella. Al boot viene
// eseguito in una VM sandbox; la tabella viene "appiattita" in coppie
// chiave/valore (es. "keyboard.layout" = "it", "wifi.1.ssid" = "Ufficio")
// e la VM viene chiusa subito per liberare RAM. Le app leggono i valori
// con le funzioni qui sotto, ognuna con un default.

#define CONFIG_PATH "/sd/config.lua"

// Carica (o ricarica) il file. Se fallisce, la config resta vuota (valgono i default)
// e config_error() descrive il problema con file e riga.
esp_err_t config_load(const char *path);
const char *config_error(void);

const char *config_str(const char *key, const char *def);
int config_int(const char *key, int def);
bool config_bool(const char *key, bool def);
// Numero di elementi di un array: config_count("wifi") con wifi = {{...},{...}} -> 2
int config_count(const char *prefix);
