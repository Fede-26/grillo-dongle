// Web Injector: gli script dell'injector scelti e seguiti da un telefono.
//
// Come il Web HID: lease di rete (rete nota o hotspot), server con PIN, QR
// code e PIN sul display. Dalla pagina si sfogliano gli script di
// /sd/payloads (cartelle, per OS, per tag), si legge la scheda e si avvia.
// Lo script gira nel task dell'app con lo stesso nucleo dell'Injector
// (injector_core): display e pulsante restano attivi, e nei dialoghi vince
// la prima risposta, dal pulsante o dalla pagina (APP_EV_ANSWER). Se la
// pagina si chiude lo script continua; alla riconnessione la pagina riceve
// la cronologia della run, lo stato, l'eventuale dialogo aperto e l'esito.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "clock.h"
#include "config.h"
#include "engine.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "icons.h"
#include "led.h"
#include "net.h"
#include "payload.h"
#include "ui.h"
#include "usbdev.h"
#include "webinjector.h"
#include "webjson.h"
#include "websrv.h"
#include "webtheme.h"
#include "webui.h"

#define ACCENT      0xB060FF
#define MAX_ENTRIES 48
// Il messaggio più lungo dalla pagina è un comando con un percorso
#define MAX_FRAME   (PAYLOAD_PATH_MAX + 16)
// Cronologia: ultime HIST_MAX voci, testo tagliato a HIST_TEXT byte (uno
// script può scrivere per ore): al massimo una trentina di KB
#define HIST_MAX    100
#define HIST_TEXT   200

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
// Esito dell'ultima run, rimandato alla riconnessione finché non ne parte
// un'altra: la pagina resta sull'esecuzione anche a run finita
static char *s_result_msg;
static bool s_page_answered; // la risposta al dialogo aperto è arrivata dalla pagina

// Cronologia della run in corso o dell'ultima (avvio, sys.log, cambi di
// stato, dialoghi con la risposta, esito), rimandata a ogni riconnessione:
// così si vede anche ciò che è successo a pagina chiusa (telefono in
// standby). Coda circolare di messaggi JSON già pronti, sotto s_lock.
static char *s_hist[HIST_MAX];
static int s_hist_first, s_hist_n, s_hist_dropped;
// Solo nel task dell'app (agganci e run_pending)
static int64_t s_run_start_us;
static char s_last_status[48]; // stato già in cronologia: i soli avanzamenti non entrano

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

// ---------------------------------------------------------------------------
// Agganci dello script (task dell'app)

// Sotto s_lock
static void hist_clear(void)
{
    for (int i = 0; i < s_hist_n; i++) free(s_hist[(s_hist_first + i) % HIST_MAX]);
    s_hist_first = s_hist_n = s_hist_dropped = 0;
}

// Aggiunge una voce alla cronologia e la manda alla pagina. kind: start,
// log, status, ask, answer, done. Da chiamare senza s_lock.
static void add_event(const char *kind, const char *text)
{
    // Ora del dongle se nota, altrimenti tempo dall'avvio della run
    char at[16];
    if (clock_valid()) {
        time_t now = time(NULL);
        struct tm tm;
        strftime(at, sizeof(at), "%H:%M:%S", localtime_r(&now, &tm));
    } else {
        int sec = (int)((esp_timer_get_time() - s_run_start_us) / 1000000);
        snprintf(at, sizeof(at), "+%d:%02d", sec / 60, sec % 60);
    }
    char cut[HIST_TEXT + 4];
    size_t n = strlen(text);
    if (n > HIST_TEXT) {
        // Taglio su un confine di carattere UTF-8, poi "…"
        n = HIST_TEXT;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
        memcpy(cut, text, n);
        memcpy(cut + n, "\xE2\x80\xA6", 4);
        text = cut;
    }
    jbuf_t b = {0};
    jb_begin(&b, "log");
    jb_kstr(&b, "k", kind);
    jb_kstr(&b, "at", at);
    jb_kstr(&b, "text", text);
    char *msg = jb_end(&b);
    if (!msg) return;
    lock();
    if (s_hist_n == HIST_MAX) {
        free(s_hist[s_hist_first]);
        s_hist_first = (s_hist_first + 1) % HIST_MAX;
        s_hist_n--;
        s_hist_dropped++;
    }
    s_hist[(s_hist_first + s_hist_n++) % HIST_MAX] = msg;
    websrv_send(msg);
    unlock();
}

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
    if (r->status[0] && strcmp(r->status, s_last_status) != 0) add_event("status", r->status);
    strlcpy(s_last_status, r->status, sizeof(s_last_status));
}

static void hook_log(void *arg, const char *text)
{
    add_event("log", text);
}

// Apre un dialogo: nuovo id, messaggio conservato per la riconnessione
static void open_dialog(dlg_kind_t kind, jbuf_t *b)
{
    lock();
    s_dialog_id = ++s_next_id;
    s_dialog_kind = kind;
    s_page_answered = false;
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
    char t[HIST_TEXT];
    snprintf(t, sizeof(t), "Menu: %s", title);
    add_event("ask", t);
}

static void hook_confirm(void *arg, const char *title, const char *text, bool yes)
{
    jbuf_t b = {0};
    jb_begin(&b, "confirm");
    jb_kstr(&b, "title", title);
    jb_kstr(&b, "text", text);
    jb_kbool(&b, "yes", yes);
    open_dialog(DLG_CHOICE, &b);
    char t[HIST_TEXT];
    snprintf(t, sizeof(t), "Confirm: %s", text);
    add_event("ask", t);
}

static void hook_wait_button(void *arg)
{
    jbuf_t b = {0};
    jb_begin(&b, "button");
    open_dialog(DLG_BUTTON, &b);
    add_event("ask", "Waiting for the button");
}

// Esito per la pagina: "done" è il dialogo aperto sul display, "result"
// ciò che resta dopo, fino alla run successiva. Il percorso (relativo a
// /payloads) serve a "Run again" anche dopo una riconnessione.
static void outcome(jbuf_t *b, const char *type, const inj_run_t *r)
{
    jb_begin(b, type);
    jb_kstr(b, "name", r->meta->name[0] ? r->meta->name : "Script");
    jb_kstr(b, "path", r->meta->path + strlen(PAYLOAD_DIR "/"));
    jb_kbool(b, "ok", r->ok);
    jb_kbool(b, "aborted", r->aborted && !r->timed_out);
    jb_kbool(b, "timeout", r->timed_out);
    jb_kstr(b, "error", r->error);
}

// L'esito resta sul display finché qualcuno risponde: per la pagina è un
// dialogo con un solo pulsante.
static void hook_done(void *arg, const inj_run_t *r)
{
    char t[HIST_TEXT];
    if (r->ok) strlcpy(t, "Done", sizeof(t));
    else if (r->timed_out) strlcpy(t, "Timeout", sizeof(t));
    else if (r->aborted) strlcpy(t, "Aborted", sizeof(t));
    else snprintf(t, sizeof(t), "Error: %s", r->error[0] ? r->error : "failed");
    add_event("done", t);
    jbuf_t b = {0};
    outcome(&b, "result", r);
    char *result = jb_end(&b);
    lock();
    s_phase = PHASE_DONE; // da qui la pagina non può più chiedere l'abort
    free(s_result_msg);
    s_result_msg = result;
    unlock();
    b = (jbuf_t){0}; // jb_end ha ceduto il buffer a s_result_msg
    outcome(&b, "done", r);
    open_dialog(DLG_CHOICE, &b);
}

static void hook_dialog_end(void *arg, const char *answer)
{
    jbuf_t b = {0};
    lock();
    // Approssimato: se pagina e pulsante rispondono insieme, vale il primo
    // arrivato ma il flag dice pagina
    bool page = s_page_answered;
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
    if (answer) {
        char t[HIST_TEXT];
        snprintf(t, sizeof(t), "Answer: %s%s", answer, page ? " (from the page)" : "");
        add_event("answer", t);
    }
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
        if (id == s_dialog_id && s_dialog_kind == DLG_CHOICE) {
            app_post(s_ctx, APP_EV_ANSWER, v);
            s_page_answered = true;
        } else if (id == s_dialog_id && s_dialog_kind == DLG_BUTTON && v >= 0 && v < 3) {
            static const app_ev_type_t gestures[] = {APP_EV_CLICK, APP_EV_DOUBLE, APP_EV_LONG};
            app_post(s_ctx, gestures[v], 0);
            s_page_answered = true;
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

// Alla (ri)connessione: fase, cronologia, stato dello script, dialogo
// aperto ed esito dell'ultima run. Sotto s_lock: una voce nuova arriva solo
// dopo, quindi niente doppioni né buchi.
static void on_auth(httpd_req_t *req)
{
    static const char *const phases[] = {"idle", "run", "done"};
    char hello[64];
    lock();
    snprintf(hello, sizeof(hello), "{\"t\":\"hello\",\"phase\":\"%s\",\"dropped\":%d}", phases[s_phase],
             s_hist_dropped);
    websrv_reply(req, hello);
    for (int i = 0; i < s_hist_n; i++) websrv_reply(req, s_hist[(s_hist_first + i) % HIST_MAX]);
    if (s_state_msg) websrv_reply(req, s_state_msg);
    if (s_dialog_msg) websrv_reply(req, s_dialog_msg);
    if (s_result_msg) websrv_reply(req, s_result_msg);
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
        // Run nuova: si perdono cronologia ed esito di quella prima
        lock();
        hist_clear();
        free(s_result_msg);
        s_result_msg = NULL;
        unlock();
        s_run_start_us = esp_timer_get_time();
        s_last_status[0] = 0;
        add_event("start", m->name[0] ? m->name : path + strlen(PAYLOAD_DIR "/"));
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

    // webinjector.pin = false o rete sicura: nessun PIN, chiunque sulla rete del dongle
    // può avviare gli script della SD
    char pin[8];
    webui_make_pin("webinjector.pin", pin, sizeof(pin));
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
    if (err == ESP_OK) err = webtheme_register();
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web Injector", msg, UI_ERR);
        goto out;
    }

    // Pagina del QR: credenziali o indirizzo, vedi webui.h
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
        else webui_draw_connecting(&icon_keyboard, ACCENT);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK) webui_qr_toggle(&qr, &ni);
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
    free(s_result_msg);
    s_state_msg = s_dialog_msg = s_result_msg = NULL;
    if (s_lock) {
        hist_clear();
        vSemaphoreDelete(s_lock);
    }
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
