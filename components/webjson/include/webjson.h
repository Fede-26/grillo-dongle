#pragma once

#include <stdbool.h>
#include <stddef.h>

// JSON messages for the web pages (Web Injector, Web Files): one object per
// message, built piece by piece in a buffer that grows by itself.
//
//   jbuf_t b = {0};
//   jb_begin(&b, "entry");          // {"t":"entry"
//   jb_kstr(&b, "name", name);      // ,"name":"..."
//   char *msg = jb_end(&b);         // } — NULL if out of memory
//   ...
//   free(msg);

typedef struct {
    char *p;
    size_t len, cap;
    bool fail; // out of memory: jb_end returns NULL
} jbuf_t;

// Text copied as is (ready-made JSON)
void jb_raw(jbuf_t *b, const char *s, size_t n);
void jb_lit(jbuf_t *b, const char *s);
// Quoted JSON string, with escapes. The browser closes the WebSocket if a
// text frame is not valid UTF-8: file names and script texts may not be, so
// invalid bytes become "?".
void jb_str(jbuf_t *b, const char *s);
// "key":value, with a leading comma: they go after jb_begin
void jb_kstr(jbuf_t *b, const char *key, const char *val);
void jb_kint(jbuf_t *b, const char *key, long long val);
void jb_kbool(jbuf_t *b, const char *key, bool val);
// Opens the object with the message type: {"t":"..."
void jb_begin(jbuf_t *b, const char *type);
// Closes the object and returns the string (to be freed), NULL when out of memory
char *jb_end(jbuf_t *b);
