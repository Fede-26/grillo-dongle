#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Configuration from /sd/config.lua.
//
// The file is a Lua script that returns a table. At boot it is run in a
// sandboxed VM; the table is "flattened" into key/value pairs
// (e.g. "keyboard.layout" = "it", "wifi.1.ssid" = "Office") and the VM is
// closed right away to free RAM. Apps read the values with the functions
// below, each with a default.

#define CONFIG_PATH "/sd/config.lua"

// Loads (or reloads) the file. On failure the config stays empty (defaults apply)
// and config_error() describes the problem with file and line.
esp_err_t config_load(const char *path);
const char *config_error(void);

const char *config_str(const char *key, const char *def);
int config_int(const char *key, int def);
bool config_bool(const char *key, bool def);
// Number of elements of an array: config_count("wifi") with wifi = {{...},{...}} -> 2
int config_count(const char *prefix);
