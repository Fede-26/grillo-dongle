#include "luart.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lauxlib.h"
#include "lualib.h"

static const char *TAG = "lua";

// Stato del runtime agganciato alla VM tramite lua_getallocf/ud.
typedef struct {
    size_t used, peak, limit;
    volatile bool *abort_flag;
    bool aborted;
    size_t stack_guard;
} rt_t;

static void *rt_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    rt_t *rt = ud;
    // Con ptr == NULL, osize indica il tipo di oggetto, non una dimensione.
    size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        free(ptr);
        rt->used -= old;
        return NULL;
    }
    if (rt->used - old + nsize > rt->limit) return NULL; // Lua solleva "not enough memory"
    void *p = realloc(ptr, nsize);
    if (p) {
        rt->used = rt->used - old + nsize;
        if (rt->used > rt->peak) rt->peak = rt->used;
    }
    return p;
}

static rt_t *rt_of(lua_State *L)
{
    void *ud;
    lua_getallocf(L, &ud);
    return ud;
}

// Hook chiamato ogni N istruzioni: è l'unico punto in cui uno script in un
// loop stretto può essere fermato, perché un task FreeRTOS non si può
// terminare dall'esterno in modo sicuro.
//
// Con stack_guard è chiamato anche a ogni chiamata di funzione. Lua limita
// l'annidamento delle chiamate C a LUAI_MAXCCALLS (200), ma sul dispositivo
// ogni livello di pcall costa circa 512 byte di stack (misurato il
// 2026-11-10): un task da 10 KB andava in overflow intorno al 15° livello.
// Il costo cambia con il percorso (gsub, sort, metamethod), quindi si
// controlla lo spazio rimasto invece di abbassare il limite.
static void rt_hook(lua_State *L, lua_Debug *ar)
{
    rt_t *rt = rt_of(L);
    if (ar->event != LUA_HOOKCOUNT) {
        // Lo stack cresce verso il basso: lo spazio libero è tra l'inizio
        // dell'area di stack e lo stack pointer corrente.
        size_t left = (uint8_t *)esp_cpu_get_sp() - (uint8_t *)xTaskGetStackStart(NULL);
        if (left < rt->stack_guard) luaL_error(L, "C stack overflow (too many nested calls)");
        return;
    }
    if (rt->abort_flag && *rt->abort_flag) {
        rt->aborted = true;
        luaL_error(L, "aborted");
    }
}

static int l_print(lua_State *L)
{
    int n = lua_gettop(L);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int i = 1; i <= n; i++) {
        if (i > 1) luaL_addchar(&b, '\t');
        luaL_tolstring(L, i, NULL);
        luaL_addvalue(&b);
    }
    luaL_pushresult(&b);
    ESP_LOGI(TAG, "%s", lua_tostring(L, -1));
    return 0;
}

lua_State *luart_new(const luart_opts_t *opts)
{
    rt_t *rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    rt->limit = opts->mem_limit;
    rt->abort_flag = opts->abort_flag;
    rt->stack_guard = opts->stack_guard;
    lua_State *L = lua_newstate(rt_alloc, rt);
    if (!L) {
        free(rt);
        return NULL;
    }
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},
        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},
        {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},
        {LUA_UTF8LIBNAME, luaopen_utf8},
        {NULL, NULL},
    };
    for (const luaL_Reg *l = libs; l->func; l++) {
        luaL_requiref(L, l->name, l->func, 1);
        lua_pop(L, 1);
    }
    // Niente accesso diretto al filesystem: chi serve (fs.*, require) lo
    // espone l'host con i propri controlli sui percorsi.
    static const char *const removed[] = {"dofile", "loadfile", NULL};
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(L);
        lua_setglobal(L, *r);
    }
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    int mask = (opts->abort_flag ? LUA_MASKCOUNT : 0) | (opts->stack_guard ? LUA_MASKCALL : 0);
    if (mask) lua_sethook(L, rt_hook, mask, 1000);
    return L;
}

void luart_close(lua_State *L)
{
    if (!L) return;
    rt_t *rt = rt_of(L);
    lua_close(L);
    free(rt);
}

bool luart_dofile(lua_State *L, const char *path, int nret, char *err, size_t errlen)
{
    // Il nome del chunk senza "@/sd" rende i messaggi più corti sul display.
    const char *name = strncmp(path, "/sd/", 4) == 0 ? path + 4 : path;
    char chunk[96];
    snprintf(chunk, sizeof(chunk), "@%s", name);

    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errlen, "%s: cannot open", name);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc(size > 0 ? size : 1);
    if (!src) {
        fclose(f);
        snprintf(err, errlen, "%s: out of memory", name);
        return false;
    }
    size_t rd = fread(src, 1, size, f);
    fclose(f);

    // Modalità "t": solo sorgenti testuali, mai bytecode precompilato
    // (il bytecode malformato può mandare in crash la VM).
    int rc = luaL_loadbufferx(L, src, rd, chunk, "t");
    free(src);
    if (rc == LUA_OK) rc = lua_pcall(L, 0, nret, 0);
    if (rc != LUA_OK) {
        snprintf(err, errlen, "%s", lua_tostring(L, -1) ? lua_tostring(L, -1) : "unknown error");
        lua_pop(L, 1);
        return false;
    }
    return true;
}

size_t luart_mem_used(lua_State *L)
{
    return rt_of(L)->used;
}

size_t luart_mem_peak(lua_State *L)
{
    return rt_of(L)->peak;
}

bool luart_aborted(lua_State *L)
{
    return rt_of(L)->aborted;
}
