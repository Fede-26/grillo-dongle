#include "websrv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "led.h"
#include "lwip/sockets.h"

static const char *TAG = "websrv";

#define MAX_TRIES  5
#define LOCKOUT_US (30 * 1000000LL)
// Pagina dell'app più i file del tema (webtheme): css, js, due font, logo
#define MAX_PAGES  8

static httpd_handle_t s_server;
static websrv_config_t s_cfg;
static char s_pin[8];
static int s_client_fd = -1;
static char s_client_ip[16];
// true durante websrv_stop: interrompe un lavoro lungo in corso nel task
// httpd, che altrimenti httpd_stop aspetterebbe fino alla fine.
static volatile bool s_stopping;

typedef struct {
    const char *type;
    const uint8_t *data;
    size_t len;
    unsigned flags; // WEBSRV_GZIP, WEBSRV_CACHE
} page_t;
static page_t s_pages[MAX_PAGES];
static int s_npages;

// Tentativi di PIN per indirizzo: con un contatore unico un altro host
// sulla LAN poteva tenere bloccato il proprietario sbagliando apposta.
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
    // Si ricicla la voce usata meno di recente, ma non una ancora bloccata:
    // ruotando gli indirizzi non si deve poter azzerare il blocco altrui.
    if (oldest->locked_until > esp_timer_get_time()) return oldest;
    *oldest = (peer_t){0};
    strlcpy(oldest->ip, ip, sizeof(oldest->ip));
    return oldest;
}

// lwIP con IPv6 attivo restituisce un sockaddr_in6 anche per i client IPv4
// (indirizzo "mapped": gli ultimi 4 byte sono l'IPv4). Leggerlo come
// sockaddr_in dava 0.0.0.0.
static void peer_ip(int fd, char *ip, size_t len)
{
    ip[0] = 0;
    struct sockaddr_storage addr;
    socklen_t alen = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &alen) != 0) return;
    struct in_addr v4 = {0};
    if (addr.ss_family == AF_INET6) {
        memcpy(&v4, &((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr[12], 4);
    } else {
        v4 = ((struct sockaddr_in *)&addr)->sin_addr;
    }
    inet_ntoa_r(v4, ip, len);
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

// Confronto a tempo costante: il PIN protegge da altri dispositivi sulla rete.
// Con PIN vuoto qualunque client entra.
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
        ESP_LOGI(TAG, "client disconnesso");
    }
    close(fd);
}

static esp_err_t page_get(httpd_req_t *req)
{
    const page_t *p = req->user_ctx;
    httpd_resp_set_type(req, p->type);
    if (p->flags & WEBSRV_GZIP) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    // In cache solo ciò che non cambia tra un firmware e l'altro (font):
    // pagine e tema devono arrivare nuovi dopo un aggiornamento.
    httpd_resp_set_hdr(req, "Cache-Control", (p->flags & WEBSRV_CACHE) ? "max-age=604800" : "no-store");
    return httpd_resp_send(req, (const char *)p->data, p->len);
}

// Dice alla pagina se serve il PIN, senza consumare tentativi.
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

// Gira nel task httpd, dove s_client_fd è aggiornato: se nel frattempo il
// client è cambiato, il messaggio non va a un socket che ora appartiene a
// qualcun altro (il numero di un socket chiuso viene riusato).
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
    // Copia: il chiamante può riusare il buffer prima che il task httpd invii
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
    // Posto occupato da un altro dispositivo: si risponde prima di guardare
    // il PIN, così durante una sessione non si possono provare PIN (prima
    // "busy" arrivava solo con il PIN giusto e confermava di averlo trovato).
    // Lo stesso indirizzo del client attivo è lo stesso dispositivo che si
    // riconnette mentre la vecchia connessione è ancora aperta a metà: con il
    // PIN giusto subentra alla sessione.
    if (s_client_fd >= 0 && s_client_fd != fd && strcmp(ip, s_client_ip) != 0) {
        websrv_reply(req, "busy");
        return;
    }
    if (!pin_ok(given)) {
        ESP_LOGW(TAG, "PIN errato da %s (%d/%d)", ip, peer->tries + 1, MAX_TRIES);
        led_flash(0xF85149, 200);
        if (++peer->tries >= MAX_TRIES) {
            peer->tries = 0;
            peer->locked_until = now + LOCKOUT_US;
        }
        websrv_reply(req, "no");
        return;
    }
    if (s_client_fd >= 0 && s_client_fd != fd) {
        ESP_LOGI(TAG, "%s si riconnette: chiudo la sessione precedente", ip);
        session_end();
        httpd_sess_trigger_close(req->handle, s_client_fd);
    }
    peer->tries = 0;
    s_client_fd = fd;
    strlcpy(s_client_ip, ip, sizeof(s_client_ip));
    ESP_LOGI(TAG, "client autenticato: %s", s_client_ip);
    websrv_reply(req, "ok");
    if (s_cfg.on_auth) s_cfg.on_auth(req);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK; // handshake completato
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
        // Messaggi di un client non autenticato: ignorati
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
    memset(s_peers, 0, sizeof(s_peers));
    s_npages = 0;
    s_stopping = false;

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.stack_size = cfg->stack_size;
    hc.max_open_sockets = 4;
    hc.lru_purge_enable = true;
    hc.close_fn = client_gone;
    // Keep-alive TCP: un client sparito senza chiudere (WiFi perso) viene
    // scoperto in circa 11 s invece di restare aperto a tempo indefinito.
    hc.keep_alive_enable = true;
    hc.keep_alive_idle = 5;
    hc.keep_alive_interval = 2;
    hc.keep_alive_count = 3;
    esp_err_t err = httpd_start(&s_server, &hc);
    if (err != ESP_OK) {
        s_server = NULL;
        return err;
    }

    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
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
    session_end();
}
