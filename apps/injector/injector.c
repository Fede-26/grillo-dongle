// Injector: runs Lua scripts from /sd/payloads that type on the connected PC.
//
// Flow: folder browser, script card (description,
// layout, OS), wait for the USB host, execution, result. A 3 s hold during
// execution aborts the script and returns to the list.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "engine.h"
#include "icons.h"
#include "injector.h"
#include "storage.h"
#include "ui.h"
#include "usbdev.h"

#define ACCENT      0xFF8A20
#define MAX_ENTRIES 48

// Script card: long = run, double = back. true if it should run.
// A long description scrolls on its own; a click advances it immediately.
static bool show_card(app_ctx_t *ctx, const payload_meta_t *m)
{
    ui_scroll_t sc;
    ui_scroll_init(&sc, &gfx_font_small, 6, UI_CONTENT_Y + 23, UI_W - 12, 3, m->desc);
    char info[48];
    snprintf(info, sizeof(info), "%s · %s", m->layout[0] ? m->layout : config_str("keyboard.layout", "us"),
             m->os[0] ? m->os : config_str("keyboard.os", "windows"));
    for (;;) {
        ui_begin();
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 1, UI_W - 12, m->name[0] ? m->name : "(no @name)", ACCENT);
        gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 12, info, UI_DIM);
        ui_scroll_draw(&sc, UI_TEXT);
        ui_draw_hint("hold: run · 2x: back");
        ui_end();

        app_event_t ev;
        if (!app_next_event(ctx, &ev, ui_scroll_wait_ms(&sc))) {
            ui_scroll_tick(&sc);
            continue;
        }
        if (ev.type == APP_EV_EXIT) return false;
        if (ev.type == APP_EV_DOUBLE) return false;
        if (ev.type == APP_EV_LONG) return true;
        if (ev.type == APP_EV_CLICK) ui_scroll_step(&sc);
    }
}

// ---------------------------------------------------------------------------
// Browser

// The browser has four views: the real folders, and three virtual views
// built by reading the headers of all scripts (@os, @tags).
typedef enum {
    VIEW_DIR,      // folders and scripts of `dir`
    VIEW_OS_LIST,  // one group per operating system
    VIEW_TAG_LIST, // one group per tag
    VIEW_FILTER,   // scripts belonging to the chosen group
} view_t;

typedef enum { ROW_UP, ROW_BY_OS, ROW_BY_TAG, ROW_GROUP, ROW_ENTRY } row_kind_t;

#define MAX_GROUPS 16

typedef struct {
    payload_entry_t e[MAX_ENTRIES];
    char label[MAX_ENTRIES + MAX_GROUPS + 3][52];
    char right[MAX_ENTRIES + MAX_GROUPS + 3][20];
    const char *ptr[MAX_ENTRIES + MAX_GROUPS + 3];
    const char *rptr[MAX_ENTRIES + MAX_GROUPS + 3];
    row_kind_t kind[MAX_ENTRIES + MAX_GROUPS + 3];
    int entry[MAX_ENTRIES + MAX_GROUPS + 3]; // index into e[] for ROW_ENTRY
} listing_t;

static const char *const OS_NAMES[] = {"windows", "linux", "mac"};
static const char *const OS_SHORT[] = {"win", "lin", "mac"};

static void add_row(listing_t *ls, int *n, row_kind_t k, const char *label, const char *right, int entry)
{
    strlcpy(ls->label[*n], label, sizeof(ls->label[0]));
    strlcpy(ls->right[*n], right ? right : "", sizeof(ls->right[0]));
    ls->kind[*n] = k;
    ls->entry[*n] = entry;
    ls->ptr[*n] = ls->label[*n];
    ls->rptr[*n] = ls->right[*n];
    (*n)++;
}

// Short layout name for the right column: "colemak" is too wide.
static const char *layout_short(const char *l)
{
    return strcasecmp(l, "colemak") == 0 ? "cmk" : l;
}

// Right column of a script: "it/win"; in the per-OS views the layout is enough.
static void entry_right(const payload_entry_t *e, bool show_os, char *out, size_t len)
{
    const char *l = layout_short(payload_layout(e));
    if (show_os) snprintf(out, len, "%s/%s", l, OS_SHORT[payload_os(e)]);
    else strlcpy(out, l, len);
}

static void injector_run(app_ctx_t *ctx)
{
    // Autorun: the script is given by the argument, relative to /payloads
    if (ctx->arg && *ctx->arg) {
        payload_meta_t *m = calloc(1, sizeof(*m));
        if (!m) return;
        char path[PAYLOAD_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", PAYLOAD_DIR, ctx->arg);
        if (!payload_read_meta(path, m)) app_message(ctx, "Autorun", "Script not found", UI_ERR);
        else if (m->error[0]) app_message(ctx, "Header error", m->error, UI_ERR);
        else inj_run_payload(ctx, m, NULL);
        free(m);
        return;
    }

    listing_t *ls = malloc(sizeof(*ls));
    payload_meta_t *m = malloc(sizeof(*m));
    if (!ls || !m) {
        app_message(ctx, "Injector", "Out of memory", UI_ERR);
        goto out;
    }
    char dir[PAYLOAD_PATH_MAX];
    strlcpy(dir, PAYLOAD_DIR, sizeof(dir));
    view_t view = VIEW_DIR, parent_view = VIEW_DIR;
    int filter_os = 0;
    char filter_tag[PAYLOAD_TAG_LEN] = "";
    int sel = 0;

    for (;;) {
        bool at_root = strcmp(dir, PAYLOAD_DIR) == 0;
        int n = 0; // rows
        const char *title = "Payloads";
        bool os_view = view == VIEW_FILTER && parent_view == VIEW_OS_LIST;

        if (view == VIEW_DIR) {
            int ne = payload_list(dir, ls->e, MAX_ENTRIES);
            if (ne < 0) {
                app_message(ctx, "Injector", "No /payloads folder on the SD", UI_ERR);
                break;
            }
            // The cap applies while reading the folder: a folder with more items
            // shows only a subset, so the user should at least be warned.
            if (ne == MAX_ENTRIES) ui_toast("Only 48 items: use subfolders", UI_WARN, 2500);
            if (!at_root) {
                add_row(ls, &n, ROW_UP, "..", NULL, -1);
                title = strrchr(dir, '/') + 1;
            } else {
                // The virtual views sit at the top, only at the root and only if there is something to group
                if (ne > 0) {
                    add_row(ls, &n, ROW_BY_OS, "/By OS", NULL, -1);
                    add_row(ls, &n, ROW_BY_TAG, "/By tag", NULL, -1);
                }
            }
            for (int i = 0; i < ne; i++) {
                char label[52], right[20] = "";
                snprintf(label, sizeof(label), "%s%s", ls->e[i].is_dir ? "/" : "", ls->e[i].title);
                if (!ls->e[i].is_dir) entry_right(&ls->e[i], true, right, sizeof(right));
                add_row(ls, &n, ROW_ENTRY, label, right, i);
            }
        } else {
            int ne = payload_scan(PAYLOAD_DIR, ls->e, MAX_ENTRIES);
            if (ne < 0) ne = 0;
            if (ne == MAX_ENTRIES) ui_toast("Only 48 scripts in views", UI_WARN, 2500);
            add_row(ls, &n, ROW_UP, "..", NULL, -1);
            if (view == VIEW_OS_LIST) {
                title = "By OS";
                for (int o = 0; o < 3; o++) {
                    int c = 0;
                    for (int i = 0; i < ne; i++) c += payload_os(&ls->e[i]) == o;
                    char cnt[12];
                    snprintf(cnt, sizeof(cnt), "%d", c);
                    if (c) add_row(ls, &n, ROW_GROUP, OS_NAMES[o], cnt, o);
                }
            } else if (view == VIEW_TAG_LIST) {
                title = "By tag";
                char tags[MAX_GROUPS][PAYLOAD_TAG_LEN];
                int nt = payload_collect_tags(ls->e, ne, tags, MAX_GROUPS);
                for (int t = 0; t < nt; t++) {
                    int c = 0;
                    for (int i = 0; i < ne; i++) c += payload_has_tag(ls->e[i].tags, tags[t]);
                    char cnt[12];
                    snprintf(cnt, sizeof(cnt), "%d", c);
                    add_row(ls, &n, ROW_GROUP, tags[t], cnt, t);
                }
                if (nt == 0) add_row(ls, &n, ROW_GROUP, "(no @tags in scripts)", NULL, -1);
            } else {
                title = os_view ? OS_NAMES[filter_os] : filter_tag;
                for (int i = 0; i < ne; i++) {
                    bool match = os_view ? payload_os(&ls->e[i]) == filter_os : payload_has_tag(ls->e[i].tags, filter_tag);
                    if (!match) continue;
                    char right[20];
                    entry_right(&ls->e[i], !os_view, right, sizeof(right));
                    add_row(ls, &n, ROW_ENTRY, ls->e[i].title, right, i);
                }
            }
        }
        if (n == 0) {
            app_message(ctx, "Injector", "No .lua scripts in /payloads", UI_WARN);
            break;
        }
        if (sel >= n) sel = 0;

        sel = app_menu_ex(ctx, title, ls->ptr, ls->rptr, n, sel);
        if (sel < 0) break; // hold 3 s

        switch (ls->kind[sel]) {
        case ROW_UP:
            if (view == VIEW_FILTER) view = parent_view;
            else if (view != VIEW_DIR) view = VIEW_DIR;
            else *strrchr(dir, '/') = 0;
            sel = 0;
            continue;
        case ROW_BY_OS:
            view = VIEW_OS_LIST;
            sel = 0;
            continue;
        case ROW_BY_TAG:
            view = VIEW_TAG_LIST;
            sel = 0;
            continue;
        case ROW_GROUP:
            if (ls->entry[sel] < 0) continue; // informational row
            parent_view = view;
            if (view == VIEW_OS_LIST) filter_os = ls->entry[sel];
            else strlcpy(filter_tag, ls->label[sel], sizeof(filter_tag));
            view = VIEW_FILTER;
            sel = 0;
            continue;
        case ROW_ENTRY: break;
        }

        const payload_entry_t *e = &ls->e[ls->entry[sel]];
        char path[PAYLOAD_PATH_MAX];
        if (view == VIEW_DIR) {
            if (snprintf(path, sizeof(path), "%s/%s", dir, e->name) >= (int)sizeof(path)) {
                app_message(ctx, "Injector", "Path too long", UI_ERR);
                continue;
            }
        } else {
            strlcpy(path, e->path, sizeof(path));
        }
        if (e->is_dir) {
            strlcpy(dir, path, sizeof(dir));
            sel = 0;
            continue;
        }
        if (!payload_read_meta(path, m)) {
            app_message(ctx, "Injector", "Cannot read the script", UI_ERR);
        } else if (m->error[0]) {
            app_message(ctx, "Header error", m->error, UI_ERR);
        } else if (show_card(ctx, m)) {
            inj_run_payload(ctx, m, NULL);
        }
        if (ctx->exit_requested) break;
    }
out:
    free(ls);
    free(m);
}

static void injector_subtitle(char *buf, size_t len)
{
    payload_entry_t *e = malloc(MAX_ENTRIES * sizeof(*e));
    int n = e && storage_state() == STORAGE_MOUNTED ? payload_list(PAYLOAD_DIR, e, MAX_ENTRIES) : -1;
    free(e);
    if (n < 0) snprintf(buf, len, "no payloads");
    else snprintf(buf, len, "%d item%s", n, n == 1 ? "" : "s");
}

const app_desc_t injector_app = {
    .id = "injector",
    .name = "Injector",
    .icon = &icon_keyboard,
    .accent = ACCENT,
    .requires = APP_REQ_SD,
    .usb = USB_PROFILE_HID,
    // 16 KB: with 10 KB a script reached about 6 levels of nested pcall
    // before hitting the stack guard (see luart.c).
    .stack = 16384,
    .run = injector_run,
    .subtitle = injector_subtitle,
};
