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
#define MAX_ERASE      500
#define LOCKOUT_US     (30 * 1000000LL)
#define ACCENT         0x4070FF

static httpd_handle_t s_server;
static char s_pin[8];
static int s_client_fd = -1;
static char s_client_ip[16];
// true durante web_stop: interrompe una digitazione lunga in corso nel task
// httpd, che altrimenti httpd_stop aspetterebbe fino alla fine.
static volatile bool s_stopping;

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

// Lunghezza in byte del carattere UTF-8 in `s`, 0 se la sequenza non è
// valida o è troncata (fine del frame): così non si legge oltre il buffer.
static int utf8_len(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

static uint32_t utf8_first(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    switch (utf8_len(s)) {
    case 1: return p[0];
    case 2: return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3: return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    case 4: return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    default: return 0xFFFD;
    }
}

// Digita il testo un carattere alla volta. Un carattere che il layout non ha
// diventa '?': prima la digitazione si fermava lì e perdeva il resto del
// blocco, e nel live typing la pagina contava un carattere che sul PC non
// c'era (i backspace successivi ne cancellavano uno di troppo).
static hid_result_t type_text(const char *s, int *skipped)
{
    char one[5];
    while (*s) {
        int n = utf8_len(s);
        if (n == 0) n = 1; // byte non valido: lo si tratta come carattere a sé
        memcpy(one, s, n);
        one[n] = 0;
        s += n;
        hid_result_t r = hid_type(one, NULL);
        if (r == HID_UNTYPEABLE) {
            (*skipped)++;
            r = hid_type("?", NULL);
            if (r == HID_UNTYPEABLE) r = HID_OK;
        }
        if (r != HID_OK) return r;
    }
    return HID_OK;
}

static bool keep_typing(void *arg)
{
    return !s_stopping;
}

static int clamp(int v, int lim)
{
    return v < -lim ? -lim : (v > lim ? lim : v);
}

static void authenticate(httpd_req_t *req, int fd, const char *given)
{
    // lwIP con IPv6 attivo restituisce un sockaddr_in6 anche per i client IPv4
    // (indirizzo "mapped": gli ultimi 4 byte sono l'IPv4). Leggerlo come
    // sockaddr_in dava 0.0.0.0.
    char ip[sizeof(s_client_ip)] = "";
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, (struct sockaddr *)&addr, &len) == 0) {
        struct in_addr v4 = {0};
        if (addr.ss_family == AF_INET6) {
            memcpy(&v4, &((struct sockaddr_in6 *)&addr)->sin6_addr.s6_addr[12], 4);
        } else {
            v4 = ((struct sockaddr_in *)&addr)->sin_addr;
        }
        inet_ntoa_r(v4, ip, sizeof(ip));
    }
    int64_t now = esp_timer_get_time();
    peer_t *peer = peer_of(ip);
    peer->last_us = now;
    if (now < peer->locked_until) {
        reply(req, "locked");
        return;
    }
    // Posto occupato da un altro dispositivo: si risponde prima di guardare
    // il PIN, così durante una sessione non si possono provare PIN (prima
    // "busy" arrivava solo con il PIN giusto e confermava di averlo trovato).
    // Lo stesso indirizzo del client attivo è lo stesso dispositivo che si
    // riconnette mentre la vecchia connessione è ancora aperta a metà: con il
    // PIN giusto subentra alla sessione.
    if (s_client_fd >= 0 && s_client_fd != fd && strcmp(ip, s_client_ip) != 0) {
        reply(req, "busy");
        return;
    }
    if (!pin_ok(given)) {
        ESP_LOGW(TAG, "PIN errato da %s (%d/%d)", ip, peer->tries + 1, MAX_TRIES);
        led_flash(0xF85149, 200);
        if (++peer->tries >= MAX_TRIES) {
            peer->tries = 0;
            peer->locked_until = now + LOCKOUT_US;
        }
        reply(req, "no");
        return;
    }
    if (s_client_fd >= 0 && s_client_fd != fd) {
        ESP_LOGI(TAG, "%s si riconnette: chiudo la sessione precedente", ip);
        hid_release_all();
        hid_mouse_buttons(0);
        httpd_sess_trigger_close(req->handle, s_client_fd);
    }
    peer->tries = 0;
    s_client_fd = fd;
    strlcpy(s_client_ip, ip, sizeof(s_client_ip));
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
//   e n         n backspace di seguito, 1..MAX_ERASE (il live typing cancella così)
//   l nome      layout dell'host (it, us, colemak)
//   o nome      OS dell'host (windows, linux, mac): sceglie i ripieghi per i caratteri mancanti
static hid_result_t command(const char *m, int *skipped)
{
    int a = 0, b = 0;
    switch (m[0]) {
    case 'm':
        // Un dx enorme teneva occupato il task httpd per ore di report
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) return hid_mouse_move(clamp(a, 2000), clamp(b, 2000));
        break;
    case 'b':
        if (sscanf(m + 1, "%d", &a) == 1) return hid_mouse_buttons(a);
        break;
    case 'w':
        sscanf(m + 1, "%d %d", &a, &b);
        return hid_mouse_scroll(clamp(a, 127), clamp(b, 127));
    case 't': return type_text(m + 1, skipped);
    case 'c':
        if (sscanf(m + 1, "%d", &a) == 1) {
            const char *ch = strchr(m + 1, ' ');
            uint8_t key, mods;
            if (ch && ch[1] && hid_char_key(utf8_first(ch + 1), &key, &mods)) return hid_chord(a | mods, &key, 1);
        }
        break;
    case 'e':
        // La pagina spezza le cancellazioni lunghe in blocchi da MAX_ERASE
        if (sscanf(m + 1, "%d", &a) == 1 && a > 0 && a <= MAX_ERASE) {
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
            int skipped = 0;
            hid_result_t r = command(buf, &skipped);
            // "hostok" solo al ritorno del PC, non a ogni comando: toglie
            // l'avviso "PC not ready" dalla pagina, che prima restava per sempre.
            static bool host_bad;
            if (r == HID_NO_HOST) reply(req, "nohost");
            else if (r == HID_OK && host_bad) reply(req, "hostok");
            host_bad = r == HID_NO_HOST;
            if (skipped) {
                char msg[24];
                snprintf(msg, sizeof(msg), "untypeable %d", skipped);
                reply(req, msg);
            }
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
    memset(s_peers, 0, sizeof(s_peers));
    s_stopping = false;
    hid_set_poll(keep_typing, NULL);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144; // hid_type usa array di tasti sullo stack
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.close_fn = client_gone;
    // Keep-alive TCP: un client sparito senza chiudere (WiFi perso) viene
    // scoperto in circa 11 s invece di restare aperto a tempo indefinito.
    cfg.keep_alive_enable = true;
    cfg.keep_alive_idle = 5;
    cfg.keep_alive_interval = 2;
    cfg.keep_alive_count = 3;
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
    s_stopping = true;
    if (s_server) httpd_stop(s_server);
    hid_set_poll(NULL, NULL);
    s_server = NULL;
    s_client_fd = -1;
    s_client_ip[0] = 0;
    hid_release_all();
    hid_mouse_buttons(0);
}
