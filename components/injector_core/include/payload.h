#pragma once

#include <stdbool.h>

#define PAYLOAD_DIR "/sd/payloads"
#define PAYLOAD_LIB_DIR PAYLOAD_DIR "/lib"
#define PAYLOAD_PATH_MAX 160

// Metadati letti dall'intestazione di uno script ("--- @chiave valore"),
// senza eseguirlo. I campi numerici valgono -1 se assenti.
typedef struct {
    char path[PAYLOAD_PATH_MAX];
    char name[48];
    char desc[160];
    char layout[16];
    char os[12];
    char led[32];
    char tags[40]; // @tags: parole separate da virgole o spazi
    int key_delay, hold, jitter, line_delay, start_delay, timeout;
    char error[96]; // intestazione non valida: messaggio con numero di riga
} payload_meta_t;

typedef struct {
    char name[64];  // nome file o cartella
    char title[48]; // @name o nome file senza estensione
    bool is_dir;
    // Solo per gli script, come scritti nell'intestazione (vuoti se assenti:
    // il default viene da config.lua, lo risolve chi mostra la riga).
    char layout[16];
    char os[12];
    char tags[40];
    char path[PAYLOAD_PATH_MAX]; // percorso completo, serve alle viste che non seguono le cartelle
} payload_entry_t;

// Elenca cartelle e script .lua di `dir` (cartelle prima, ordine alfabetico).
// Salta file nascosti, file AppleDouble ("._*") e la cartella lib.
int payload_list(const char *dir, payload_entry_t *out, int max);
bool payload_read_meta(const char *path, payload_meta_t *m);

// Come payload_list ma ricorsivo (fino a 3 livelli) e solo script, in un
// elenco piatto: base delle viste per OS e per tag.
int payload_scan(const char *dir, payload_entry_t *out, int max);
// true se `tag` compare in `tags` (confronto senza maiuscole, parole intere).
bool payload_has_tag(const char *tags, const char *tag);

// Layout e OS effettivi dello script: intestazione, altrimenti config.lua.
// payload_os restituisce un hid_os_t.
const char *payload_layout(const payload_entry_t *e);
int payload_os(const payload_entry_t *e);

// Tag distinti presenti negli script, in ordine alfabetico, al massimo `max`.
// PAYLOAD_TAG_LEN come il campo tags: un tag troncato non combaciava più con
// payload_has_tag.
#define PAYLOAD_TAG_LEN sizeof(((payload_entry_t *)0)->tags)
int payload_collect_tags(const payload_entry_t *e, int n, char tags[][PAYLOAD_TAG_LEN], int max);
