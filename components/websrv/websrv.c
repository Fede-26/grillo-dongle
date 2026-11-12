#include "websrv.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "led.h"
#include "lwip/sockets.h"

static const char *TAG = "websrv";

#define MAX_TRIES  5
#define LOCKOUT_US (30 * 1000000LL)
// App page plus the theme files (webtheme): css, js, two fonts, logo
#define MAX_PAGES  8

static httpd_handle_t s_server;
static websrv_config_t s_cfg;
static char s_pin[8];
static int s_client_fd = -1;
static char s_client_ip[16];
// Token for the authenticated client's HTTP routes (see websrv_authorized):
// new at every login, empty without a session.
static char s_token[33];
// true during websrv_stop: interrupts a long job running in the httpd task,
// which httpd_stop would otherwise wait for until it ends.
static volatile bool s_stopping;

typedef struct {
    const char *type;
    const uint8_t *data;
    size_t len;
    unsigned flags; // WEBSRV_GZIP, WEBSRV_CACHE
} page_t;
static page_t s_pages[MAX_PAGES];
static int s_npages;

// PIN attempts per address: with a single counter another host on the LAN
// could keep the owner locked out by failing on purpose.
#define MAX_PEERS 4
typedef struct {
    char ip[16];
    int tries;
    int64_t locked_until, last_us;
} peer_t;
static peer_t s_peers[MAX_PEERS];

static peer_t *peer_of(const char *ip)
{
    peer_t *oldest = &s_peers[0];
    for (int i = 0; i < MAX_PEERS; i++) {
        if (strcmp(s_peers[i].ip, ip) == 0) return &s_peers[i];
        if (s_peers[i].last_us < oldest->last_us) oldest = &s_peers[i];
    }
    // The least recently used entry is recycled, but not one still locked:
    // rotating addresses must not make it possible to reset someone else's lock.
    if (oldest->locked_until > esp_timer_get_time()) return oldest;
    *oldest = (peer_t){0};
    strlcpy(oldest->ip, ip, sizeof(oldest->ip));
    return oldest;
}

// lwIP with IPv6 enabled returns a sockaddr_in6 even for IPv4 clients
// ("mapped" address: the last 4 bytes are the IPv4). Reading it as
// sockaddr_in gave 0.0.0.0. `local`: the dongle's address on that socket
// instead of the client's.
static void sock_ip(int fd, bool local, char *ip, size_t len)
{
    ip[0] = 0;
    struct sockaddr_storage addr;
    socklen_t alen = sizeof(addr);
    int r = local ? getsockname(fd, (struct sockaddr *)&addr, &alen) : getpeername(fd, (struct sockaddr *)&addr, &alen);
    if (r != 0) return;
    struct in_addr v4 = {0};
    if (addr.ss_family == AF_INET6) {
        memcpy(&v4, &((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr[12], 4);
    } else {
        v4 = ((struct sockaddr_in *)&addr)->sin_addr;
    }
    inet_ntoa_r(v4, ip, len);
}

static void peer_ip(int fd, char *ip, size_t len)
{
    sock_ip(fd, false, ip, len);
}

const char *websrv_client_ip(void)
{
    return s_client_ip;
}

bool websrv_stopping(void)
{
    return s_stopping;
}

bool websrv_is_client(httpd_req_t *req)
{
    char ip[sizeof(s_client_ip)];
    peer_ip(httpd_req_to_sockfd(req), ip, sizeof(ip));
    return s_client_ip[0] && strcmp(ip, s_client_ip) == 0;
}

// Constant-time comparison: the PIN protects against other devices on the network.
// With an empty PIN any client gets in.
static bool pin_ok(const char *given)
{
    size_t n = strlen(s_pin);
    if (n == 0) return true;
    if (strlen(given) != n) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= given[i] ^ s_pin[i];
    return diff == 0;
}

static void session_end(void)
{
    if (s_cfg.on_session_end) s_cfg.on_session_end();
}

static void client_gone(httpd_handle_t hd, int fd)
{
    if (fd == s_client_fd) {
        session_end();
        s_client_fd = -1;
        s_client_ip[0] = 0;
        s_token[0] = 0;
        ESP_LOGI(TAG, "client disconnected");
    }
    close(fd);
}

static esp_err_t page_get(httpd_req_t *req)
{
    const page_t *p = req->user_ctx;
    httpd_resp_set_type(req, p->type);
    if (p->flags & WEBSRV_GZIP) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    // Cache only what does not change from one firmware to the next (fonts):
    // pages and theme must arrive fresh after an update.
    httpd_resp_set_hdr(req, "Cache-Control", (p->flags & WEBSRV_CACHE) ? "max-age=604800" : "no-store");
    return httpd_resp_send(req, (const char *)p->data, p->len);
}

// Tells the page whether the PIN is needed, without using up attempts.
static esp_err_t cfg_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, s_pin[0] ? "pin" : "nopin");
}

void websrv_reply(httpd_req_t *req, const char *msg)
{
    httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)msg, .len = strlen(msg)};
    httpd_ws_send_frame(req, &f);
}

typedef struct {
    int fd;
    size_t len;
    char text[];
} outgoing_t;

// Runs in the httpd task, where s_client_fd is up to date: if the client
// changed in the meantime, the message does not go to a socket that now
// belongs to someone else (the number of a closed socket gets reused).
static void send_work(void *arg)
{
    outgoing_t *o = arg;
    if (o->fd == s_client_fd && s_server) {
        httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)o->text, .len = o->len};
        httpd_ws_send_frame_async(s_server, o->fd, &f);
    }
    free(o);
}

esp_err_t websrv_send(const char *msg)
{
    int fd = s_client_fd;
    if (!s_server || fd < 0) return ESP_ERR_INVALID_STATE;
    size_t len = strlen(msg);
    // Copy: the caller may reuse the buffer before the httpd task sends
    outgoing_t *o = malloc(sizeof(*o) + len);
    if (!o) return ESP_ERR_NO_MEM;
    o->fd = fd;
    o->len = len;
    memcpy(o->text, msg, len);
    esp_err_t err = httpd_queue_work(s_server, send_work, o);
    if (err != ESP_OK) free(o);
    return err;
}

static void authenticate(httpd_req_t *req, int fd, const char *given)
{
    char ip[sizeof(s_client_ip)];
    peer_ip(fd, ip, sizeof(ip));
    int64_t now = esp_timer_get_time();
    peer_t *peer = peer_of(ip);
    peer->last_us = now;
    if (now < peer->locked_until) {
        websrv_reply(req, "locked");
        return;
    }
    // Slot taken by another device: reply before looking at the PIN, so PINs
    // cannot be tried during a session (before, "busy" came only with the
    // right PIN and confirmed it had been found).
    // The same address as the active client is the same device reconnecting
    // while the old connection is still half open: with the right PIN it
    // takes over the session.
    if (s_client_fd >= 0 && s_client_fd != fd && strcmp(ip, s_client_ip) != 0) {
        websrv_reply(req, "busy");
        return;
    }
    if (!pin_ok(given)) {
        ESP_LOGW(TAG, "wrong PIN from %s (%d/%d)", ip, peer->tries + 1, MAX_TRIES);
        led_flash(0xF85149, 200);
        if (++peer->tries >= MAX_TRIES) {
            peer->tries = 0;
            peer->locked_until = now + LOCKOUT_US;
        }
        websrv_reply(req, "no");
        return;
    }
    if (s_client_fd >= 0 && s_client_fd != fd) {
        ESP_LOGI(TAG, "%s reconnecting: closing the previous session", ip);
        session_end();
        httpd_sess_trigger_close(req->handle, s_client_fd);
    }
    peer->tries = 0;
    s_client_fd = fd;
    strlcpy(s_client_ip, ip, sizeof(s_client_ip));
    ESP_LOGI(TAG, "client authenticated: %s", s_client_ip);
    websrv_reply(req, "ok");
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));
    for (int i = 0; i < 16; i++) snprintf(s_token + 2 * i, 3, "%02x", rnd[i]);
    char tok[40];
    snprintf(tok, sizeof(tok), "T%s", s_token);
    websrv_reply(req, tok);
    if (s_cfg.on_auth) s_cfg.on_auth(req);
}

// Before the WebSocket handshake: the Origin header must be a page served by
// the dongle. A WebSocket does not follow the same-origin policy, so without
// this check any site open in a browser on the same network could connect
// and, with the PIN disabled (trusted network or <app>.pin = false), act.
// The comparison is with the socket's local IP and not with the Host header,
// which with DNS rebinding belongs to the hostile site. Without Origin it is
// accepted: all browsers send it, while a non-browser client (the python
// test scripts) is already on the network and does not need a bridge
// site.
static esp_err_t ws_origin_check(httpd_req_t *req)
{
    size_t n = httpd_req_get_hdr_value_len(req, "Origin");
    if (n == 0) return ESP_OK;
    char origin[48] = "", ip[16], want[32];
    if (n < sizeof(origin)) httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof(origin));
    sock_ip(httpd_req_to_sockfd(req), true, ip, sizeof(ip));
    int w = snprintf(want, sizeof(want), "http://%s", ip);
    // Port 80 is implicit, but a browser may write it
    if (*ip && strncmp(origin, want, w) == 0 && (origin[w] == 0 || strcmp(origin + w, ":80") == 0)) return ESP_OK;
    ESP_LOGW(TAG, "WebSocket refused: Origin \"%s\" (expected %s)", origin, want);
    return ESP_FAIL;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK; // handshake completed
    int fd = httpd_req_to_sockfd(req);

    httpd_ws_frame_t f = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &f, 0);
    if (err != ESP_OK || f.len == 0 || f.len > s_cfg.max_frame) return err != ESP_OK ? err : ESP_FAIL;
    char *buf = malloc(f.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    f.payload = (uint8_t *)buf;
    err = httpd_ws_recv_frame(req, &f, f.len);
    if (err == ESP_OK && f.type == HTTPD_WS_TYPE_TEXT) {
        buf[f.len] = 0;
        if (buf[0] == 'p') authenticate(req, fd, buf + 1);
        else if (fd == s_client_fd) s_cfg.on_message(req, buf, f.len);
        // Messages from an unauthenticated client: ignored
    }
    free(buf);
    return err;
}

esp_err_t websrv_start(const websrv_config_t *cfg)
{
    s_cfg = *cfg;
    strlcpy(s_pin, cfg->pin, sizeof(s_pin));
    s_client_fd = -1;
    s_client_ip[0] = 0;
    s_token[0] = 0;
    memset(s_peers, 0, sizeof(s_peers));
    s_npages = 0;
    s_stopping = false;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = cfg->stack_size;
    hc.max_open_sockets = 4;
    // /ws, /cfg, the static files (MAX_PAGES) and the app routes: the default
    // of 8 was barely enough for page and theme
    hc.max_uri_handlers = 2 + MAX_PAGES + 8;
    hc.lru_purge_enable = true;
    hc.close_fn = client_gone;
    // TCP keep-alive: a client that vanished without closing (WiFi lost) is
    // detected in about 11 s instead of staying open indefinitely.
    hc.keep_alive_enable = true;
    hc.keep_alive_idle = 5;
    hc.keep_alive_interval = 2;
    hc.keep_alive_count = 3;
    esp_err_t err = httpd_start(&s_server, &hc);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }

    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
        .ws_pre_handshake_cb = ws_origin_check,
    };
    httpd_uri_t cfg_uri = {.uri = "/cfg", .method = HTTP_GET, .handler = cfg_get};
    httpd_register_uri_handler(s_server, &cfg_uri);
    httpd_register_uri_handler(s_server, &ws);
    return ESP_OK;
}

esp_err_t websrv_add_page(const char *uri, const char *type, const uint8_t *gz, size_t len)
{
    return websrv_add_file(uri, type, gz, len, WEBSRV_GZIP);
}

esp_err_t websrv_add_file(const char *uri, const char *type, const uint8_t *data, size_t len, unsigned flags)
{
    if (!s_server) return ESP_ERR_INVALID_STATE;
    if (s_npages >= MAX_PAGES) return ESP_ERR_NO_MEM;
    page_t *p = &s_pages[s_npages++];
    *p = (page_t){type, data, len, flags};
    httpd_uri_t u = {.uri = uri, .method = HTTP_GET, .handler = page_get, .user_ctx = p};
    return httpd_register_uri_handler(s_server, &u);
}

// Decodes %XX and '+' in place
static void url_decode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(hex, NULL, 16);
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

bool websrv_query(httpd_req_t *req, const char *key, char *out, size_t len)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0) return false;
    char *q = malloc(qlen + 1);
    if (!q) return false;
    bool ok = httpd_req_get_url_query_str(req, q, qlen + 1) == ESP_OK && httpd_query_key_value(q, key, out, len) == ESP_OK;
    free(q);
    if (ok) url_decode(out);
    return ok;
}

bool websrv_authorized(httpd_req_t *req)
{
    char given[sizeof(s_token)] = "";
    if (httpd_req_get_hdr_value_str(req, "X-Grillo-Token", given, sizeof(given)) != ESP_OK) {
        websrv_query(req, "t", given, sizeof(given));
    }
    // Constant-time comparison, as for the PIN
    size_t n = strlen(s_token);
    if (n == 0 || strlen(given) != n) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= given[i] ^ s_token[i];
    return diff == 0;
}

esp_err_t websrv_add_route(const httpd_uri_t *uri)
{
    if (!s_server) return ESP_ERR_INVALID_STATE;
    return httpd_register_uri_handler(s_server, uri);
}

void websrv_stop(void)
{
    s_stopping = true;
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
    s_client_fd = -1;
    s_client_ip[0] = 0;
    s_token[0] = 0;
    session_end();
}
