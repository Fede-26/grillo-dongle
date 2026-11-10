#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "lauxlib.h"
#include "luart.h"

static const char *TAG = "config";

typedef struct {
    char *key;
    char *val;
} kv_t;

static kv_t *s_kv;
static int s_n, s_cap;
static char s_error[160];

static void put(const char *key, const char *val)
{
    if (s_n == s_cap) {
        int cap = s_cap ? s_cap * 2 : 32;
        kv_t *n = realloc(s_kv, cap * sizeof(kv_t));
        if (!n) return;
        s_kv = n;
        s_cap = cap;
    }
    char *k = strdup(key), *v = strdup(val);
    // Senza memoria la chiave si perde (vale il default), ma config_str non
    // deve mai trovare un NULL da confrontare.
    if (!k || !v) {
        free(k);
        free(v);
        return;
    }
    s_kv[s_n].key = k;
    s_kv[s_n].val = v;
    s_n++;
}

// Visita ricorsiva della tabella in cima allo stack, prefissando le chiavi.
static void flatten(lua_State *L, const char *prefix, int depth)
{
    if (depth > 8) return; // difesa da tabelle cicliche
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        char key[96];
        if (lua_type(L, -2) == LUA_TNUMBER) {
            snprintf(key, sizeof(key), "%s%s%lld", prefix, *prefix ? "." : "", (long long)lua_tointeger(L, -2));
        } else if (lua_type(L, -2) == LUA_TSTRING) {
            snprintf(key, sizeof(key), "%s%s%s", prefix, *prefix ? "." : "", lua_tostring(L, -2));
        } else {
            lua_pop(L, 1);
            continue;
        }
        switch (lua_type(L, -1)) {
        case LUA_TTABLE: flatten(L, key, depth + 1); break;
        case LUA_TBOOLEAN: put(key, lua_toboolean(L, -1) ? "true" : "false"); break;
        case LUA_TNUMBER:
        case LUA_TSTRING: {
            // lua_tostring converte il numero sul posto: copiamo prima il
            // valore per non alterare la chiave usata da lua_next.
            lua_pushvalue(L, -1);
            put(key, lua_tostring(L, -1));
            lua_pop(L, 1);
            break;
        }
        default: break;
        }
        lua_pop(L, 1);
    }
}

static void clear(void)
{
    for (int i = 0; i < s_n; i++) {
        free(s_kv[i].key);
        free(s_kv[i].val);
    }
    s_n = 0;
}

esp_err_t config_load(const char *path)
{
    // Ricaricabile: ogni chiamata sostituisce la configurazione precedente
    clear();
    s_error[0] = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGW(TAG, "%s assente: uso i default", path);
        return ESP_ERR_NOT_FOUND;
    }
    fclose(f);

    lua_State *L = luart_new(&(luart_opts_t){.mem_limit = 64 * 1024});
    if (!L) return ESP_ERR_NO_MEM;
    esp_err_t ret = ESP_OK;
    if (!luart_dofile(L, path, 1, s_error, sizeof(s_error))) {
        ret = ESP_FAIL;
    } else if (!lua_istable(L, -1)) {
        snprintf(s_error, sizeof(s_error), "config.lua: must return a table");
        ret = ESP_FAIL;
    } else {
        flatten(L, "", 0);
        ESP_LOGI(TAG, "%d chiavi caricate", s_n);
    }
    if (ret != ESP_OK) ESP_LOGE(TAG, "%s", s_error);
    luart_close(L);
    return ret;
}

const char *config_error(void)
{
    return s_error[0] ? s_error : NULL;
}

const char *config_str(const char *key, const char *def)
{
    for (int i = 0; i < s_n; i++) {
        if (strcmp(s_kv[i].key, key) == 0) return s_kv[i].val;
    }
    return def;
}

int config_int(const char *key, int def)
{
    const char *v = config_str(key, NULL);
    if (!v) return def;
    char *end;
    long n = strtol(v, &end, 0);
    return end != v ? (int)n : def;
}

bool config_bool(const char *key, bool def)
{
    const char *v = config_str(key, NULL);
    if (!v) return def;
    return strcmp(v, "true") == 0 || strcmp(v, "1") == 0 || strcmp(v, "yes") == 0;
}

int config_count(const char *prefix)
{
    int n = 0;
    char probe[96];
    for (;;) {
        snprintf(probe, sizeof(probe), "%s.%d.", prefix, n + 1);
        size_t len = strlen(probe);
        bool found = false;
        for (int i = 0; i < s_n && !found; i++) {
            found = strncmp(s_kv[i].key, probe, len) == 0;
        }
        // Accetta anche array di scalari: prefix.N senza sottochiavi
        if (!found) {
            probe[len - 1] = 0;
            found = config_str(probe, NULL) != NULL;
        }
        if (!found) return n;
        n++;
    }
}
