#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hid.h"
#include "led.h"
#include "lwip/sockets.h"

static const char *TAG = "webhid";

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

#define MAX_FRAME      2048 // la pagina spezza gli incolla in blocchi più piccoli
#define MAX_TRIES      5
#define LOCKOUT_US     (30 * 1000000LL)
#define ACCENT         0x30B0F0

static httpd_handle_t s_server;
static char s_pin[8];
static int s_client_fd = -1;
static char s_client_ip[16];
static int s_tries;
static int64_t s_locked_until;

const char *web_client_ip(void)
{
    return s_client_ip;
}

// Confronto a tempo costante: il PIN protegge da altri dispositivi sulla rete.
// Con PIN vuoto (webhid.pin = false in config.lua) qualunque client entra.
static bool pin_ok(const char *given)
{
    size_t n = strlen(s_pin);
    if (n == 0) return true;
    if (strlen(given) != n) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) diff |= given[i] ^ s_pin[i];
    return diff == 0;
}

static void client_gone(httpd_handle_t hd, int fd)
{
    if (fd == s_client_fd) {
        hid_release_all();
        hid_mouse_buttons(0);
        s_client_fd = -1;
        s_client_ip[0] = 0;
        ESP_LOGI(TAG, "client disconnesso");
    }
    close(fd);
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

// Dice alla pagina se serve il PIN, senza consumare tentativi.
static esp_err_t cfg_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, s_pin[0] ? "pin" : "nopin");
}

static void reply(httpd_req_t *req, const char *msg)
{
    httpd_ws_frame_t f = {.type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)msg, .len = strlen(msg)};
    httpd_ws_send_frame(req, &f);
}

static uint32_t utf8_first(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    if (p[0] < 0x80) return p[0];
    if ((p[0] & 0xE0) == 0xC0) return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    if ((p[0] & 0xF0) == 0xE0) return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
}

static void authenticate(httpd_req_t *req, int fd, const char *given)
{
    int64_t now = esp_timer_get_time();
    if (now < s_locked_until) {
        reply(req, "locked");
        return;
    }
    if (!pin_ok(given)) {
        ESP_LOGW(TAG, "PIN errato (%d/%d)", s_tries + 1, MAX_TRIES);
        led_flash(0xF85149, 200);
        if (++s_tries >= MAX_TRIES) {
            s_tries = 0;
            s_locked_until = now + LOCKOUT_US;
        }
        reply(req, "no");
        return;
    }
    if (s_client_fd >= 0 && s_client_fd != fd) {
        reply(req, "busy");
        return;
    }
    s_tries = 0;
    s_client_fd = fd;
    // lwIP con IPv6 attivo restituisce un sockaddr_in6 anche per i client IPv4
    // (indirizzo "mapped": gli ultimi 4 byte sono l'IPv4). Leggerlo come
    // sockaddr_in dava 0.0.0.0.
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    s_client_ip[0] = 0;
    if (getpeername(fd, (struct sockaddr *)&addr, &len) == 0) {
        struct in_addr v4 = {0};
        if (addr.ss_family == AF_INET6) {
            memcpy(&v4, &((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr[12], 4);
        } else {
            v4 = ((struct sockaddr_in *)&addr)->sin_addr;
        }
        inet_ntoa_r(v4, s_client_ip, sizeof(s_client_ip));
    }
    ESP_LOGI(TAG, "client autenticato: %s", s_client_ip);
    reply(req, "ok");
    // Layout e OS correnti, per allineare i selettori della pagina
    static const char *const os_names[] = {"windows", "linux", "mac"};
    char st[48];
    snprintf(st, sizeof(st), "L%s %s", hid_get_layout()->name, os_names[hid_get_os()]);
    reply(req, st);
}

// Comandi (testo, un carattere di tipo + argomenti):
//   m dx dy     muove il mouse        b mask      stato dei pulsanti
//   w dy dx     rotella (dx facoltativo)
//   t testo     digita il testo nel layout dell'host
//   c mods ch   combinazione: modificatori + carattere (es. Ctrl+C)
//   k mods key  combinazione con usage HID grezzo (tasti speciali, F-key)
//   e n         n backspace di seguito (il live typing cancella così)
//   l nome      layout dell'host (it, us, colemak)
//   o nome      OS dell'host (windows, linux, mac): sceglie i ripieghi per i caratteri mancanti
static hid_result_t command(const char *m)
{
    int a = 0, b = 0;
    switch (m[0]) {
    case 'm':
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) return hid_mouse_move(a, b);
        break;
    case 'b':
        if (sscanf(m + 1, "%d", &a) == 1) return hid_mouse_buttons(a);
        break;
    case 'w':
        sscanf(m + 1, "%d %d", &a, &b);
        return hid_mouse_scroll(a, b);
    case 't': return hid_type(m + 1, NULL);
    case 'c':
        if (sscanf(m + 1, "%d", &a) == 1) {
            const char *ch = strchr(m + 1, ' ');
            uint8_t key, mods;
            if (ch && ch[1] && hid_char_key(utf8_first(ch + 1), &key, &mods)) return hid_chord(a | mods, &key, 1);
        }
        break;
    case 'e':
        if (sscanf(m + 1, "%d", &a) == 1 && a > 0 && a <= 200) {
            uint8_t bs = 0x2A; // Backspace
            for (int i = 0; i < a; i++) {
                hid_result_t r = hid_chord(0, &bs, 1);
                if (r != HID_OK) return r;
            }
        }
        break;
    case 'l': {
        const hid_layout_t *l = hid_layout_find(m + 1);
        if (l) hid_set_layout(l);
        break;
    }
    case 'o': {
        hid_os_t os;
        if (hid_os_parse(m + 1, &os)) hid_set_os(os);
        break;
    }
    case 'k':
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) {
            uint8_t key = b;
            return hid_chord(a, &key, b ? 1 : 0);
        }
        break;
    default: break;
    }
    return HID_OK;
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ESP_OK; // handshake completato
    int fd = httpd_req_to_sockfd(req);

    httpd_ws_frame_t f = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &f, 0);
    if (err != ESP_OK || f.len == 0 || f.len > MAX_FRAME) return err != ESP_OK ? err : ESP_FAIL;
    char *buf = malloc(f.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    f.payload = (uint8_t *)buf;
    err = httpd_ws_recv_frame(req, &f, f.len);
    if (err == ESP_OK && f.type == HTTPD_WS_TYPE_TEXT) {
        buf[f.len] = 0;
        if (buf[0] == 'p') {
            authenticate(req, fd, buf + 1);
        } else if (fd == s_client_fd) {
            led_flash(ACCENT, 15);
            if (command(buf) == HID_NO_HOST) reply(req, "nohost");
        }
        // Messaggi di un client non autenticato: ignorati
    }
    free(buf);
    return err;
}

esp_err_t web_start(const char *pin)
{
    strlcpy(s_pin, pin, sizeof(s_pin));
    s_client_fd = -1;
    s_client_ip[0] = 0;
    s_tries = 0;
    s_locked_until = 0;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144; // hid_type usa array di tasti sullo stack
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.close_fn = client_gone;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) return err;

    httpd_uri_t idx = {.uri = "/", .method = HTTP_GET, .handler = index_get};
    httpd_uri_t ws = {.uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true};
    httpd_uri_t cfg_uri = {.uri = "/cfg", .method = HTTP_GET, .handler = cfg_get};
    httpd_register_uri_handler(s_server, &cfg_uri);
    httpd_register_uri_handler(s_server, &idx);
    httpd_register_uri_handler(s_server, &ws);
    return ESP_OK;
}

void web_stop(void)
{
    if (s_server) httpd_stop(s_server);
    s_server = NULL;
    s_client_fd = -1;
    s_client_ip[0] = 0;
    hid_release_all();
    hid_mouse_buttons(0);
}
