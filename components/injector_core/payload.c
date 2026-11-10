#include "payload.h"

#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "hid_layout.h"

static int cmp_entry(const void *a, const void *b)
{
    const payload_entry_t *x = a, *y = b;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->title, y->title);
}

static bool has_lua_ext(const char *n)
{
    size_t l = strlen(n);
    return l > 4 && strcasecmp(n + l - 4, ".lua") == 0;
}

// Riempie un elemento-script leggendo l'intestazione. Il titolo viene da
// @name, altrimenti dal nome file senza ".lua".
static void fill_file(payload_entry_t *p, const char *dir, const char *name)
{
    memset(p, 0, sizeof(*p));
    strlcpy(p->name, name, sizeof(p->name));
    strlcpy(p->title, name, sizeof(p->title));
    snprintf(p->path, sizeof(p->path), "%s/%s", dir, name);
    payload_meta_t *m = malloc(sizeof(*m)); // 400 byte: meglio fuori dallo stack, la ricorsione li moltiplica
    bool named = false;
    if (m && payload_read_meta(p->path, m)) {
        named = m->name[0];
        if (named) strlcpy(p->title, m->name, sizeof(p->title));
        strlcpy(p->layout, m->layout, sizeof(p->layout));
        strlcpy(p->os, m->os, sizeof(p->os));
        strlcpy(p->tags, m->tags, sizeof(p->tags));
    }
    free(m);
    if (!named) {
        char *dot = strrchr(p->title, '.');
        if (dot) *dot = 0;
    }
}

int payload_list(const char *dir, payload_entry_t *out, int max)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    bool at_root = strcmp(dir, PAYLOAD_DIR) == 0;
    struct dirent *e;
    while (n < max && (e = readdir(d))) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue; // nascosti, "._*" di macOS
        bool is_dir = e->d_type == DT_DIR;
        if (is_dir && at_root && strcmp(e->d_name, "lib") == 0) continue;
        if (!is_dir && !has_lua_ext(e->d_name)) continue;
        if (is_dir) {
            payload_entry_t *p = &out[n++];
            memset(p, 0, sizeof(*p));
            strlcpy(p->name, e->d_name, sizeof(p->name));
            strlcpy(p->title, e->d_name, sizeof(p->title));
            p->is_dir = true;
        } else {
            fill_file(&out[n++], dir, e->d_name);
        }
    }
    closedir(d);
    qsort(out, n, sizeof(out[0]), cmp_entry);
    return n;
}

static int scan_rec(const char *dir, payload_entry_t *out, int n, int max, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return n;
    struct dirent *e;
    while (n < max && (e = readdir(d))) {
        if (e->d_name[0] == '.' || e->d_name[0] == '_') continue;
        if (e->d_type == DT_DIR) {
            if (depth >= 3 || (depth == 0 && !strcmp(e->d_name, "lib"))) continue;
            char sub[PAYLOAD_PATH_MAX];
            if (snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name) < (int)sizeof(sub)) {
                n = scan_rec(sub, out, n, max, depth + 1);
            }
        } else if (has_lua_ext(e->d_name)) {
            fill_file(&out[n++], dir, e->d_name);
        }
    }
    closedir(d);
    return n;
}

int payload_scan(const char *dir, payload_entry_t *out, int max)
{
    DIR *d = opendir(dir);
    if (!d) return -1;
    closedir(d);
    int n = scan_rec(dir, out, 0, max, 0);
    qsort(out, n, sizeof(out[0]), cmp_entry);
    return n;
}

bool payload_has_tag(const char *tags, const char *tag)
{
    size_t tl = strlen(tag);
    const char *p = tags;
    while (*p) {
        while (*p == ',' || isspace((unsigned char)*p)) p++;
        const char *q = p;
        while (*q && *q != ',' && !isspace((unsigned char)*q)) q++;
        if ((size_t)(q - p) == tl && strncasecmp(p, tag, tl) == 0) return true;
        p = q;
    }
    return false;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static bool parse_int(char *v, int *out)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v || *trim(end) || n < 0) return false;
    *out = (int)n;
    return true;
}

typedef struct {
    const char *key;
    size_t offset;
    int max; // i tempi finiscono in campi uint16_t: oltre 60000 ms si troncavano
} int_key_t;

static const int_key_t s_int_keys[] = {
    {"key_delay", offsetof(payload_meta_t, key_delay), 60000},
    {"hold", offsetof(payload_meta_t, hold), 60000},
    {"jitter", offsetof(payload_meta_t, jitter), 60000},
    {"line_delay", offsetof(payload_meta_t, line_delay), 60000},
    {"start_delay", offsetof(payload_meta_t, start_delay), 60000},
    {"timeout", offsetof(payload_meta_t, timeout), 24 * 3600}, // secondi
};

bool payload_read_meta(const char *path, payload_meta_t *m)
{
    memset(m, 0, sizeof(*m));
    strlcpy(m->path, path, sizeof(m->path));
    m->key_delay = m->hold = m->jitter = m->line_delay = m->start_delay = m->timeout = -1;
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[200];
    int lineno = 0;
    // L'intestazione è il blocco di commenti iniziale: ci si ferma alla
    // prima riga che non è un commento.
    while (!m->error[0] && fgets(line, sizeof(line), f)) {
        lineno++;
        char *s = trim(line);
        if (!*s) continue;
        if (strncmp(s, "--", 2) != 0) break;
        if (strncmp(s, "--- @", 5) != 0) continue;
        char *key = s + 5, *val = key;
        while (*val && !isspace((unsigned char)*val)) val++;
        if (*val) *val++ = 0;
        val = trim(val);

        if (!strcmp(key, "name")) strlcpy(m->name, val, sizeof(m->name));
        else if (!strcmp(key, "desc")) strlcpy(m->desc, val, sizeof(m->desc));
        else if (!strcmp(key, "layout")) {
            strlcpy(m->layout, val, sizeof(m->layout));
            if (!hid_layout_find(val)) snprintf(m->error, sizeof(m->error), "line %d: unknown @layout %s", lineno, val);
        } else if (!strcmp(key, "os")) {
            // Prima un @os sbagliato (es. "wndows") veniva ignorato in silenzio
            hid_os_t os;
            strlcpy(m->os, val, sizeof(m->os));
            if (!hid_os_parse(val, &os)) snprintf(m->error, sizeof(m->error), "line %d: unknown @os %s", lineno, val);
        }
        else if (!strcmp(key, "led")) strlcpy(m->led, val, sizeof(m->led));
        else if (!strcmp(key, "tags")) strlcpy(m->tags, val, sizeof(m->tags));
        else {
            bool known = false;
            for (size_t i = 0; i < sizeof(s_int_keys) / sizeof(s_int_keys[0]); i++) {
                if (strcmp(key, s_int_keys[i].key)) continue;
                known = true;
                int *dst = (int *)((char *)m + s_int_keys[i].offset);
                if (!parse_int(val, dst) || *dst > s_int_keys[i].max) {
                    snprintf(m->error, sizeof(m->error), "line %d: @%s needs a number 0..%d", lineno, key,
                             s_int_keys[i].max);
                }
            }
            if (!known) snprintf(m->error, sizeof(m->error), "line %d: unknown @%s", lineno, key);
        }
    }
    fclose(f);
    return true;
}
