#pragma once

#include <stdbool.h>
#include <stddef.h>

// Messaggi JSON per le pagine web (Web Injector, Web Files): un oggetto per
// messaggio, costruito a pezzi in un buffer che cresce da sé.
//
//   jbuf_t b = {0};
//   jb_begin(&b, "entry");          // {"t":"entry"
//   jb_kstr(&b, "name", name);      // ,"name":"..."
//   char *msg = jb_end(&b);         // } — NULL se la memoria è finita
//   ...
//   free(msg);

typedef struct {
    char *p;
    size_t len, cap;
    bool fail; // memoria finita: jb_end restituisce NULL
} jbuf_t;

// Testo copiato così com'è (JSON già pronto)
void jb_raw(jbuf_t *b, const char *s, size_t n);
void jb_lit(jbuf_t *b, const char *s);
// Stringa JSON tra virgolette, con gli escape. Il browser chiude il
// WebSocket se un frame di testo non è UTF-8 valido: nomi di file e testi
// degli script possono non esserlo, quindi i byte non validi diventano "?".
void jb_str(jbuf_t *b, const char *s);
// "chiave":valore, con la virgola davanti: vanno dopo jb_begin
void jb_kstr(jbuf_t *b, const char *key, const char *val);
void jb_kint(jbuf_t *b, const char *key, long long val);
void jb_kbool(jbuf_t *b, const char *key, bool val);
// Apre l'oggetto con il tipo del messaggio: {"t":"..."
void jb_begin(jbuf_t *b, const char *type);
// Chiude l'oggetto e restituisce la stringa (da liberare), NULL senza memoria
char *jb_end(jbuf_t *b);
