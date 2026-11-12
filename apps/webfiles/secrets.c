#include "secrets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// Beyond this number secrets stay visible instead of masked: a placeholder
// that can no longer be traced back to its value would erase it on
// save.
#define MAX_SECRETS 64

// Callback for every string literal found: start and length in the text,
// quotes or brackets included.
typedef void (*string_fn)(void *arg, const char *start, size_t len, bool secret);

// End of a long bracket [==[ ... ]==] starting at p (on '['), or NULL
// if p does not open a long bracket. Without a closing one: end of text.
static const char *long_bracket_end(const char *p)
{
    const char *q = p + 1;
    int level = 0;
    while (*q == '=') q++, level++;
    if (*q != '[') return NULL;
    for (q++; *q; q++) {
        if (*q != ']') continue;
        const char *r = q + 1;
        int l = 0;
        while (*r == '=') r++, l++;
        if (l == level && *r == ']') return r + 1;
    }
    return q;
}

static bool is_secret_key(const char *id, size_t n)
{
    return (n == 4 && strncasecmp(id, "pass", 4) == 0) || (n == 8 && strncasecmp(id, "password", 8) == 0);
}

static void scan(const char *s, string_fn fn, void *arg)
{
    const char *p = s;
    int depth = 0, secrets_depth = 0;
    // Secret key seen: 1 after the name, 2 after "=". For "secrets": 1, 2, then "{".
    int key_state = 0, table_state = 0;
    while (*p) {
        unsigned char c = *p;
        if (isspace(c)) {
            p++;
            continue;
        }
        if (c == '-' && p[1] == '-') {
            p += 2;
            const char *e = *p == '[' ? long_bracket_end(p) : NULL;
            if (e) p = e;
            else while (*p && *p != '\n') p++;
            continue;
        }
        const char *end = NULL;
        if (c == '"' || c == '\'') {
            end = p + 1;
            while (*end && *end != (char)c && *end != '\n') {
                if (*end == '\\' && end[1]) end++;
                end++;
            }
            if (*end == (char)c) end++;
        } else if (c == '[') {
            end = long_bracket_end(p);
        }
        if (end) {
            fn(arg, p, (size_t)(end - p), key_state == 2 || secrets_depth > 0);
            key_state = table_state = 0;
            p = end;
            continue;
        }
        if (isalpha(c) || c == '_') {
            const char *id = p;
            while (isalnum((unsigned char)*p) || *p == '_') p++;
            size_t n = (size_t)(p - id);
            key_state = is_secret_key(id, n) ? 1 : 0;
            table_state = n == 7 && strncmp(id, "secrets", 7) == 0 ? 1 : 0;
            continue;
        }
        if (c == '=' && p[1] != '=') {
            key_state = key_state == 1 ? 2 : 0;
            table_state = table_state == 1 ? 2 : 0;
        } else if (c == '{') {
            depth++;
            if (table_state == 2 && !secrets_depth) secrets_depth = depth;
            key_state = table_state = 0;
        } else if (c == '}') {
            if (secrets_depth && depth == secrets_depth) secrets_depth = 0;
            if (depth > 0) depth--;
            key_state = table_state = 0;
        } else {
            if (c == '=') p++; // "==": not an assignment
            key_state = table_state = 0;
        }
        p++;
    }
}

typedef struct {
    char *out;
    size_t len, cap;
    bool fail;
} buf_t;

static void put(buf_t *b, const char *s, size_t n)
{
    if (b->fail) return;
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1) cap *= 2;
        char *o = realloc(b->out, cap);
        if (!o) {
            b->fail = true;
            return;
        }
        b->out = o;
        b->cap = cap;
    }
    memcpy(b->out + b->len, s, n);
    b->len += n;
    b->out[b->len] = 0;
}

static char *finish(buf_t *b)
{
    if (b->fail) {
        free(b->out);
        return NULL;
    }
    if (!b->out) put(b, "", 0); // empty text: empty string, not NULL
    return b->fail ? NULL : b->out;
}

typedef struct {
    buf_t b;
    const char *src; // how far it has already been copied
    int n;
} mask_t;

static void mask_string(void *arg, const char *start, size_t len, bool secret)
{
    mask_t *m = arg;
    if (!secret || m->n >= MAX_SECRETS) return;
    put(&m->b, m->src, (size_t)(start - m->src));
    char ph[24];
    snprintf(ph, sizeof(ph), "\"<secret:%d>\"", ++m->n);
    put(&m->b, ph, strlen(ph));
    m->src = start + len;
}

char *secrets_mask(const char *src)
{
    mask_t m = {.src = src};
    scan(src, mask_string, &m);
    put(&m.b, m.src, strlen(m.src));
    return finish(&m.b);
}

// Secrets of orig, in order
typedef struct {
    const char *start[MAX_SECRETS];
    size_t len[MAX_SECRETS];
    int n;
} list_t;

static void collect(void *arg, const char *start, size_t len, bool secret)
{
    list_t *l = arg;
    if (!secret || l->n >= MAX_SECRETS) return;
    l->start[l->n] = start;
    l->len[l->n] = len;
    l->n++;
}

typedef struct {
    buf_t b;
    const char *src;
    const list_t *orig;
    bool unknown;
} unmask_t;

static void unmask_string(void *arg, const char *start, size_t len, bool secret)
{
    (void)secret; // the placeholder is valid anywhere: the page may have moved it
    unmask_t *u = arg;
    int idx;
    char tail;
    // Only the whole placeholder, in double or single quotes
    if (len < 4 || (start[0] != '"' && start[0] != '\'') || start[len - 1] != start[0]) return;
    if (sscanf(start + 1, "<secret:%d>%c", &idx, &tail) != 2 || tail != start[0]) return;
    char ph[24];
    int phl = snprintf(ph, sizeof(ph), "%c<secret:%d>%c", start[0], idx, start[0]);
    if ((size_t)phl != len || memcmp(ph, start, len) != 0) return;
    // Without a value the placeholder would end up in the file as the password
    if (idx < 1 || idx > u->orig->n) {
        u->unknown = true;
        return;
    }
    put(&u->b, u->src, (size_t)(start - u->src));
    put(&u->b, u->orig->start[idx - 1], u->orig->len[idx - 1]);
    u->src = start + len;
}

char *secrets_unmask(const char *edited, const char *orig, bool *unknown)
{
    *unknown = false;
    list_t *l = calloc(1, sizeof(*l));
    if (!l) return NULL;
    scan(orig, collect, l);
    unmask_t u = {.src = edited, .orig = l};
    scan(edited, unmask_string, &u);
    put(&u.b, u.src, strlen(u.src));
    free(l);
    if (u.unknown) {
        *unknown = true;
        u.b.fail = true;
    }
    return finish(&u.b);
}
