// Web Files: the microSD browsed and edited from a web page.
//
// Like Web HID: network lease (known network or hotspot), server with PIN, QR
// code and PIN on the display. The page sees the whole SD: listing, upload,
// download, new folder or file, rename and move, copy, trash and permanent
// deletion, text editor (Lua and CSV) with script checking on the dongle.
// All the work runs in the httpd task; the app task only draws the
// screen.
//
// Short operations go through the WebSocket; file contents through the
// HTTP routes, protected by the session token (websrv_authorized):
//   GET  /api/file?path=&reveal=      text for the editor (max EDIT_MAX)
//   PUT  /api/file?path=&overwrite=   save and upload (raw body)
//   GET  /api/download?path=&t=       download as an attachment
//   POST /api/check?path=             compiles a Lua script without running it
//
// Trash: a deleted item goes to /.trash/<n>/<name>, next to
// /.trash/<n>/.origin (line 1 original path, line 2 date). It is a rename,
// so it is instant even for a whole folder.

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "clock.h"
#include "config.h"
#include "icons.h"
#include "lauxlib.h"
#include "led.h"
#include "luart.h"
#include "net.h"
#include "payload.h"
#include "secrets.h"
#include "storage.h"
#include "ui.h"
#include "usbdev.h"
#include "webfiles.h"
#include "webjson.h"
#include "websrv.h"
#include "webtheme.h"
#include "webui.h"

#define ACCENT      0xF5D547
#define TRASH_DIR   STORAGE_ROOT "/.trash"
// "/sd" plus a FAT path: a long name can be up to 255 characters
#define PATH_LEN    300
// The page editor works on a textarea: beyond this it gets slow, and the
// whole text must fit in RAM for the check and for the secrets
#define EDIT_MAX    (64 * 1024)
#define CHUNK       4096
// Two relative paths (rename, copy) and the command
#define MAX_FRAME   (2 * PATH_LEN + 8)
// Beyond this, the folder shows up truncated (the page says so)
#define MAX_LIST    1000
// Maximum depth of a deleted or emptied folder: the recursion
// keeps one DIR open per level
#define MAX_DEPTH   16
// VM memory used only for compiling (POST /api/check)
#define CHECK_MEM   (80 * 1024)

extern const uint8_t page_gz_start[] asm("_binary_webfiles_html_gz_start");
extern const uint8_t page_gz_end[] asm("_binary_webfiles_html_gz_end");

// ---------------------------------------------------------------------------
// Paths

// Path sent by the page, relative to the SD root ("/",
// "/payloads/a.lua"), into a VFS path ("/sd", "/sd/payloads/a.lua").
// Rejected: "\" (also a separator for FatFs), ":" (FatFs volume prefix)
// and names ending with "." or a space: FatFs strips trailing dots and
// spaces, so "/.trash." would be the trash without looking like it, and
// ".." would escape the card.
static bool sd_path(const char *rel, char *out, size_t len)
{
    if (rel[0] != '/' || strpbrk(rel, "\\:")) return false;
    for (const char *p = rel + 1; *p;) {
        const char *q = strchr(p, '/');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        if (n > 0 && (p[n - 1] == '.' || p[n - 1] == ' ')) return false;
        if (!q) break;
        p = q + 1;
    }
    int w = snprintf(out, len, "%s%s", STORAGE_ROOT, rel);
    if (w >= (int)len) return false;
    // No trailing "/": "/sd/" and "/sd/x/" become "/sd" and "/sd/x"
    while (w > (int)strlen(STORAGE_ROOT) && out[w - 1] == '/') out[--w] = 0;
    return true;
}

static bool is_root(const char *path)
{
    return strcmp(path, STORAGE_ROOT) == 0;
}

// FAT is case-insensitive: "/.Trash" is the trash. The path comparisons
// below are therefore case-insensitive (ASCII only: enough for ".trash"
// and "config.lua").

// true if `path` is `dir` or is inside `dir`
static bool under(const char *path, const char *dir)
{
    size_t n = strlen(dir);
    return strncasecmp(path, dir, n) == 0 && (path[n] == 0 || path[n] == '/');
}

// true for the trash and what it contains: handled only with the trash
// commands, otherwise deleting an item would move it into itself
static bool in_trash(const char *path)
{
    return under(path, TRASH_DIR);
}

static bool is_config(const char *path)
{
    return strcasecmp(path, CONFIG_PATH) == 0;
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static const char *base_name(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// config.lua changed (saved, renamed, deleted, restored): the system
// reloads it when the app exits
static void touched(const char *path)
{
    if (is_config(path)) app_config_changed();
}

// Creates the missing folders of `path` (itself included)
static bool mkdir_p(const char *path)
{
    char p[PATH_LEN];
    strlcpy(p, path, sizeof(p));
    for (char *s = p + strlen(STORAGE_ROOT) + 1; *s; s++) {
        if (*s != '/') continue;
        *s = 0;
        if (mkdir(p, 0777) != 0 && errno != EEXIST) return false;
        *s = '/';
    }
    return mkdir(p, 0777) == 0 || errno == EEXIST;
}

// Deletes a file or a folder with all its contents. `path` is a PATH_LEN
// buffer that grows and shrinks during the descent.
static bool rm_tree(char *path, int depth)
{
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return unlink(path) == 0;
    if (depth >= MAX_DEPTH) return false;
    DIR *d = opendir(path);
    if (!d) return false;
    size_t len = strlen(path);
    bool ok = true;
    struct dirent *e;
    while (ok && !websrv_stopping() && (e = readdir(d))) {
        if (snprintf(path + len, PATH_LEN - len, "/%s", e->d_name) >= (int)(PATH_LEN - len)) ok = false;
        else ok = rm_tree(path, depth + 1);
        path[len] = 0;
    }
    closedir(d);
    return ok && rmdir(path) == 0;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in) return false;
    FILE *out = fopen(to, "wb");
    char *buf = malloc(CHUNK);
    bool ok = out && buf;
    size_t n;
    while (ok && !websrv_stopping() && (n = fread(buf, 1, CHUNK, in)) > 0) ok = fwrite(buf, 1, n, out) == n;
    ok = ok && !ferror(in) && !websrv_stopping();
    free(buf);
    fclose(in);
    if (out && fclose(out) != 0) ok = false;
    if (!ok && out) unlink(to); // no half-done copies
    return ok;
}

// ---------------------------------------------------------------------------
// Trash

// Folder of trash item <id>
static void slot_path(int id, char *out, size_t len)
{
    snprintf(out, len, "%s/%d", TRASH_DIR, id);
}

// Original path and date from the .origin file, item name from the only
// other entry in the folder. false if the item is not valid.
static bool slot_read(int id, char *origin, size_t olen, char *date, size_t dlen, char *item, size_t ilen)
{
    char p[PATH_LEN];
    slot_path(id, p, sizeof(p));
    size_t n = strlen(p);
    strlcpy(p + n, "/.origin", sizeof(p) - n);
    FILE *f = fopen(p, "r");
    if (!f) return false;
    origin[0] = date[0] = 0;
    if (fgets(origin, olen, f)) origin[strcspn(origin, "\r\n")] = 0;
    if (fgets(date, dlen, f)) date[strcspn(date, "\r\n")] = 0;
    fclose(f);
    p[n] = 0;
    DIR *d = opendir(p);
    if (!d) return false;
    item[0] = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".origin") != 0) {
            strlcpy(item, e->d_name, ilen);
            break;
        }
    }
    closedir(d);
    return origin[0] && item[0];
}

// Moves `path` to the trash, into a new numbered folder
static const char *trash_put(const char *path)
{
    if (!mkdir_p(TRASH_DIR)) return "Cannot create the trash folder";
    int id = 0;
    DIR *d = opendir(TRASH_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            int n = atoi(e->d_name);
            if (n > id) id = n;
        }
        closedir(d);
    }
    id++;
    char slot[40], p[PATH_LEN];
    slot_path(id, slot, sizeof(slot));
    if (mkdir(slot, 0777) != 0) return "Cannot create the trash folder";
    snprintf(p, sizeof(p), "%s/.origin", slot);
    FILE *f = fopen(p, "w");
    bool ok = f != NULL;
    if (f) {
        // The date only with a valid clock: otherwise it would be 1970
        char date[24] = "";
        time_t now = time(NULL);
        if (clock_valid()) strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now));
        ok = fprintf(f, "%s\n%s\n", path + strlen(STORAGE_ROOT), date) > 0;
        if (fclose(f) != 0) ok = false;
    }
    if (ok && snprintf(p, sizeof(p), "%s/%s", slot, base_name(path)) < (int)sizeof(p)) ok = rename(path, p) == 0;
    else ok = false;
    if (ok) return NULL;
    slot_path(id, p, sizeof(p));
    rm_tree(p, 0);
    return "Cannot move to the trash";
}

// Puts item <id> back in its place. If an item with the same name has
// appeared there in the meantime, " (2)", " (3)"... is added before the
// extension; the original folder, if missing, is recreated.
static const char *trash_restore(int id)
{
    char origin[PATH_LEN], date[24], item[256], from[PATH_LEN], to[PATH_LEN];
    if (!slot_read(id, origin, sizeof(origin), date, sizeof(date), item, sizeof(item))) return "No such trash item";
    if (!sd_path(origin, to, sizeof(to)) || is_root(to) || in_trash(to)) return "Bad original path";
    char slot[40];
    slot_path(id, slot, sizeof(slot));
    snprintf(from, sizeof(from), "%s/%s", slot, item);
    // Original folder
    char *sl = strrchr(to, '/');
    *sl = 0;
    if (!is_root(to) && !mkdir_p(to)) return "Cannot recreate the original folder";
    *sl = '/';
    if (exists(to)) {
        char stem[256];
        strlcpy(stem, sl + 1, sizeof(stem));
        // The extension of a file, not of a folder, and not a name starting with "."
        char *dot = is_dir(from) ? NULL : strrchr(stem, '.');
        if (dot == stem) dot = NULL;
        const char *ext = "";
        char extbuf[64] = "";
        if (dot && strlen(dot) < sizeof(extbuf)) {
            strlcpy(extbuf, dot, sizeof(extbuf));
            *dot = 0;
            ext = extbuf;
        }
        int n = 2;
        for (; n < 100; n++) {
            *sl = 0;
            int w = snprintf(to + strlen(to), sizeof(to) - strlen(to), "/%s (%d)%s", stem, n, ext);
            if (w < 0 || !exists(to)) break;
        }
        if (n == 100) return "Too many items with the same name";
    }
    if (rename(from, to) != 0) return "Cannot restore";
    touched(to);
    rm_tree(slot, 0);
    return NULL;
}

// ---------------------------------------------------------------------------
// WebSocket commands (httpd task)

static void reply_owned(httpd_req_t *req, char *msg)
{
    if (msg) websrv_reply(req, msg);
    free(msg);
}

static void reply_result(httpd_req_t *req, const char *err)
{
    jbuf_t b = {0};
    jb_begin(&b, err ? "error" : "ok");
    if (err) jb_kstr(&b, "text", err);
    reply_owned(req, jb_end(&b));
}

// One entry per message, then "end": no large buffers for full folders
static void send_list(httpd_req_t *req, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        reply_result(req, "Cannot open the folder");
        return;
    }
    char p[PATH_LEN];
    size_t len = strlcpy(p, dir, sizeof(p));
    int n = 0;
    struct dirent *e;
    while (!websrv_stopping() && n < MAX_LIST && (e = readdir(d))) {
        struct stat st;
        snprintf(p + len, sizeof(p) - len, "/%s", e->d_name);
        bool ok = stat(p, &st) == 0;
        jbuf_t b = {0};
        jb_begin(&b, "entry");
        jb_kstr(&b, "name", e->d_name);
        jb_kbool(&b, "dir", ok ? S_ISDIR(st.st_mode) : e->d_type == DT_DIR);
        jb_kint(&b, "size", ok ? (long long)st.st_size : 0);
        jb_kint(&b, "mtime", ok ? (long long)st.st_mtime : 0);
        reply_owned(req, jb_end(&b));
        n++;
    }
    closedir(d);
    jbuf_t b = {0};
    jb_begin(&b, "end");
    jb_kbool(&b, "full", n == MAX_LIST);
    reply_owned(req, jb_end(&b));
}

static void send_trash(httpd_req_t *req)
{
    DIR *d = opendir(TRASH_DIR);
    if (d) {
        struct dirent *e;
        while (!websrv_stopping() && (e = readdir(d))) {
            int id = atoi(e->d_name);
            char origin[PATH_LEN], date[24], item[256], p[PATH_LEN];
            if (id <= 0 || !slot_read(id, origin, sizeof(origin), date, sizeof(date), item, sizeof(item))) continue;
            snprintf(p, sizeof(p), "%s/%d/%s", TRASH_DIR, id, item);
            struct stat st;
            bool ok = stat(p, &st) == 0;
            jbuf_t b = {0};
            jb_begin(&b, "trash");
            jb_kint(&b, "id", id);
            jb_kstr(&b, "origin", origin);
            jb_kstr(&b, "date", date);
            jb_kbool(&b, "dir", ok && S_ISDIR(st.st_mode));
            jb_kint(&b, "size", ok ? (long long)st.st_size : 0);
            reply_owned(req, jb_end(&b));
        }
        closedir(d);
    }
    // Trash that does not exist yet: empty, not an error
    jbuf_t b = {0};
    jb_begin(&b, "end");
    jb_kbool(&b, "full", false);
    reply_owned(req, jb_end(&b));
}

static void send_space(httpd_req_t *req)
{
    uint64_t total, free_b;
    if (!storage_info(&total, &free_b)) {
        reply_result(req, "Cannot read the card size");
        return;
    }
    jbuf_t b = {0};
    jb_begin(&b, "space");
    jb_kint(&b, "total", (long long)total);
    jb_kint(&b, "free", (long long)free_b);
    reply_owned(req, jb_end(&b));
}

// Splits "<from>\t<to>" and converts both paths
static bool two_paths(char *arg, char *from, char *to)
{
    char *tab = strchr(arg, '\t');
    if (!tab) return false;
    *tab = 0;
    return sd_path(arg, from, PATH_LEN) && sd_path(tab + 1, to, PATH_LEN);
}

// The operations that write to the card. NULL on success, otherwise the
// message for the page.
static const char *write_op(char op, char *arg)
{
    char a[PATH_LEN], b[PATH_LEN];
    switch (op) {
    case 'k':
    case 'n': {
        if (!sd_path(arg, a, sizeof(a)) || is_root(a) || in_trash(a)) return "Bad path";
        if (exists(a)) return "Already exists";
        if (op == 'k') return mkdir(a, 0777) == 0 ? NULL : "Cannot create the folder";
        FILE *f = fopen(a, "w");
        if (!f) return "Cannot create the file";
        fclose(f);
        return NULL;
    }
    case 'v':
    case 'c': {
        if (!two_paths(arg, a, b) || is_root(a) || is_root(b) || in_trash(a) || in_trash(b)) return "Bad path";
        if (!exists(a)) return "Not found";
        // Same name with different case: for FAT it is the same item,
        // and rename only changes how it is written
        bool recase = op == 'v' && strcasecmp(a, b) == 0;
        if (exists(b) && !recase) return "Already exists";
        if (op == 'c') {
            if (is_dir(a)) return "Only files can be copied";
            if (!copy_file(a, b)) return "Copy failed";
            touched(b);
            return NULL;
        }
        // A folder inside itself would detach it from the tree, and FatFs
        // does not check for it: the FAT would be left with lost clusters
        if (under(b, a) && !recase) return "Cannot move a folder into itself";
        if (rename(a, b) != 0) return "Cannot rename";
        touched(a);
        touched(b);
        return NULL;
    }
    case 'd':
    case 'D': {
        if (!sd_path(arg, a, sizeof(a)) || is_root(a) || in_trash(a)) return "Bad path";
        if (!exists(a)) return "Not found";
        const char *err = op == 'd' ? trash_put(a) : rm_tree(a, 0) ? NULL : "Delete failed (partly deleted?)";
        // Even halfway: a full folder may have been partly emptied
        touched(a);
        return err;
    }
    case 'R':
    case 'E': {
        int id = atoi(arg);
        if (id <= 0) return "Bad trash item";
        if (op == 'R') return trash_restore(id);
        slot_path(id, a, sizeof(a));
        if (!exists(a)) return "No such trash item";
        return rm_tree(a, 0) ? NULL : "Delete failed";
    }
    case 'X':
        if (!exists(TRASH_DIR)) return NULL;
        strlcpy(a, TRASH_DIR, sizeof(a));
        return rm_tree(a, 0) ? NULL : "Delete failed";
    default: return "Unknown command";
    }
}

// Commands (paths relative to the SD root, starting with "/"):
//   l<folder>      listing: "entry" items, then "end"
//   f              total and free space ("space")
//   k<folder>      new folder
//   n<file>        new empty file
//   v<from>\t<to>   rename or move
//   c<from>\t<to>   copy (files only)
//   d<path>        to the trash
//   D<path>        delete permanently (full folders too)
//   L              trash listing: "trash" items, then "end"
//   R<id>          restore from the trash
//   E<id>          delete from the trash
//   X              empty the trash
// Every command has a single final reply ("end", "space", "ok" or
// "error"), in command order: the page matches them in a queue.
static void on_message(httpd_req_t *req, char *msg, size_t len)
{
    char path[PATH_LEN];
    switch (msg[0]) {
    case 'l':
        if (!sd_path(msg + 1, path, sizeof(path))) reply_result(req, "Bad path");
        else send_list(req, path);
        return;
    case 'L': send_trash(req); return;
    case 'f': send_space(req); return;
    default:
        // Lending the card to the host (console `usb`) waits for the end
        if (!storage_begin_write()) {
            reply_result(req, "The card is not available");
            return;
        }
        const char *err = write_op(msg[0], msg + 1);
        storage_end_write();
        reply_result(req, err);
        return;
    }
}

// ---------------------------------------------------------------------------
// HTTP routes (httpd task)

static esp_err_t send_status(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, text);
}

// Token and path from the query; on error the response has already been sent
static bool request_path(httpd_req_t *req, char *path)
{
    char rel[PATH_LEN];
    if (!websrv_authorized(req)) {
        send_status(req, "403 Forbidden", "Not authorized");
        return false;
    }
    if (!websrv_query(req, "path", rel, sizeof(rel)) || !sd_path(rel, path, PATH_LEN)) {
        send_status(req, "400 Bad Request", "Bad path");
        return false;
    }
    return true;
}

static bool query_flag(httpd_req_t *req, const char *key)
{
    char v[4];
    return websrv_query(req, key, v, sizeof(v)) && strcmp(v, "1") == 0;
}

// The whole file in a 0-terminated buffer (NULL if missing or too large)
static char *read_text(const char *path, size_t max, size_t *out_len)
{
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode) || (size_t)st.st_size > max) return NULL;
    FILE *f = fopen(path, "rb");
    char *buf = f ? malloc(st.st_size + 1) : NULL;
    size_t n = buf ? fread(buf, 1, st.st_size, f) : 0;
    if (f) fclose(f);
    if (!buf) return NULL;
    buf[n] = 0;
    if (out_len) *out_len = n;
    return buf;
}

// Request body, exactly `len` bytes
static bool recv_body(httpd_req_t *req, char *buf, size_t len)
{
    size_t got = 0;
    int timeouts = 0;
    while (got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (r <= 0) return false;
        got += r;
    }
    return true;
}

// config.lua from the page arrives with the secret placeholders in place of
// the values: puts them back taking them from the current file. NULL on
// error, with the message for the page in `why`.
static char *config_body(httpd_req_t *req, size_t *out_len, const char **why)
{
    *why = "Cannot read config.lua";
    size_t len = req->content_len;
    char *edited = len <= EDIT_MAX ? malloc(len + 1) : NULL;
    if (!edited || !recv_body(req, edited, len)) {
        free(edited);
        return NULL;
    }
    edited[len] = 0;
    char *orig = read_text(CONFIG_PATH, EDIT_MAX, NULL);
    bool unknown = false;
    char *text = secrets_unmask(edited, orig ? orig : "", &unknown);
    free(orig);
    free(edited);
    if (unknown) *why = "A <secret:N> placeholder does not match config.lua any more: reopen the file";
    if (text) *out_len = strlen(text);
    return text;
}

static esp_err_t file_get(httpd_req_t *req)
{
    char path[PATH_LEN];
    if (!request_path(req, path)) return ESP_OK;
    struct stat st;
    if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) return send_status(req, "404 Not Found", "Not found");
    if (st.st_size > EDIT_MAX) return send_status(req, "413 Content Too Large", "Too large for the editor");
    size_t len;
    char *text = read_text(path, EDIT_MAX, &len);
    if (!text) return send_status(req, "500 Internal Server Error", "Cannot read the file");
    // Secrets in clear text only on explicit request (page button)
    if (is_config(path) && !query_flag(req, "reveal")) {
        char *masked = secrets_mask(text);
        free(text);
        if (!masked) return send_status(req, "500 Internal Server Error", "Out of memory");
        text = masked;
        len = strlen(text);
    }
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send(req, text, len);
    free(text);
    return err;
}

// Save from the editor and upload. The content first goes to "<name>.part":
// if the connection drops halfway, the previous file stays intact.
static esp_err_t file_put(httpd_req_t *req)
{
    char path[PATH_LEN], tmp[PATH_LEN];
    if (!request_path(req, path)) return ESP_OK;
    if (is_root(path) || in_trash(path) || snprintf(tmp, sizeof(tmp), "%s.part", path) >= (int)sizeof(tmp)) {
        return send_status(req, "400 Bad Request", "Bad path");
    }
    struct stat st;
    bool there = stat(path, &st) == 0;
    if (there && S_ISDIR(st.st_mode)) return send_status(req, "409 Conflict", "A folder with this name exists");
    if (there && !query_flag(req, "overwrite")) return send_status(req, "409 Conflict", "Already exists");
    bool config = is_config(path);
    // Before the lock: config_body reads from the network, and unmask needs it whole
    size_t text_len = 0;
    const char *why = NULL;
    char *text = config ? config_body(req, &text_len, &why) : NULL;
    if (config && !text) return send_status(req, "400 Bad Request", why);
    char *buf = config ? NULL : malloc(CHUNK);
    if (!config && !buf) return send_status(req, "500 Internal Server Error", "Out of memory");
    if (!storage_begin_write()) {
        free(text);
        free(buf);
        return send_status(req, "503 Service Unavailable", "The card is not available");
    }
    FILE *f = fopen(tmp, "wb");
    bool ok = f != NULL;
    if (ok && config) ok = fwrite(text, 1, text_len, f) == text_len;
    // The rest in chunks: an upload can be larger than the RAM
    for (size_t left = req->content_len, timeouts = 0; ok && !config && left > 0;) {
        int r = httpd_req_recv(req, buf, left < CHUNK ? left : CHUNK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        ok = r > 0 && !websrv_stopping() && fwrite(buf, 1, r, f) == (size_t)r;
        if (ok) left -= r;
    }
    if (f && fclose(f) != 0) ok = false;
    // FAT does not replace on rename: the old one is removed first
    bool gone = false;
    if (ok && there) ok = gone = unlink(path) == 0;
    if (ok) ok = rename(tmp, path) == 0;
    // Old one already deleted and rename failed: the new content stays in .part
    if (!ok && !gone) unlink(tmp);
    if (ok) touched(path);
    storage_end_write();
    free(text);
    free(buf);
    if (!ok) return send_status(req, "500 Internal Server Error", gone ? "Rename failed: the file is saved as .part" : "Write failed");
    return send_status(req, "200 OK", "ok");
}

// Content-Disposition with the file name: filename= in ASCII for old
// clients, filename*= (RFC 5987) with the real name in UTF-8
static void disposition(const char *name, char *out, size_t len)
{
    int w = snprintf(out, len, "attachment; filename=\"");
    for (const unsigned char *s = (const unsigned char *)name; *s && w < (int)len - 40; s++) {
        out[w++] = (*s < 0x20 || *s >= 0x7F || *s == '"' || *s == '\\') ? '_' : *s;
    }
    w += snprintf(out + w, len - w, "\"; filename*=UTF-8''");
    for (const unsigned char *s = (const unsigned char *)name; *s && w < (int)len - 4; s++) {
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || strchr("-._~", *s)) {
            out[w++] = *s;
        } else {
            w += snprintf(out + w, len - w, "%%%02X", *s);
        }
    }
    out[w] = 0;
}

// httpd_send may write fewer bytes than requested
static bool send_all(httpd_req_t *req, const char *buf, size_t len)
{
    while (len > 0) {
        int n = httpd_send(req, buf, len);
        if (n <= 0) return false;
        buf += n;
        len -= n;
    }
    return true;
}

static esp_err_t download_get(httpd_req_t *req)
{
    char path[PATH_LEN];
    if (!request_path(req, path)) return ESP_OK;
    FILE *f = is_dir(path) ? NULL : fopen(path, "rb");
    if (!f) return send_status(req, "404 Not Found", "Not found");
    struct stat st;
    char *buf = malloc(CHUNK);
    if (!buf || fstat(fileno(f), &st) != 0) {
        const char *why = buf ? "Cannot read file" : "Out of memory";
        fclose(f);
        free(buf);
        return send_status(req, "500 Internal Server Error", why);
    }
    // 255 name characters, up to 3 bytes "%XX" each in filename*
    char disp[1100];
    disposition(base_name(path), disp, sizeof(disp));
    // Response written by hand: httpd_resp_send_chunk always sends
    // Transfer-Encoding: chunked, without a length, and the browser shows
    // neither the total nor the remaining time. With Content-Length the bytes
    // must be exactly the announced ones: if reading stops early (error,
    // shortened file, websrv_stop) ESP_FAIL is returned and httpd closes the
    // connection, so the browser marks the download as failed instead of
    // saving a truncated file.
    long long left = st.st_size;
    int hl = snprintf(buf, CHUNK,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/octet-stream\r\n"
                      "Content-Length: %lld\r\n"
                      "Content-Disposition: %s\r\n"
                      "Cache-Control: no-store\r\n\r\n",
                      left, disp);
    bool ok = hl < CHUNK && send_all(req, buf, hl);
    while (ok && left > 0 && !websrv_stopping()) {
        size_t n = fread(buf, 1, left < CHUNK ? (size_t)left : CHUNK, f);
        ok = n > 0 && send_all(req, buf, n);
        left -= n;
    }
    fclose(f);
    free(buf);
    return ok && left == 0 ? ESP_OK : ESP_FAIL;
}

// Script check from the editor: compiles it without running it (syntax)
// and, for injector scripts, reads the header. The text arrives in the
// body because it may not be saved yet.
static esp_err_t check_post(httpd_req_t *req)
{
    char path[PATH_LEN];
    if (!request_path(req, path)) return ESP_OK;
    size_t len = req->content_len;
    if (len > EDIT_MAX) return send_status(req, "413 Content Too Large", "Too large");
    char *text = malloc(len + 1);
    if (!text || !recv_body(req, text, len)) {
        free(text);
        return send_status(req, "400 Bad Request", "Cannot read the body");
    }
    text[len] = 0;

    jbuf_t b = {0};
    jb_begin(&b, "check");
    lua_State *L = luart_new(&(luart_opts_t){.mem_limit = CHECK_MEM});
    if (!L) {
        jb_kbool(&b, "ok", false);
        jb_kstr(&b, "error", "Not enough memory to check");
    } else {
        // Fixed chunk name: the message is "script:<line>: <error>"
        int st = luaL_loadbuffer(L, text, len, "=script");
        jb_kbool(&b, "ok", st == LUA_OK);
        if (st != LUA_OK) {
            const char *msg = lua_tostring(L, -1);
            if (!msg) msg = "error";
            int line = 0;
            if (strncmp(msg, "script:", 7) == 0) {
                line = atoi(msg + 7);
                const char *rest = strchr(msg + 7, ':');
                if (rest) msg = rest + 1 + (rest[1] == ' ');
            }
            jb_kstr(&b, "error", msg);
            jb_kint(&b, "line", line);
        }
        luart_close(L);
    }
    // "--- @key" header: only for injector scripts, not for libraries
    // nor for config.lua
    size_t n = strlen(path);
    if (under(path, PAYLOAD_DIR) && !under(path, PAYLOAD_LIB_DIR) && n > 4 && strcasecmp(path + n - 4, ".lua") == 0) {
        payload_meta_t *m = malloc(sizeof(*m));
        FILE *f = m ? fmemopen(text, len, "r") : NULL;
        if (f) {
            payload_parse_meta(f, path, m);
            fclose(f);
            jb_kstr(&b, "header", m->error);
        }
        free(m);
    }
    free(text);
    char *msg = jb_end(&b);
    if (!msg) return send_status(req, "500 Internal Server Error", "Out of memory");
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_sendstr(req, msg);
    free(msg);
    return err;
}

static const httpd_uri_t s_routes[] = {
    {.uri = "/api/file", .method = HTTP_GET, .handler = file_get},
    {.uri = "/api/file", .method = HTTP_PUT, .handler = file_put},
    {.uri = "/api/download", .method = HTTP_GET, .handler = download_get},
    {.uri = "/api/check", .method = HTTP_POST, .handler = check_post},
};

// ---------------------------------------------------------------------------
// App

static void webfiles_run(app_ctx_t *ctx)
{
    webui_draw_connecting(&icon_disk, ACCENT);
    esp_err_t err = app_net_acquire(ctx, NET_AUTO, 15000);
    if (err == ESP_ERR_NOT_FINISHED) return; // exit during network selection
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "WiFi failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web Files", msg, UI_ERR);
        return;
    }

    // webfiles.pin = false or secure network: no PIN, anyone on the dongle's network reads
    // and modifies the whole SD
    char pin[8];
    webui_make_pin("webfiles.pin", pin, sizeof(pin));
    websrv_config_t cfg = {
        .pin = pin,
        // The Lua parser of /api/check is recursive: same as the stack of
        // the script in the Injector
        .stack_size = 16384,
        .max_frame = MAX_FRAME,
        .on_message = on_message,
    };
    err = websrv_start(&cfg);
    bool started = err == ESP_OK;
    if (err == ESP_OK) err = websrv_add_page("/", "text/html", page_gz_start, page_gz_end - page_gz_start);
    if (err == ESP_OK) err = webtheme_register();
    for (size_t i = 0; err == ESP_OK && i < sizeof(s_routes) / sizeof(s_routes[0]); i++) {
        err = websrv_add_route(&s_routes[i]);
    }
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web Files", msg, UI_ERR);
        if (started) websrv_stop();
        return;
    }

    // QR page: credentials or address, see webui.h
    webui_qr_t qr = WEBUI_QR_INIT;
    char last_client[16] = "";
    bool led_stale = true;
    for (;;) {
        net_info_t ni;
        net_get_info(&ni);
        const char *client = websrv_client_ip();
        if (led_stale || strcmp(client, last_client) != 0) {
            strlcpy(last_client, client, sizeof(last_client));
            led_set(LED_LAYER_APP, *client ? LED_FX_SOLID : LED_FX_PULSE, *client ? UI_OK : ACCENT, 2000);
            led_stale = false;
        }
        webui_qr_update(&qr, &ni);
        if (ni.up) webui_draw_main(&ni, pin, qr.page, client, ACCENT);
        else webui_draw_connecting(&icon_disk, ACCENT);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK) webui_qr_toggle(&qr, &ni);
    }
    websrv_stop();
}

static void webfiles_subtitle(char *buf, size_t len)
{
    snprintf(buf, len, "%s", config_count("wifi") ? "WiFi" : "hotspot");
}

const app_desc_t webfiles_app = {
    .id = "webfiles",
    .name = "Web Files",
    .icon = &icon_disk,
    .accent = ACCENT,
    .requires = APP_REQ_SD | APP_REQ_NET,
    // No USB: the card stays with the firmware, which serves it to the page
    .usb = USB_PROFILE_NONE,
    .stack = 6144,
    .run = webfiles_run,
    .subtitle = webfiles_subtitle,
};
