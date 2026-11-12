#pragma once

#include <stdbool.h>
#include "app.h"
#include "payload.h"
#include "ui.h"

// Execution of an injector script, shared by the apps that use it
// (Injector from the display, Web Injector from the phone).
//
// An "engine" knows how to run one file type (only Lua today; DuckyScript in
// the future). inj_run_payload prepares an inj_run_t, picks the engine with
// can_run() and calls run(): the script runs in the app task, so dialogs
// (ui.menu, ui.confirm) can use the event queue directly.

typedef struct inj_run inj_run_t;

// Optional front-end hooks: the Web Injector uses them to mirror on the phone
// what the script shows on the display. All run in the app task; any field
// may be NULL.
typedef struct {
    void *arg;
    // Status, progress or host wait changed
    void (*state)(void *arg, const inj_run_t *r);
    // Line from sys.log
    void (*log)(void *arg, const char *text);
    // Dialog opened on the display (the front-end can answer with
    // APP_EV_ANSWER); closed by dialog_end, whoever answered, with the
    // answer as text (menu item, "Yes"/"No", button gesture) or
    // NULL if there is none (abort, closing of the result).
    void (*menu)(void *arg, const char *title, const char *const *items, int n);
    void (*confirm)(void *arg, const char *title, const char *text, bool yes);
    void (*wait_button)(void *arg);
    // Result, then shown on the display until a click or an APP_EV_ANSWER
    void (*done)(void *arg, const inj_run_t *r);
    void (*dialog_end)(void *arg, const char *answer);
} inj_hooks_t;

struct inj_run {
    app_ctx_t *ctx;
    const payload_meta_t *meta;
    const inj_hooks_t *hooks;

    // Status shown by the run screen, updated by the script
    char status[48];
    ui_scroll_t status_scroll; // long status: scrolls by itself (see inj_redraw)
    int64_t redraw_us;         // next periodic redraw by inj_pump
    int progress, progress_max; // bar hidden if progress_max == 0
    bool waiting_host;
    int64_t started_us;

    // Events collected by inj_pump() while the script runs
    uint32_t clicks, doubles, longs;
    bool ejected; // the host ejected the disk (HID_DISK profile)

    // Result
    bool ok, aborted, timed_out;
    char error[160];
};

typedef struct {
    const char *name;
    bool (*can_run)(const char *path);
    void (*run)(inj_run_t *r);
} inj_engine_t;

extern const inj_engine_t engine_lua;

// Runs the script and shows the result on the display. hooks may be NULL.
void inj_run_payload(app_ctx_t *ctx, const payload_meta_t *m, const inj_hooks_t *hooks);

// --- For the engines ---

// Redraws the run screen.
void inj_redraw(inj_run_t *r);
// Drains the app event queue (counting clicks, eject...) and updates the
// screen if needed. false = the script must stop (3 s hold or timeout).
bool inj_pump(inj_run_t *r);
// Calls a front-end hook, if present.
#define INJ_HOOK(r, fn, ...)                                                          \
    do {                                                                              \
        if ((r)->hooks && (r)->hooks->fn) (r)->hooks->fn((r)->hooks->arg, ##__VA_ARGS__); \
    } while (0)
