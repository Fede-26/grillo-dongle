#include "webjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void jb_raw(jbuf_t *b, const char *s, size_t n)
{
    if (b->fail) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 128;
        while (cap < b->len + n + 1) cap *= 2;
        char *p = realloc(b->p, cap);
        if (!p) {
            b->fail = true;
            return;
        }
        b->p = p;
        b->cap = cap;
    }
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

void jb_lit(jbuf_t *b, const char *s)
{
    jb_raw(b, s, strlen(s));
}

// Lunghezza della sequenza UTF-8 valida che inizia in s, 0 se non valida
static int utf8_len(const unsigned char *s)
{
    unsigned char c = s[0];
    int n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

void jb_str(jbuf_t *b, const char *s)
{
    jb_lit(b, "\"");
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int n = utf8_len(p);
        char esc[8];
        if (n == 0) {
            jb_lit(b, "?");
            n = 1;
        } else if (*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = *p;
            jb_raw(b, esc, 2);
        } else if (*p < 0x20) {
            snprintf(esc, sizeof(esc), "\\u%04x", *p);
            jb_lit(b, esc);
        } else {
            jb_raw(b, (const char *)p, n);
        }
        p += n;
    }
    jb_lit(b, "\"");
}

void jb_kstr(jbuf_t *b, const char *key, const char *val)
{
    jb_lit(b, ",\"");
    jb_lit(b, key);
    jb_lit(b, "\":");
    jb_str(b, val);
}

void jb_kint(jbuf_t *b, const char *key, long long val)
{
    char t[48];
    snprintf(t, sizeof(t), ",\"%s\":%lld", key, val);
    jb_lit(b, t);
}

void jb_kbool(jbuf_t *b, const char *key, bool val)
{
    char t[40];
    snprintf(t, sizeof(t), ",\"%s\":%s", key, val ? "true" : "false");
    jb_lit(b, t);
}

void jb_begin(jbuf_t *b, const char *type)
{
    jb_lit(b, "{\"t\":");
    jb_str(b, type);
}

char *jb_end(jbuf_t *b)
{
    jb_lit(b, "}");
    if (!b->fail) return b->p;
    free(b->p);
    return NULL;
}
