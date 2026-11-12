// Web Injector: injector scripts picked and followed from a phone.
//
// Like Web HID: network lease (known network or hotspot), server with PIN, QR
// code and PIN on the display. From the page you browse the scripts in
// /sd/payloads (folders, by OS, by tag), read the details card and launch.
// The script runs in the app task with the same core as the Injector
// (injector_core): display and button stay active, and in dialogs the first
// answer wins, from the button or from the page (APP_EV_ANSWER). If the
// page closes the script keeps going; on reconnection the page receives
// the run history, the state, any open dialog and the outcome.

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
// The longest message from the page is a command with a path
#define MAX_FRAME   (PAYLOAD_PATH_MAX + 16)
// History: last HIST_MAX entries, text cut at HIST_TEXT bytes (a script
// can write for hours): about thirty KB at most
#define HIST_MAX    100
#define HIST_TEXT   200

extern const uint8_t page_gz_start[] asm("_binary_webinjector_html_gz_start");
extern const uint8_t page_gz_end[] asm("_binary_webinjector_html_gz_end");

static const char *const OS_NAMES[] = {"windows", "linux", "mac"};

typedef enum { PHASE_IDLE, PHASE_RUNNING, PHASE_DONE } phase_t;
// CHOICE: menu, confirmation and outcome, answered with APP_EV_ANSWER.
// BUTTON: ui.wait_button, which the page answers with a gesture.
typedef enum { DLG_NONE, DLG_CHOICE, DLG_BUTTON } dlg_kind_t;

// Shared between the app task (script hooks) and the httpd task
// (page commands, reconnection): always under s_lock.
static SemaphoreHandle_t s_lock;
static app_ctx_t *s_ctx;
static phase_t s_phase;
static char s_pending[PAYLOAD_PATH_MAX]; // script requested by the page
static int s_dialog_id, s_next_id;       // id of the open dialog, 0 if none
static dlg_kind_t s_dialog_kind;
// Latest state and dialog, resent on every page reconnection
static char *s_state_msg, *s_dialog_msg;
// Outcome of the last run, resent on reconnection until another one
// starts: the page stays on the execution view even after the run ends
static char *s_result_msg;
static bool s_page_answered; // the answer to the open dialog came from the page

// History of the current or last run (start, sys.log, status changes,
// dialogs with their answer, outcome), resent on every reconnection:
// this way you also see what happened while the page was closed (phone
// in standby). Circular queue of ready-made JSON messages, under s_lock.
static char *s_hist[HIST_MAX];
static int s_hist_first, s_hist_n, s_hist_dropped;
// Only in the app task (hooks and run_pending)
static int64_t s_run_start_us;
static char s_last_status[48]; // status already in history: progress updates alone are not added

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

// ---------------------------------------------------------------------------
// Script hooks (app task)

// Under s_lock
static void hist_clear(void)
{
    for (int i = 0; i < s_hist_n; i++) free(s_hist[(s_hist_first + i) % HIST_MAX]);
    s_hist_first = s_hist_n = s_hist_dropped = 0;
}

// Adds an entry to the history and sends it to the page. kind: start,
// log, status, ask, answer, done. Call without s_lock.
static void add_event(const char *kind, const char *text)
{
    // Dongle time if known, otherwise time since the run started
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
        // Cut on a UTF-8 character boundary, then "…"
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

// NULL (out of memory) is not sent
static void send_msg(const char *msg)
{
    if (msg) websrv_send(msg);
}

// Replaces the message to resend on reconnection and sends it
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

// Opens a dialog: new id, message kept for reconnection
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

// Outcome for the page: "done" is the dialog open on the display, "result"
// what remains afterwards, until the next run. The path (relative to
// /payloads) lets "Run again" work even after a reconnection.
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

// The outcome stays on the display until someone answers: for the page it
// is a dialog with a single button.
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
    s_phase = PHASE_DONE; // from here on the page can no longer request an abort
    free(s_result_msg);
    s_result_msg = result;
    unlock();
    b = (jbuf_t){0}; // jb_end handed the buffer over to s_result_msg
    outcome(&b, "done", r);
    open_dialog(DLG_CHOICE, &b);
}

static void hook_dialog_end(void *arg, const char *answer)
{
    jbuf_t b = {0};
    lock();
    // Approximate: if page and button answer together, the first to arrive
    // wins but the flag says page
    bool page = s_page_answered;
    jb_begin(&b, "close");
    jb_kint(&b, "id", s_dialog_id);
    s_dialog_id = 0;
    s_dialog_kind = DLG_NONE;
    free(s_dialog_msg);
    s_dialog_msg = NULL;
    // Once the outcome is closed, the script is completely finished
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
// Page commands (httpd task)

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

// Path relative to /sd/payloads sent by the page: no absolute paths
// and no "..", so the page cannot read outside the folder.
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

// One entry per message, then "end": no large buffers for full folders.
// For scripts, layout and OS are the effective ones (header or
// config.lua), and with the scan the relative path is included too.
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

// Commands:
//   l<folder>    listing of a folder ("" = /payloads)
//   s            all scripts, for the by-OS and by-tag views
//   m<script>    details card (header)
//   r<script>    run
//   a<id>:<n>    answer to dialog <id>: index, 0/1 for no/yes, 0 for
//                the outcome; for ui.wait_button 0 click, 1 double, 2 long
//   x            abort the script
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
        // An old id is an answer to a dialog already closed by the button
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
        // Only while running: after the outcome an EXIT would quit the app
        if (s_phase == PHASE_RUNNING) app_post(s_ctx, APP_EV_EXIT, 0);
        unlock();
        return;
    default: return;
    }
}

// On (re)connection: phase, history, script state, open dialog and
// outcome of the last run. Under s_lock: a new entry can only arrive
// afterwards, so no duplicates and no gaps.
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

// Runs the script requested by the page
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
        // New run: the history and outcome of the previous one are discarded
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
    if (err == ESP_ERR_NOT_FINISHED) return; // exit during network selection
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

    // webinjector.pin = false or secure network: no PIN, anyone on the dongle's network
    // can launch the scripts on the SD
    char pin[8];
    webui_make_pin("webinjector.pin", pin, sizeof(pin));
    websrv_config_t cfg = {
        .pin = pin,
        .stack_size = 6144, // payload_read_meta and the entries' JSON
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
        else webui_draw_connecting(&icon_keyboard, ACCENT);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK) webui_qr_toggle(&qr, &ni);
        if (ev.type == APP_EV_USER) {
            run_pending(ctx, m);
            led_stale = true; // the run used the LED
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
    // Like the Injector: the script runs in this task (see injector.c)
    .stack = 16384,
    .run = webinjector_run,
    .subtitle = webinjector_subtitle,
};
