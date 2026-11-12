#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lua.h"

// Shared Lua runtime (config, injector, future apps).
//
// Every VM is a sandbox: only the safe libraries (base without file access,
// string, table, math, utf8, coroutine), limited memory and the ability to
// interrupt the script from outside through `abort_flag`.

typedef struct {
    size_t mem_limit;           // maximum bytes the VM may allocate
    volatile bool *abort_flag;  // when it becomes true the script ends with error "aborted"
    // Bytes of C stack to keep free (0 = no check). On every function call
    // the task stack is checked: below the threshold the script ends with an
    // error instead of sending the dongle into a panic.
    size_t stack_guard;
} luart_opts_t;

lua_State *luart_new(const luart_opts_t *opts);
void luart_close(lua_State *L);

// Runs a Lua file. On error writes the message into `err`
// (with "file:line:") and returns false. The return values (nret,
// LUA_MULTRET for all) stay on the stack.
bool luart_dofile(lua_State *L, const char *path, int nret, char *err, size_t errlen);

// Bytes currently allocated by the VM and the peak reached.
size_t luart_mem_used(lua_State *L);
size_t luart_mem_peak(lua_State *L);

// Returns true if execution was interrupted by abort_flag.
bool luart_aborted(lua_State *L);
