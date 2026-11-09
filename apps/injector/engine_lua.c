// Engine Lua dell'injector: VM sandbox (luart) più i moduli kb, mouse,
// led, ui, sys, fs, cfg e require.

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "config.h"
#include "engine.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hid.h"
#include "lauxlib.h"
#include "led.h"
#include "luart.h"
#include "storage.h"
#include "syslog.h"
#include "ui.h"
#include "usbdev.h"

static const char *TAG = "inj";

#define LUA_MEM_LIMIT  (96 * 1024)
#define FS_READ_MAX    (16 * 1024)
#define HOST_WAIT_MS   (24 * 3600 * 1000) // "per sempre": si esce con hold 3 s

// Uno script alla volta: lo stato dell'esecuzione corrente è statico.
static inj_run_t *s_run;

static bool poll(void *arg)
{
    return inj_pump(s_run);
}

// Converte un risultato HID in errore Lua (non ritorna se != HID_OK).
static void check_hid(lua_State *L, hid_result_t r, uint32_t bad_cp)
{
    switch (r) {
    case HID_OK: return;
    case HID_ABORTED: luaL_error(L, "aborted"); break;
    case HID_NO_HOST: luaL_error(L, "USB host not responding"); break;
    case HID_UNTYPEABLE:
        luaL_error(L, "U+%04X not typeable with layout %s", (unsigned)bad_cp, hid_get_layout()->name);
        break;
    }
}

static int opt_ms(lua_State *L, int idx)
{
    lua_Integer v = luaL_checkinteger(L, idx);
    luaL_argcheck(L, v >= 0 && v <= 60000, idx, "0..60000 ms");
    return (int)v;
}

// ---------------------------------------------------------------------------
// kb

// "${nome}" sostituito con vars[nome] (tabella all'indice `vars`, 0 = usa
// le variabili globali dello script). Una variabile mancante è un errore:
// digitare "${user}" alla lettera non è mai ciò che si voleva.
static const char *expand(lua_State *L, const char *s, int vars)
{
    if (!strstr(s, "${")) return s;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    while (*s) {
        const char *open = strstr(s, "${");
        const char *close = open ? strchr(open, '}') : NULL;
        if (!close) {
            luaL_addstring(&b, s);
            break;
        }
        luaL_addlstring(&b, s, open - s);
        char name[48];
        size_t n = close - open - 2;
        if (n >= sizeof(name)) n = sizeof(name) - 1;
        memcpy(name, open + 2, n);
        name[n] = 0;
        if (vars) lua_getfield(L, vars, name);
        else lua_getglobal(L, name);
        if (lua_isnil(L, -1)) luaL_error(L, "undefined ${%s}", name);
        luaL_tolstring(L, -1, NULL);
        lua_remove(L, -2);
        luaL_addvalue(&b);
        s = close + 1;
    }
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}

static int kb_type(lua_State *L)
{
    const char *s = luaL_checkstring(L, 1);
    int vars = 0;
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TTABLE);
        vars = 2;
    }
    s = expand(L, s, vars);
    uint32_t bad = 0;
    check_hid(L, hid_type(s, &bad), bad);
    return 0;
}

// Le password stanno in config.lua (tabella `secrets`) e non passano mai
// per una stringa Lua: lo script non può stamparle né salvarle.
static int kb_type_secret(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    char key[64];
    snprintf(key, sizeof(key), "secrets.%s", name);
    const char *v = config_str(key, NULL);
    if (!v) return luaL_error(L, "secret '%s' not in config.lua", name);
    uint32_t bad = 0;
    check_hid(L, hid_type(v, &bad), bad);
    return 0;
}

// Nome di tasto ("ENTER", "F5", "CTRL") o singolo carattere ("r").
static void key_arg(lua_State *L, int idx, uint8_t *key, uint8_t *mods)
{
    const char *name = luaL_checkstring(L, idx);
    *key = *mods = 0;
    if (hid_key_by_name(name, key, mods)) return;
    // Un solo carattere UTF-8: lo si cerca nel layout corrente
    const unsigned char *p = (const unsigned char *)name;
    uint32_t cp = p[0];
    size_t len = strlen(name);
    if (len == 2 && (p[0] & 0xE0) == 0xC0) cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    else if (len != 1) luaL_argerror(L, idx, "unknown key name");
    // Lettera maiuscola in una combinazione: si intende il tasto, non lo Shift
    if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
    if (!hid_char_key(cp, key, mods)) luaL_argerror(L, idx, "key not in layout");
}

static int kb_tap(lua_State *L)
{
    uint8_t key, mods;
    key_arg(L, 1, &key, &mods);
    lua_Integer n = luaL_optinteger(L, 2, 1);
    for (lua_Integer i = 0; i < n; i++) check_hid(L, hid_chord(mods, key ? &key : NULL, key ? 1 : 0), 0);
    return 0;
}

// kb.press("CTRL", "ALT", "DELETE"): tutti insieme, poi rilascio.
static int kb_press(lua_State *L)
{
    int n = lua_gettop(L);
    uint8_t mods = 0, keys[6];
    int nkeys = 0;
    for (int i = 1; i <= n; i++) {
        uint8_t k, m;
        key_arg(L, i, &k, &m);
        mods |= m;
        if (k) {
            luaL_argcheck(L, nkeys < 6, i, "max 6 keys");
            keys[nkeys++] = k;
        }
    }
    check_hid(L, hid_chord(mods, keys, nkeys), 0);
    return 0;
}

static int kb_layout(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        const hid_layout_t *l = hid_layout_find(luaL_checkstring(L, 1));
        if (!l) return luaL_argerror(L, 1, "unknown layout");
        hid_set_layout(l);
    }
    lua_pushstring(L, hid_get_layout()->name);
    return 1;
}

static const char *const OS_NAMES[] = {"windows", "linux", "mac"};

static int kb_os(lua_State *L)
{
    if (!lua_isnoneornil(L, 1)) {
        hid_os_t os;
        if (!hid_os_parse(luaL_checkstring(L, 1), &os)) return luaL_argerror(L, 1, "windows|linux|mac");
        hid_set_os(os);
    }
    lua_pushstring(L, OS_NAMES[hid_get_os()]);
    return 1;
}

// Le quattro impostazioni di tempo hanno la stessa forma: kb.x(ms) imposta,
// kb.x() legge. L'upvalue è l'offset del campo in hid_timing_t.
static int kb_timing(lua_State *L)
{
    uint16_t *field = (uint16_t *)((char *)hid_timing() + lua_tointeger(L, lua_upvalueindex(1)));
    if (!lua_isnoneornil(L, 1)) *field = opt_ms(L, 1);
    lua_pushinteger(L, *field);
    return 1;
}

static void open_kb(lua_State *L)
{
    static const luaL_Reg fns[] = {
        {"type", kb_type}, {"type_secret", kb_type_secret}, {"tap", kb_tap},
        {"press", kb_press}, {"layout", kb_layout}, {"os", kb_os}, {NULL, NULL},
    };
    luaL_newlib(L, fns);
    static const struct {
        const char *name;
        size_t off;
    } timing[] = {
        {"speed", offsetof(hid_timing_t, key_delay)},
        {"hold", offsetof(hid_timing_t, hold)},
        {"jitter", offsetof(hid_timing_t, jitter)},
        {"line_delay", offsetof(hid_timing_t, line_delay)},
    };
    for (size_t i = 0; i < sizeof(timing) / sizeof(timing[0]); i++) {
        lua_pushinteger(L, timing[i].off);
        lua_pushcclosure(L, kb_timing, 1);
        lua_setfield(L, -2, timing[i].name);
    }
    lua_setglobal(L, "kb");
}

// ---------------------------------------------------------------------------
// mouse

static uint8_t button_arg(lua_State *L, int idx)
{
    static const char *const names[] = {"left", "right", "middle", NULL};
    static const uint8_t bits[] = {HID_BTN_LEFT, HID_BTN_RIGHT, HID_BTN_MIDDLE};
    return bits[luaL_checkoption(L, idx, "left", names)];
}

// Senza questo controllo l'errore sarebbe un generico "host not responding"
static void check_mouse(lua_State *L)
{
    if (!usbdev_has_mouse()) luaL_error(L, "no mouse while the disk is attached");
}

static int mouse_move(lua_State *L)
{
    check_mouse(L);
    check_hid(L, hid_mouse_move(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2)), 0);
    return 0;
}

static int mouse_click(lua_State *L)
{
    check_mouse(L);
    check_hid(L, hid_mouse_click(button_arg(L, 1)), 0);
    return 0;
}

static int mouse_scroll(lua_State *L)
{
    lua_Integer n = luaL_checkinteger(L, 1);
    luaL_argcheck(L, n >= -127 && n <= 127, 1, "-127..127");
    check_mouse(L);
    check_hid(L, hid_mouse_scroll((int)n, 0), 0);
    return 0;
}

static void open_mouse(lua_State *L)
{
    static const luaL_Reg fns[] = {
        {"move", mouse_move}, {"click", mouse_click}, {"scroll", mouse_scroll}, {NULL, NULL},
    };
    luaL_newlib(L, fns);
    lua_setglobal(L, "mouse");
}

// ---------------------------------------------------------------------------
// led

static const char *const FX_NAMES[] = {"off", "solid", "pulse", "blink", "rainbow", NULL};
static const led_fx_t FX_VALUES[] = {LED_FX_BLACK, LED_FX_SOLID, LED_FX_PULSE, LED_FX_BLINK, LED_FX_RAINBOW};

static int led_rgb(lua_State *L)
{
    uint32_t rgb = (luaL_checkinteger(L, 1) & 0xFF) << 16 | (luaL_checkinteger(L, 2) & 0xFF) << 8 |
                   (luaL_checkinteger(L, 3) & 0xFF);
    led_set(LED_LAYER_APP, LED_FX_SOLID, rgb, 0);
    return 0;
}

static int led_effect(lua_State *L)
{
    led_fx_t fx = FX_VALUES[luaL_checkoption(L, 1, NULL, FX_NAMES)];
    uint32_t rgb = luaL_optinteger(L, 2, ui_accent()) & 0xFFFFFF;
    led_set(LED_LAYER_APP, fx, rgb, luaL_optinteger(L, 3, 1000));
    return 0;
}

static int led_off(lua_State *L)
{
    led_set(LED_LAYER_APP, LED_FX_BLACK, 0, 0);
    return 0;
}

static void open_led(lua_State *L)
{
    static const luaL_Reg fns[] = {{"rgb", led_rgb}, {"effect", led_effect}, {"off", led_off}, {NULL, NULL}};
    luaL_newlib(L, fns);
    lua_setglobal(L, "led");
}

// Intestazione "@led pulse 00FFCC": effetto ed eventuale colore esadecimale.
static void apply_header_led(const char *spec)
{
    char fx[12] = "";
    unsigned rgb = ui_accent();
    if (sscanf(spec, "%11s %x", fx, &rgb) < 1) return;
    for (int i = 0; FX_NAMES[i]; i++) {
        if (strcasecmp(fx, FX_NAMES[i]) == 0) led_set(LED_LAYER_APP, FX_VALUES[i], rgb & 0xFFFFFF, 1000);
    }
}

// ---------------------------------------------------------------------------
// ui

static int ui_status(lua_State *L)
{
    strlcpy(s_run->status, luaL_optstring(L, 1, ""), sizeof(s_run->status));
    inj_redraw(s_run);
    return 0;
}

static int ui_progress(lua_State *L)
{
    s_run->progress = luaL_checkinteger(L, 1);
    s_run->progress_max = luaL_optinteger(L, 2, 100);
    inj_redraw(s_run);
    return 0;
}

// Indice 1-based della voce scelta, nil se l'utente esce (hold 3 s).
static int ui_menu(lua_State *L)
{
    const char *title = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int n = luaL_len(L, 2);
    luaL_argcheck(L, n > 0 && n <= 32, 2, "1..32 items");
    const char *items[32];
    luaL_checkstack(L, n, "menu items");
    for (int i = 0; i < n; i++) {
        lua_geti(L, 2, i + 1);
        items[i] = luaL_checkstring(L, -1); // resta sullo stack: la stringa non viene raccolta
    }
    int sel = app_menu(s_run->ctx, title, items, n, 0);
    inj_redraw(s_run);
    if (sel < 0) luaL_error(L, "aborted");
    lua_pushinteger(L, sel + 1);
    return 1;
}

static int ui_confirm(lua_State *L)
{
    const char *text = luaL_checkstring(L, 1);
    bool yes = app_confirm(s_run->ctx, s_run->meta->name[0] ? s_run->meta->name : "Confirm", text,
                           lua_isnoneornil(L, 2) || lua_toboolean(L, 2));
    if (s_run->ctx->exit_requested) luaL_error(L, "aborted");
    inj_redraw(s_run);
    lua_pushboolean(L, yes);
    return 1;
}

// Attende un gesto del pulsante: "click", "double" o "long".
static int ui_wait_button(lua_State *L)
{
    uint32_t c = s_run->clicks, d = s_run->doubles, l = s_run->longs;
    for (;;) {
        if (!inj_pump(s_run)) luaL_error(L, "aborted");
        if (s_run->clicks != c) lua_pushliteral(L, "click");
        else if (s_run->doubles != d) lua_pushliteral(L, "double");
        else if (s_run->longs != l) lua_pushliteral(L, "long");
        else {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        return 1;
    }
}

static void open_ui(lua_State *L)
{
    static const luaL_Reg fns[] = {
        {"status", ui_status}, {"progress", ui_progress}, {"menu", ui_menu},
        {"confirm", ui_confirm}, {"wait_button", ui_wait_button}, {NULL, NULL},
    };
    luaL_newlib(L, fns);
    lua_setglobal(L, "ui");
}

// ---------------------------------------------------------------------------
// sys

static int sys_sleep(lua_State *L)
{
    lua_Integer ms = luaL_checkinteger(L, 1);
    check_hid(L, hid_sleep(ms > 0 ? (uint32_t)ms : 0), 0);
    return 0;
}

// Attende che l'host abbia montato la tastiera. true, oppure false allo
// scadere del timeout (ms, default: per sempre).
static int sys_wait_host(lua_State *L)
{
    lua_Integer t = luaL_optinteger(L, 1, HOST_WAIT_MS);
    hid_result_t r = hid_wait_host((uint32_t)t);
    if (r == HID_ABORTED) check_hid(L, r, 0);
    lua_pushboolean(L, r == HID_OK);
    return 1;
}

static int sys_log(lua_State *L)
{
    ESP_LOGI("script", "%s", luaL_checkstring(L, 1));
    return 0;
}

static void open_sys(lua_State *L)
{
    static const luaL_Reg fns[] = {{"sleep", sys_sleep}, {"wait_host", sys_wait_host}, {"log", sys_log}, {NULL, NULL}};
    luaL_newlib(L, fns);
    lua_setglobal(L, "sys");
}

// ---------------------------------------------------------------------------
// fs (sola lettura, solo /payloads e /data)

// Percorso dello script ("/data/x.csv") -> percorso VFS ("/sd/data/x.csv").
static const char *fs_path(lua_State *L, int idx, char *out, size_t len)
{
    const char *p = luaL_checkstring(L, idx);
    bool allowed = (strncmp(p, "/payloads", 9) == 0 && (p[9] == '/' || !p[9])) ||
                   (strncmp(p, "/data", 5) == 0 && (p[5] == '/' || !p[5]));
    if (!allowed || strstr(p, "..")) luaL_argerror(L, idx, "only /payloads and /data are readable");
    snprintf(out, len, "/sd%s", p);
    return out;
}

static int fs_read(lua_State *L)
{
    char path[PAYLOAD_PATH_MAX];
    FILE *f = fopen(fs_path(L, 1, path, sizeof(path)), "rb");
    if (!f) {
        lua_pushnil(L);
        lua_pushstring(L, "cannot open");
        return 2;
    }
    luaL_Buffer b;
    char *dst = luaL_buffinitsize(L, &b, FS_READ_MAX);
    size_t n = fread(dst, 1, FS_READ_MAX, f);
    bool more = fgetc(f) != EOF;
    fclose(f);
    if (more) return luaL_error(L, "%s: file larger than %d bytes", path + 3, FS_READ_MAX);
    luaL_pushresultsize(&b, n);
    return 1;
}

// fs.lines restituisce un iteratore che tiene aperto il file in un
// userdata: __gc lo chiude anche se lo script esce dal for a metà.
static int lines_gc(lua_State *L)
{
    FILE **f = luaL_checkudata(L, 1, "inj.lines");
    if (*f) fclose(*f);
    *f = NULL;
    return 0;
}

static int lines_next(lua_State *L)
{
    FILE **f = luaL_checkudata(L, lua_upvalueindex(1), "inj.lines");
    char line[256];
    if (!*f || !fgets(line, sizeof(line), *f)) {
        if (*f) fclose(*f);
        *f = NULL;
        return 0;
    }
    size_t n = strcspn(line, "\r\n");
    lua_pushlstring(L, line, n);
    return 1;
}

static int fs_lines(lua_State *L)
{
    char path[PAYLOAD_PATH_MAX];
    fs_path(L, 1, path, sizeof(path));
    FILE **f = lua_newuserdatauv(L, sizeof(FILE *), 0);
    *f = NULL;
    if (luaL_newmetatable(L, "inj.lines")) {
        lua_pushcfunction(L, lines_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    *f = fopen(path, "r");
    if (!*f) return luaL_error(L, "%s: cannot open", path + 3);
    lua_pushcclosure(L, lines_next, 1);
    return 1;
}

static int fs_list(lua_State *L)
{
    char path[PAYLOAD_PATH_MAX];
    DIR *d = opendir(fs_path(L, 1, path, sizeof(path)));
    if (!d) {
        lua_pushnil(L);
        lua_pushstring(L, "cannot open");
        return 2;
    }
    lua_newtable(L);
    int i = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        lua_pushstring(L, e->d_name);
        lua_rawseti(L, -2, ++i);
    }
    closedir(d);
    return 1;
}

static int fs_exists(lua_State *L)
{
    char path[PAYLOAD_PATH_MAX];
    struct stat st;
    lua_pushboolean(L, stat(fs_path(L, 1, path, sizeof(path)), &st) == 0);
    return 1;
}

static void open_fs(lua_State *L)
{
    static const luaL_Reg fns[] = {
        {"read", fs_read}, {"lines", fs_lines}, {"list", fs_list}, {"exists", fs_exists}, {NULL, NULL},
    };
    luaL_newlib(L, fns);
    lua_setglobal(L, "fs");
}

// ---------------------------------------------------------------------------
// disk: la SD come disco in sola lettura sul PC, mentre la tastiera resta
// attiva. Solo primitive lato dongle: i comandi per copiare ed espellere
// sul PC li digita lo script con kb.type.

static bool s_attached;

static void disk_release(void)
{
    if (!s_attached) return;
    usbdev_apply(USB_PROFILE_HID, NULL);
    storage_unshare();
    s_attached = false;
}

// disk.attach([timeout_ms]): il dongle si ri-enumera, quindi attende che
// l'host rimonti la tastiera. Il mouse non è disponibile finché il disco è collegato.
static int disk_attach(lua_State *L)
{
    lua_Integer t = luaL_optinteger(L, 1, 15000);
    if (s_attached) return 0;
    if (storage_state() != STORAGE_MOUNTED) return luaL_error(L, "SD not mounted");
    syslog_flush(); // da qui il firmware non scrive più sulla SD
    s_run->ejected = false;
    if (usbdev_apply(USB_PROFILE_HID_DISK, storage_share_ro()) != ESP_OK) {
        storage_unshare();
        usbdev_apply(USB_PROFILE_HID, NULL);
        return luaL_error(L, "cannot switch USB profile");
    }
    s_attached = true;
    check_hid(L, hid_wait_host((uint32_t)t), 0);
    return 0;
}

// disk.wait_eject([timeout_ms]): "eject" se l'host ha espulso il disco,
// "click" se l'utente ha premuto il pulsante, nil allo scadere del tempo.
static int disk_wait_eject(lua_State *L)
{
    if (!s_attached) return luaL_error(L, "call disk.attach() first");
    int64_t end = esp_timer_get_time() + luaL_optinteger(L, 1, 120000) * 1000LL;
    uint32_t clicks = s_run->clicks;
    while (esp_timer_get_time() < end) {
        if (!inj_pump(s_run)) luaL_error(L, "aborted");
        if (s_run->ejected) {
            lua_pushliteral(L, "eject");
            return 1;
        }
        if (s_run->clicks != clicks) {
            lua_pushliteral(L, "click");
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return 0;
}

static int disk_detach(lua_State *L)
{
    disk_release();
    check_hid(L, hid_wait_host(15000), 0);
    return 0;
}

static void open_disk(lua_State *L)
{
    static const luaL_Reg fns[] = {
        {"attach", disk_attach}, {"wait_eject", disk_wait_eject}, {"detach", disk_detach}, {NULL, NULL},
    };
    luaL_newlib(L, fns);
    lua_setglobal(L, "disk");
}

// ---------------------------------------------------------------------------
// cfg e require

// cfg("chiave", default): valore di config.lua come stringa (o il default).
// I segreti restano fuori: si usano solo con kb.type_secret.
static int l_cfg(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    const char *v = strncmp(key, "secrets", 7) == 0 ? NULL : config_str(key, NULL);
    if (v) lua_pushstring(L, v);
    else lua_settop(L, 2);
    return 1;
}

// require("nome") carica /payloads/lib/nome.lua una volta sola.
static int l_require(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    for (const char *p = name; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) luaL_argerror(L, 1, "invalid module name");
    }
    lua_getfield(L, LUA_REGISTRYINDEX, "inj.loaded");
    lua_getfield(L, -1, name);
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);

    char path[PAYLOAD_PATH_MAX], err[160];
    snprintf(path, sizeof(path), "%s/%s.lua", PAYLOAD_LIB_DIR, name);
    if (!luart_dofile(L, path, 1, err, sizeof(err))) return luaL_error(L, "%s", err);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name);
    return 1;
}

// ---------------------------------------------------------------------------

static bool lua_can_run(const char *path)
{
    size_t l = strlen(path);
    return l > 4 && strcasecmp(path + l - 4, ".lua") == 0;
}

static void on_timeout(void *arg)
{
    inj_run_t *r = arg;
    r->timed_out = true;
    r->ctx->exit_requested = true;
}

// Default da config.lua, poi intestazione dello script.
static void apply_settings(const payload_meta_t *m)
{
    hid_reset();
    const char *layout = m->layout[0] ? m->layout : config_str("keyboard.layout", "us");
    const char *os = m->os[0] ? m->os : config_str("keyboard.os", "windows");
    hid_set_layout(hid_layout_find(layout));
    hid_os_t o;
    if (hid_os_parse(os, &o)) hid_set_os(o);
    hid_timing_t *t = hid_timing();
    t->key_delay = m->key_delay >= 0 ? m->key_delay : config_int("keyboard.key_delay", 8);
    t->hold = m->hold >= 0 ? m->hold : config_int("keyboard.hold", 5);
    t->jitter = m->jitter >= 0 ? m->jitter : config_int("keyboard.jitter", 0);
    t->line_delay = m->line_delay >= 0 ? m->line_delay : config_int("keyboard.line_delay", 0);
    led_set(LED_LAYER_APP, LED_FX_PULSE, ui_accent(), 1000);
    if (m->led[0]) apply_header_led(m->led);
}

static void lua_run(inj_run_t *r)
{
    const payload_meta_t *m = r->meta;
    s_run = r;
    apply_settings(m);
    if (m->layout[0] && !hid_layout_find(m->layout)) {
        snprintf(r->error, sizeof(r->error), "unknown @layout %s", m->layout);
        return;
    }
    hid_set_poll(poll, NULL);

    esp_timer_handle_t timer = NULL;
    if (m->timeout > 0) {
        esp_timer_create_args_t a = {.callback = on_timeout, .arg = r, .name = "inj_timeout"};
        esp_timer_create(&a, &timer);
        esp_timer_start_once(timer, m->timeout * 1000000LL);
    }

    // Attesa dell'host, poi start_delay: l'host deve aver caricato il driver
    r->waiting_host = true;
    inj_redraw(r);
    hid_result_t hr = hid_wait_host(HOST_WAIT_MS);
    r->waiting_host = false;
    r->started_us = esp_timer_get_time();
    inj_redraw(r);
    if (hr == HID_OK) hr = hid_sleep(m->start_delay >= 0 ? m->start_delay : config_int("keyboard.start_delay", 1000));

    if (hr == HID_OK) {
        luart_opts_t opts = {.mem_limit = LUA_MEM_LIMIT, .abort_flag = &r->ctx->exit_requested};
        lua_State *L = luart_new(&opts);
        if (!L) {
            strlcpy(r->error, "out of memory", sizeof(r->error));
        } else {
            open_kb(L);
            open_mouse(L);
            open_led(L);
            open_ui(L);
            open_sys(L);
            open_fs(L);
            open_disk(L);
            lua_register(L, "cfg", l_cfg);
            lua_newtable(L);
            lua_setfield(L, LUA_REGISTRYINDEX, "inj.loaded");
            lua_register(L, "require", l_require);

            r->ok = luart_dofile(L, m->path, 0, r->error, sizeof(r->error));
            ESP_LOGI(TAG, "%s: %s (Lua peak %u B)", m->path, r->ok ? "ok" : r->error, (unsigned)luart_mem_peak(L));
            luart_close(L);
        }
    }
    if (timer) {
        esp_timer_stop(timer);
        esp_timer_delete(timer);
    }
    if (!r->ok && (r->timed_out || r->ctx->exit_requested)) r->aborted = true;
    hid_set_poll(NULL, NULL);
    hid_release_all();
    disk_release(); // script finito o interrotto con il disco ancora collegato
    s_run = NULL;
}

const inj_engine_t engine_lua = {
    .name = "lua",
    .can_run = lua_can_run,
    .run = lua_run,
};
