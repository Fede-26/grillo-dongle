#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lua.h"

// Runtime Lua condiviso (config, injector, app future).
//
// Ogni VM è una sandbox: solo le librerie sicure (base senza accesso a file,
// string, table, math, utf8, coroutine), memoria limitata e possibilità di
// interrompere lo script dall'esterno tramite `abort_flag`.

typedef struct {
    size_t mem_limit;           // byte massimi allocabili dalla VM
    volatile bool *abort_flag;  // se diventa true lo script termina con errore "aborted"
} luart_opts_t;

lua_State *luart_new(const luart_opts_t *opts);
void luart_close(lua_State *L);

// Esegue un file Lua. In caso di errore scrive in `err` il messaggio
// (con "file:riga:") e restituisce false. I valori di ritorno (nret,
// LUA_MULTRET per tutti) restano sullo stack.
bool luart_dofile(lua_State *L, const char *path, int nret, char *err, size_t errlen);

// Byte attualmente allocati dalla VM e picco raggiunto.
size_t luart_mem_used(lua_State *L);
size_t luart_mem_peak(lua_State *L);

// Restituisce true se l'esecuzione è stata interrotta dall'abort_flag.
bool luart_aborted(lua_State *L);
