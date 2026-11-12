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

// Runtime state attached to the VM through lua_getallocf/ud.
typedef struct {
    size_t used, peak, limit;
    volatile bool *abort_flag;
    bool aborted;
    size_t stack_guard;
} rt_t;

static void *rt_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    rt_t *rt = ud;
    // With ptr == NULL, osize gives the object type, not a size.
    size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        free(ptr);
        rt->used -= old;
        return NULL;
    }
    if (rt->used - old + nsize > rt->limit) return NULL; // Lua raises "not enough memory"
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

// Hook called every N instructions: it is the only point where a script in a
// tight loop can be stopped, because a FreeRTOS task cannot be terminated
// safely from outside.
//
// With stack_guard it is also called on every function call. Lua limits the
// nesting of C calls to LUAI_MAXCCALLS (200), but on the device each pcall
// level costs about 512 bytes of stack (measured on 2026-11-10): a 10 KB task
// overflowed around the 15th level. The cost changes with the code path
// (gsub, sort, metamethods), so the remaining space is checked instead of
// lowering the limit.
static void rt_hook(lua_State *L, lua_Debug *ar)
{
    rt_t *rt = rt_of(L);
    if (ar->event != LUA_HOOKCOUNT) {
        // The stack grows downward: the free space lies between the start of
        // the stack area and the current stack pointer.
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
    // No direct filesystem access: what is needed (fs.*, require) is exposed
    // by the host with its own path checks.
    static const char *const removed[] = {"dofile", "loadfile", NULL};
    for (const char *const *r = removed; *r; r++) {
        lua_pushnil(L);
        lua_setglobal(L, *r);
    }
    lua_pushcfunction(L, l_print);
    lua_setglobal(L, "print");
    // Text sources only from load() too: malformed bytecode can corrupt the
    // VM memory, and luart_dofile already rejects it. Without string.dump a
    // script cannot even produce any.
    (void)luaL_dostring(L, "local l = load\n"
                     "load = function(c, n, _, e) return l(c, n, 't', e) end");
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
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
    // The chunk name without "@/sd" makes the messages shorter on the display.
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

    // Mode "t": text sources only, never precompiled bytecode
    // (malformed bytecode can crash the VM).
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
