// Injector: esegue script Lua da /sd/payloads che digitano sul PC collegato.
//
// Flusso: browser delle cartelle, scheda dello script (descrizione,
// layout, OS), attesa dell'host USB, esecuzione, esito. Hold 3 s durante
// l'esecuzione interrompe lo script e riporta alla lista.

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "config.h"
#include "engine.h"
#include "esp_timer.h"
#include "icons.h"
#include "injector.h"
#include "led.h"
#include "storage.h"
#include "ui.h"
#include "usbdev.h"

#define ACCENT      0xFF8A20
#define MAX_ENTRIES 48

static const inj_engine_t *const s_engines[] = {&engine_lua};

static const inj_engine_t *find_engine(const char *path)
{
    for (size_t i = 0; i < sizeof(s_engines) / sizeof(s_engines[0]); i++) {
        if (s_engines[i]->can_run(path)) return s_engines[i];
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Schermata di esecuzione

void inj_redraw(inj_run_t *r)
{
    ui_begin();
    if (r->waiting_host) {
        gfx_text(&gfx_font_large, 6, UI_CONTENT_Y + 4, "Waiting", ACCENT);
        gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 24, "for the USB host…", UI_TEXT);
    } else {
        int secs = (esp_timer_get_time() - r->started_us) / 1000000;
        char t[16];
        snprintf(t, sizeof(t), "%d:%02d", secs / 60, secs % 60);
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 40, r->meta->name[0] ? r->meta->name : "Script", ACCENT);
        gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 2, UI_W - 6, GFX_ALIGN_RIGHT, t, UI_DIM);
        ui_draw_wrapped(&gfx_font_small, 6, UI_CONTENT_Y + 16, UI_W - 12, 2, r->status, UI_TEXT);
        if (r->progress_max > 0) ui_draw_progress(6, UI_CONTENT_Y + 38, UI_W - 12, 5, r->progress, r->progress_max, ACCENT);
    }
    ui_draw_hint("hold 3s to abort");
    ui_end();
}

bool inj_pump(inj_run_t *r)
{
    app_event_t ev;
    while (app_next_event(r->ctx, &ev, 0)) {
        switch (ev.type) {
        case APP_EV_CLICK: r->clicks++; break;
        case APP_EV_DOUBLE: r->doubles++; break;
        case APP_EV_LONG: r->longs++; break;
        case APP_EV_USB:
            if (ev.arg == USBDEV_EV_MSC_EJECTED) r->ejected = true;
            break;
        default: break;
        }
    }
    return !r->ctx->exit_requested;
}

// ---------------------------------------------------------------------------
// Esecuzione e esito

static void run_payload(app_ctx_t *ctx, const payload_meta_t *m)
{
    const inj_engine_t *eng = find_engine(m->path);
    if (!eng) {
        app_message(ctx, "Cannot run", "No engine for this file type", UI_ERR);
        return;
    }
    inj_run_t *r = calloc(1, sizeof(*r));
    if (!r) {
        app_message(ctx, "Cannot run", "Out of memory", UI_ERR);
        return;
    }
    r->ctx = ctx;
    r->meta = m;
    r->started_us = esp_timer_get_time();
    eng->run(r);

    if (r->ok) {
        led_set(LED_LAYER_APP, LED_FX_SOLID, UI_OK, 0);
        app_message(ctx, "Done", m->name[0] ? m->name : "Script finished", UI_OK);
    } else {
        led_set(LED_LAYER_APP, LED_FX_BLINK, UI_ERR, 400);
        const char *why = r->timed_out ? "timeout" : (r->error[0] ? r->error : "failed");
        app_message(ctx, r->aborted && !r->timed_out ? "Aborted" : "Error", why, UI_ERR);
    }
    led_clear(LED_LAYER_APP);
    // L'hold 3 s ha solo interrotto lo script: si resta nell'app. Un
    // secondo hold dalla lista esce.
    ctx->exit_requested = false;
    free(r);
}

// Righe di descrizione visibili insieme. Se la descrizione è più lunga si
// scorre a pagine: da sola ogni PAGE_MS, o subito con un click.
#define DESC_LINES 3
#define PAGE_MS    3500

// Scheda dello script: lungo = esegui, doppio = indietro. true se va eseguito.
static bool show_card(app_ctx_t *ctx, const payload_meta_t *m)
{
    int total = ui_draw_wrapped_scroll(&gfx_font_small, 0, 0, UI_W - 12, 0, 0, m->desc, UI_TEXT);
    int pages = (total + DESC_LINES - 1) / DESC_LINES;
    int page = 0;
    int64_t flip_at = esp_timer_get_time() + PAGE_MS * 1000LL;
    for (;;) {
        ui_begin();
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 1, UI_W - 12, m->name[0] ? m->name : "(no @name)", ACCENT);
        char info[48];
        snprintf(info, sizeof(info), "%s · %s", m->layout[0] ? m->layout : config_str("keyboard.layout", "us"),
                 m->os[0] ? m->os : config_str("keyboard.os", "windows"));
        gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 12, info, UI_DIM);
        if (pages > 1) {
            char pg[24];
            snprintf(pg, sizeof(pg), "%d/%d", page + 1, pages);
            gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 12, UI_W - 6, GFX_ALIGN_RIGHT, pg, UI_FAINT);
        }
        ui_draw_wrapped_scroll(&gfx_font_small, 6, UI_CONTENT_Y + 23, UI_W - 12, DESC_LINES, page * DESC_LINES, m->desc,
                               UI_TEXT);
        ui_draw_hint(pages > 1 ? "click: more · hold: run" : "hold: run · 2x: back");
        ui_end();

        app_event_t ev;
        // Con più pagine ci si sveglia anche senza eventi, per girare pagina da soli
        if (!app_next_event(ctx, &ev, pages > 1 ? 250 : portMAX_DELAY)) {
            if (esp_timer_get_time() >= flip_at) {
                page = (page + 1) % pages;
                flip_at = esp_timer_get_time() + PAGE_MS * 1000LL;
            }
            continue;
        }
        if (ev.type == APP_EV_EXIT) return false;
        if (ev.type == APP_EV_DOUBLE) return false;
        if (ev.type == APP_EV_LONG) return true;
        if (ev.type == APP_EV_CLICK && pages > 1) {
            page = (page + 1) % pages;
            flip_at = esp_timer_get_time() + PAGE_MS * 1000LL; // chi legge a mano ha più tempo
        }
    }
}

// ---------------------------------------------------------------------------
// Browser

// Il browser ha quattro viste: le cartelle vere, e tre viste virtuali
// costruite leggendo le intestazioni di tutti gli script (@os, @tags).
typedef enum {
    VIEW_DIR,      // cartelle e script di `dir`
    VIEW_OS_LIST,  // un gruppo per sistema operativo
    VIEW_TAG_LIST, // un gruppo per tag
    VIEW_FILTER,   // script che appartengono al gruppo scelto
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
    int entry[MAX_ENTRIES + MAX_GROUPS + 3]; // indice in e[] per ROW_ENTRY
} listing_t;

static const char *const OS_NAMES[] = {"windows", "linux", "mac"};
static const char *const OS_SHORT[] = {"win", "lin", "mac"};

// Layout e OS effettivi dello script: intestazione, altrimenti config.lua.
static const char *eff_layout(const payload_entry_t *e)
{
    return e->layout[0] ? e->layout : config_str("keyboard.layout", "us");
}

static int eff_os(const payload_entry_t *e)
{
    const char *os = e->os[0] ? e->os : config_str("keyboard.os", "windows");
    for (int i = 0; i < 3; i++) {
        if (!strcasecmp(os, OS_NAMES[i])) return i;
    }
    return 0;
}

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

// Nome corto del layout per la colonna destra: "colemak" è troppo largo.
static const char *layout_short(const char *l)
{
    return strcasecmp(l, "colemak") == 0 ? "cmk" : l;
}

// Colonna destra di uno script: "it/win"; nelle viste per OS basta il layout.
static void entry_right(const payload_entry_t *e, bool show_os, char *out, size_t len)
{
    const char *l = layout_short(eff_layout(e));
    if (show_os) snprintf(out, len, "%s/%s", l, OS_SHORT[eff_os(e)]);
    else strlcpy(out, l, len);
}

// Tag distinti presenti negli script, in ordine alfabetico.
static int collect_tags(const payload_entry_t *e, int n, char tags[][16], int max)
{
    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *p = e[i].tags;
        while (*p) {
            while (*p == ',' || isspace((unsigned char)*p)) p++;
            const char *q = p;
            while (*q && *q != ',' && !isspace((unsigned char)*q)) q++;
            if (q > p && count < max) {
                char t[16];
                strlcpy(t, p, (size_t)(q - p) + 1 < sizeof(t) ? (size_t)(q - p) + 1 : sizeof(t));
                bool dup = false;
                for (int j = 0; j < count; j++) dup |= !strcasecmp(tags[j], t);
                if (!dup) strlcpy(tags[count++], t, 16);
            }
            p = q;
        }
    }
    for (int i = 1; i < count; i++) { // insertion sort: max 16 elementi
        char t[16];
        strlcpy(t, tags[i], sizeof(t));
        int j = i - 1;
        for (; j >= 0 && strcasecmp(tags[j], t) > 0; j--) strlcpy(tags[j + 1], tags[j], 16);
        strlcpy(tags[j + 1], t, 16);
    }
    return count;
}

static void injector_run(app_ctx_t *ctx)
{
    // Autorun: lo script è indicato dall'argomento, relativo a /payloads
    if (ctx->arg && *ctx->arg) {
        payload_meta_t *m = calloc(1, sizeof(*m));
        if (!m) return;
        char path[PAYLOAD_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", PAYLOAD_DIR, ctx->arg);
        if (!payload_read_meta(path, m)) app_message(ctx, "Autorun", "Script not found", UI_ERR);
        else if (m->error[0]) app_message(ctx, "Header error", m->error, UI_ERR);
        else run_payload(ctx, m);
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
    char filter_tag[16] = "";
    int sel = 0;

    for (;;) {
        bool at_root = strcmp(dir, PAYLOAD_DIR) == 0;
        int n = 0; // righe
        const char *title = "Payloads";
        bool os_view = view == VIEW_FILTER && parent_view == VIEW_OS_LIST;

        if (view == VIEW_DIR) {
            int ne = payload_list(dir, ls->e, MAX_ENTRIES);
            if (ne < 0) {
                app_message(ctx, "Injector", "No /payloads folder on the SD", UI_ERR);
                break;
            }
            if (!at_root) {
                add_row(ls, &n, ROW_UP, "..", NULL, -1);
                title = strrchr(dir, '/') + 1;
            } else {
                // Le viste virtuali stanno in cima, solo alla radice e solo se c'è qualcosa da raggruppare
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
            add_row(ls, &n, ROW_UP, "..", NULL, -1);
            if (view == VIEW_OS_LIST) {
                title = "By OS";
                for (int o = 0; o < 3; o++) {
                    int c = 0;
                    for (int i = 0; i < ne; i++) c += eff_os(&ls->e[i]) == o;
                    char cnt[12];
                    snprintf(cnt, sizeof(cnt), "%d", c);
                    if (c) add_row(ls, &n, ROW_GROUP, OS_NAMES[o], cnt, o);
                }
            } else if (view == VIEW_TAG_LIST) {
                title = "By tag";
                char tags[MAX_GROUPS][16];
                int nt = collect_tags(ls->e, ne, tags, MAX_GROUPS);
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
                    bool match = os_view ? eff_os(&ls->e[i]) == filter_os : payload_has_tag(ls->e[i].tags, filter_tag);
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
            if (ls->entry[sel] < 0) continue; // riga informativa
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
            run_payload(ctx, m);
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
    // 16 KB: con 10 KB uno script arrivava a circa 6 livelli di pcall
    // annidati prima della guardia dello stack (vedi luart.c).
    .stack = 16384,
    .run = injector_run,
    .subtitle = injector_subtitle,
};
