// Web Injector: gli script dell'injector scelti e seguiti da un telefono.
//
// Come il Web HID: lease di rete (rete nota o hotspot), server con PIN, QR
// code e PIN sul display. Dalla pagina si sfogliano gli script di
// /sd/payloads (cartelle, per OS, per tag), si legge la scheda e si avvia.
// Lo script gira nel task dell'app con lo stesso nucleo dell'Injector
// (injector_core): display e pulsante restano attivi, e nei dialoghi vince
// la prima risposta, dal pulsante o dalla pagina (APP_EV_ANSWER). Se la
// pagina si chiude lo script continua; alla riconnessione la pagina riceve
// lo stato e l'eventuale dialogo aperto.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "engine.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "icons.h"
#include "led.h"
#include "net.h"
#include "payload.h"
#include "ui.h"
#include "usbdev.h"
#include "webinjector.h"
#include "websrv.h"
#include "webui.h"

#define ACCENT      0xB060FF
#define MAX_ENTRIES 48
// Il messaggio più lungo dalla pagina è un comando con un percorso
#define MAX_FRAME   (PAYLOAD_PATH_MAX + 16)

extern const uint8_t page_gz_start[] asm("_binary_webinjector_html_gz_start");
extern const uint8_t page_gz_end[] asm("_binary_webinjector_html_gz_end");

static const char *const OS_NAMES[] = {"windows", "linux", "mac"};

typedef enum { PHASE_IDLE, PHASE_RUNNING, PHASE_DONE } phase_t;
// CHOICE: menu, conferma ed esito, a cui si risponde con APP_EV_ANSWER.
// BUTTON: ui.wait_button, a cui la pagina risponde con un gesto.
typedef enum { DLG_NONE, DLG_CHOICE, DLG_BUTTON } dlg_kind_t;

// Condiviso tra il task dell'app (agganci dello script) e il task httpd
// (comandi della pagina, riconnessione): sempre sotto s_lock.
static SemaphoreHandle_t s_lock;
static app_ctx_t *s_ctx;
static phase_t s_phase;
static char s_pending[PAYLOAD_PATH_MAX]; // script chiesto dalla pagina
static int s_dialog_id, s_next_id;       // id del dialogo aperto, 0 se nessuno
static dlg_kind_t s_dialog_kind;
// Ultimi stato e dialogo, rimandati a ogni riconnessione della pagina
static char *s_state_msg, *s_dialog_msg;

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

// ---------------------------------------------------------------------------
// JSON

typedef struct {
    char *p;
    size_t len, cap;
    bool fail; // memoria finita: il messaggio non viene mandato
} jbuf_t;

static void jb_raw(jbuf_t *b, const char *s, size_t n)
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

static void jb_lit(jbuf_t *b, const char *s)
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

// Stringa JSON tra virgolette. Il browser chiude il WebSocket se un frame di
// testo non è UTF-8 valido: nomi di file e testi degli script possono non
// esserlo, quindi i byte non validi diventano "?".
static void jb_str(jbuf_t *b, const char *s)
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

// "chiave":"valore", con la virgola davanti se non è il primo campo
static void jb_kstr(jbuf_t *b, const char *key, const char *val)
{
    jb_lit(b, ",\"");
    jb_lit(b, key);
    jb_lit(b, "\":");
    jb_str(b, val);
}

static void jb_kint(jbuf_t *b, const char *key, int val)
{
    char t[40];
    snprintf(t, sizeof(t), ",\"%s\":%d", key, val);
    jb_lit(b, t);
}

static void jb_kbool(jbuf_t *b, const char *key, bool val)
{
    char t[40];
    snprintf(t, sizeof(t), ",\"%s\":%s", key, val ? "true" : "false");
    jb_lit(b, t);
}

// Apre l'oggetto con il tipo del messaggio: {"t":"..."
static void jb_begin(jbuf_t *b, const char *type)
{
    jb_lit(b, "{\"t\":");
    jb_str(b, type);
}

// Chiude l'oggetto e restituisce la stringa (da liberare), NULL senza memoria
static char *jb_end(jbuf_t *b)
{
    jb_lit(b, "}");
    if (!b->fail) return b->p;
    free(b->p);
    return NULL;
}

// ---------------------------------------------------------------------------
// Agganci dello script (task dell'app)

// NULL (memoria finita) non si manda
static void send_msg(const char *msg)
{
    if (msg) websrv_send(msg);
}

// Sostituisce il messaggio da rimandare alla riconnessione e lo invia
static void keep_and_send(char **slot, char *msg)
{
    lock();
    free(*slot);
    *slot = msg;
    if (msg) websrv_send(msg);
    unlock();
}

static void hook_state(void *arg, const inj_run_t *r)
{
    jbuf_t b = {0};
    jb_begin(&b, "state");
    jb_kstr(&b, "name", r->meta->name[0] ? r->meta->name : "Script");
    jb_kstr(&b, "status", r->status);
    jb_kint(&b, "p", r->progress);
    jb_kint(&b, "max", r->progress_max);
    jb_kbool(&b, "wait", r->waiting_host);
    keep_and_send(&s_state_msg, jb_end(&b));
}

static void hook_log(void *arg, const char *text)
{
    jbuf_t b = {0};
    jb_begin(&b, "log");
    jb_kstr(&b, "text", text);
    char *msg = jb_end(&b);
    send_msg(msg);
    free(msg);
}

// Apre un dialogo: nuovo id, messaggio conservato per la riconnessione
static void open_dialog(dlg_kind_t kind, jbuf_t *b)
{
    lock();
    s_dialog_id = ++s_next_id;
    s_dialog_kind = kind;
    jb_kint(b, "id", s_dialog_id);
    unlock();
    keep_and_send(&s_dialog_msg, jb_end(b));
}

static void hook_menu(void *arg, const char *title, const char *const *items, int n)
{
    jbuf_t b = {0};
    jb_begin(&b, "menu");
    jb_kstr(&b, "title", title);
    jb_lit(&b, ",\"items\":[");
    for (int i = 0; i < n; i++) {
        if (i) jb_lit(&b, ",");
        jb_str(&b, items[i]);
    }
    jb_lit(&b, "]");
    open_dialog(DLG_CHOICE, &b);
}

static void hook_confirm(void *arg, const char *title, const char *text, bool yes)
{
    jbuf_t b = {0};
    jb_begin(&b, "confirm");
    jb_kstr(&b, "title", title);
    jb_kstr(&b, "text", text);
    jb_kbool(&b, "yes", yes);
    open_dialog(DLG_CHOICE, &b);
}

static void hook_wait_button(void *arg)
{
    jbuf_t b = {0};
    jb_begin(&b, "button");
    open_dialog(DLG_BUTTON, &b);
}

// L'esito resta sul display finché qualcuno risponde: per la pagina è un
// dialogo con un solo pulsante.
static void hook_done(void *arg, const inj_run_t *r)
{
    lock();
    s_phase = PHASE_DONE; // da qui la pagina non può più chiedere l'abort
    unlock();
    jbuf_t b = {0};
    jb_begin(&b, "done");
    jb_kstr(&b, "name", r->meta->name[0] ? r->meta->name : "Script");
    jb_kbool(&b, "ok", r->ok);
    jb_kbool(&b, "aborted", r->aborted && !r->timed_out);
    jb_kbool(&b, "timeout", r->timed_out);
    jb_kstr(&b, "error", r->error);
    open_dialog(DLG_CHOICE, &b);
}

static void hook_dialog_end(void *arg)
{
    jbuf_t b = {0};
    lock();
    jb_begin(&b, "close");
    jb_kint(&b, "id", s_dialog_id);
    s_dialog_id = 0;
    s_dialog_kind = DLG_NONE;
    free(s_dialog_msg);
    s_dialog_msg = NULL;
    // Chiuso l'esito, lo script è finito del tutto
    bool finished = s_phase == PHASE_DONE;
    if (finished) {
        s_phase = PHASE_IDLE;
        free(s_state_msg);
        s_state_msg = NULL;
    }
    char *msg = jb_end(&b);
    send_msg(msg);
    free(msg);
    if (finished) websrv_send("{\"t\":\"idle\"}");
    unlock();
}

static const inj_hooks_t s_hooks = {
    .state = hook_state,
    .log = hook_log,
    .menu = hook_menu,
    .confirm = hook_confirm,
    .wait_button = hook_wait_button,
    .done = hook_done,
    .dialog_end = hook_dialog_end,
};

// ---------------------------------------------------------------------------
// Comandi della pagina (task httpd)

static void reply_owned(httpd_req_t *req, char *msg)
{
    if (msg) websrv_reply(req, msg);
    free(msg);
}

static void reply_error(httpd_req_t *req, const char *text)
{
    jbuf_t b = {0};
    jb_begin(&b, "error");
    jb_kstr(&b, "text", text);
    reply_owned(req, jb_end(&b));
}

// Percorso relativo a /sd/payloads mandato dalla pagina: niente percorsi
// assoluti né "..", così la pagina non legge fuori dalla cartella.
static bool full_path(const char *rel, char *out, size_t len)
{
    if (rel[0] == '/') return false;
    for (const char *p = rel; *p;) {
        const char *q = strchr(p, '/');
        size_t n = q ? (size_t)(q - p) : strlen(p);
        if (n == 2 && p[0] == '.' && p[1] == '.') return false;
        if (!q) break;
        p = q + 1;
    }
    int w = rel[0] ? snprintf(out, len, "%s/%s", PAYLOAD_DIR, rel) : snprintf(out, len, "%s", PAYLOAD_DIR);
    return w < (int)len;
}

// Una voce per messaggio, poi "end": niente buffer grandi per cartelle piene.
// Per gli script layout e OS sono quelli effettivi (intestazione o
// config.lua), e con la scansione c'è anche il percorso relativo.
static void send_entries(httpd_req_t *req, const char *dir, bool scan)
{
    payload_entry_t *e = malloc(MAX_ENTRIES * sizeof(*e));
    if (!e) {
        reply_error(req, "Out of memory");
        return;
    }
    int n = scan ? payload_scan(dir, e, MAX_ENTRIES) : payload_list(dir, e, MAX_ENTRIES);
    if (n < 0) {
        free(e);
        reply_error(req, "No /payloads folder on the SD");
        return;
    }
    for (int i = 0; i < n && !websrv_stopping(); i++) {
        jbuf_t b = {0};
        jb_begin(&b, "entry");
        jb_kstr(&b, "name", e[i].name);
        jb_kstr(&b, "title", e[i].title);
        jb_kbool(&b, "dir", e[i].is_dir);
        if (!e[i].is_dir) {
            jb_kstr(&b, "layout", payload_layout(&e[i]));
            jb_kstr(&b, "os", OS_NAMES[payload_os(&e[i])]);
            jb_kstr(&b, "tags", e[i].tags);
        }
        if (scan) jb_kstr(&b, "path", e[i].path + strlen(PAYLOAD_DIR "/"));
        reply_owned(req, jb_end(&b));
    }
    jbuf_t b = {0};
    jb_begin(&b, "end");
    jb_kbool(&b, "scan", scan);
    jb_kbool(&b, "full", n == MAX_ENTRIES);
    reply_owned(req, jb_end(&b));
    free(e);
}

static void send_meta(httpd_req_t *req, const char *path, const char *rel)
{
    payload_meta_t *m = malloc(sizeof(*m));
    if (!m || !payload_read_meta(path, m)) {
        free(m);
        reply_error(req, "Cannot read the script");
        return;
    }
    jbuf_t b = {0};
    jb_begin(&b, "meta");
    jb_kstr(&b, "path", rel);
    jb_kstr(&b, "name", m->name);
    jb_kstr(&b, "desc", m->desc);
    jb_kstr(&b, "layout", m->layout[0] ? m->layout : config_str("keyboard.layout", "us"));
    jb_kstr(&b, "os", m->os[0] ? m->os : config_str("keyboard.os", "windows"));
    jb_kstr(&b, "tags", m->tags);
    jb_kint(&b, "timeout", m->timeout);
    jb_kstr(&b, "error", m->error);
    reply_owned(req, jb_end(&b));
    free(m);
}

// Comandi:
//   l<cartella>  elenco di una cartella ("" = /payloads)
//   s            tutti gli script, per le viste per OS e per tag
//   m<script>    scheda (intestazione)
//   r<script>    esegui
//   a<id>:<n>    risposta al dialogo <id>: indice, 0/1 per no/sì, 0 per
//                l'esito; per ui.wait_button 0 click, 1 doppio, 2 lungo
//   x            interrompi lo script
static void on_message(httpd_req_t *req, char *msg, size_t len)
{
    char path[PAYLOAD_PATH_MAX];
    switch (msg[0]) {
    case 'l':
    case 's':
        if (msg[0] == 's') strlcpy(path, PAYLOAD_DIR, sizeof(path));
        else if (!full_path(msg + 1, path, sizeof(path))) {
            reply_error(req, "Bad path");
            return;
        }
        send_entries(req, path, msg[0] == 's');
        return;
    case 'm':
        if (!full_path(msg + 1, path, sizeof(path))) reply_error(req, "Bad path");
        else send_meta(req, path, msg + 1);
        return;
    case 'r': {
        if (!full_path(msg + 1, path, sizeof(path))) {
            reply_error(req, "Bad path");
            return;
        }
        lock();
        bool idle = s_phase == PHASE_IDLE;
        if (idle) {
            s_phase = PHASE_RUNNING;
            strlcpy(s_pending, path, sizeof(s_pending));
            app_post(s_ctx, APP_EV_USER, 0);
        }
        unlock();
        if (!idle) reply_error(req, "A script is already running");
        return;
    }
    case 'a': {
        int id, v;
        if (sscanf(msg + 1, "%d:%d", &id, &v) != 2) return;
        lock();
        // Un id vecchio è una risposta a un dialogo già chiuso dal pulsante
        if (id == s_dialog_id && s_dialog_kind == DLG_CHOICE) app_post(s_ctx, APP_EV_ANSWER, v);
        else if (id == s_dialog_id && s_dialog_kind == DLG_BUTTON) {
            static const app_ev_type_t gestures[] = {APP_EV_CLICK, APP_EV_DOUBLE, APP_EV_LONG};
            if (v >= 0 && v < 3) app_post(s_ctx, gestures[v], 0);
        }
        unlock();
        return;
    }
    case 'x':
        lock();
        // Solo durante l'esecuzione: dopo l'esito un EXIT farebbe uscire dall'app
        if (s_phase == PHASE_RUNNING) app_post(s_ctx, APP_EV_EXIT, 0);
        unlock();
        return;
    default: return;
    }
}

// Alla (ri)connessione: fase, stato dello script e dialogo aperto
static void on_auth(httpd_req_t *req)
{
    static const char *const phases[] = {"idle", "run", "done"};
    char hello[40];
    lock();
    snprintf(hello, sizeof(hello), "{\"t\":\"hello\",\"phase\":\"%s\"}", phases[s_phase]);
    websrv_reply(req, hello);
    if (s_state_msg) websrv_reply(req, s_state_msg);
    if (s_dialog_msg) websrv_reply(req, s_dialog_msg);
    unlock();
}

// ---------------------------------------------------------------------------
// App

// Esegue lo script chiesto dalla pagina
static void run_pending(app_ctx_t *ctx, payload_meta_t *m)
{
    char path[PAYLOAD_PATH_MAX];
    lock();
    strlcpy(path, s_pending, sizeof(path));
    unlock();
    const char *err = NULL;
    if (!payload_read_meta(path, m)) err = "Cannot read the script";
    else if (m->error[0]) err = m->error;
    if (!err) {
        inj_run_payload(ctx, m, &s_hooks);
        return;
    }
    jbuf_t b = {0};
    jb_begin(&b, "error");
    jb_kstr(&b, "text", err);
    char *msg = jb_end(&b);
    lock();
    s_phase = PHASE_IDLE;
    send_msg(msg);
    unlock();
    free(msg);
}

static void webinjector_run(app_ctx_t *ctx)
{
    webui_draw_connecting(&icon_keyboard, ACCENT);
    esp_err_t err = app_net_acquire(ctx, NET_AUTO, 15000);
    if (err == ESP_ERR_NOT_FINISHED) return; // uscita durante la scelta della rete
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "WiFi failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web Injector", msg, UI_ERR);
        return;
    }
    bool started = false;
    s_lock = xSemaphoreCreateMutex();
    payload_meta_t *m = malloc(sizeof(*m));
    if (!s_lock || !m) {
        app_message(ctx, "Web Injector", "Out of memory", UI_ERR);
        goto out;
    }
    s_ctx = ctx;
    s_phase = PHASE_IDLE;
    s_dialog_id = 0;
    s_dialog_kind = DLG_NONE;

    // webinjector.pin = false: nessun PIN, chiunque sulla rete del dongle
    // può avviare gli script della SD
    char pin[8] = "";
    if (config_bool("webinjector.pin", true)) snprintf(pin, sizeof(pin), "%06u", (unsigned)(esp_random() % 1000000));
    websrv_config_t cfg = {
        .pin = pin,
        .stack_size = 6144, // payload_read_meta e il JSON delle voci
        .max_frame = MAX_FRAME,
        .on_message = on_message,
        .on_auth = on_auth,
    };
    err = websrv_start(&cfg);
    started = err == ESP_OK;
    if (err == ESP_OK) {
        err = websrv_add_page("/", "text/html", page_gz_start, page_gz_end - page_gz_start);
    }
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web Injector", msg, UI_ERR);
        goto out;
    }

    // In hotspot si parte dalle credenziali WiFi, come nel Web HID
    int page = -1;
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
        if (ni.up && page < 0) page = ni.mode == NET_AP ? 1 : 0;
        if (ni.up) webui_draw_main(&ni, pin, page, client, ACCENT);
        else webui_draw_connecting(&icon_keyboard, ACCENT);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK && ni.mode == NET_AP) page ^= 1;
        if (ev.type == APP_EV_USER) {
            run_pending(ctx, m);
            led_stale = true; // l'esecuzione ha usato il LED
            if (ctx->exit_requested) break;
        }
    }
out:
    if (started) websrv_stop();
    free(m);
    free(s_state_msg);
    free(s_dialog_msg);
    s_state_msg = s_dialog_msg = NULL;
    if (s_lock) vSemaphoreDelete(s_lock);
    s_lock = NULL;
}

static void webinjector_subtitle(char *buf, size_t len)
{
    snprintf(buf, len, "%s", config_count("wifi") ? "WiFi" : "hotspot");
}

const app_desc_t webinjector_app = {
    .id = "webinjector",
    .name = "Web Injector",
    .icon = &icon_keyboard,
    .accent = ACCENT,
    .requires = APP_REQ_SD | APP_REQ_NET,
    .usb = USB_PROFILE_HID,
    // Come l'Injector: lo script gira in questo task (vedi injector.c)
    .stack = 16384,
    .run = webinjector_run,
    .subtitle = webinjector_subtitle,
};
