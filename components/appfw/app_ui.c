// Dialoghi bloccanti comuni a tutte le app: menu, conferma, messaggio.

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "app.h"
#include "ui.h"

// Eventi non gestiti da un dialogo (USB, rete, TICK, USER). Prima venivano
// scartati: un'espulsione del disco arrivata mentre era aperta una conferma
// andava persa e l'app restava ad aspettarla.
typedef struct {
    app_event_t ev[8];
    int n;
} stash_t;

static void stash_put(stash_t *s, const app_event_t *ev)
{
    // Una risposta esterna in ritardo non deve rispondere al dialogo successivo
    if (ev->type == APP_EV_CLICK || ev->type == APP_EV_DOUBLE || ev->type == APP_EV_LONG || ev->type == APP_EV_EXIT ||
        ev->type == APP_EV_ANSWER) {
        return;
    }
    if (s->n < (int)(sizeof(s->ev) / sizeof(s->ev[0]))) s->ev[s->n++] = *ev;
}

// Rimette gli eventi in testa alla coda, nell'ordine in cui erano arrivati.
static void stash_restore(app_ctx_t *ctx, stash_t *s)
{
    for (int i = s->n - 1; i >= 0; i--) xQueueSendToFront(ctx->queue, &s->ev[i], 0);
    s->n = 0;
}

typedef struct {
    const char *const *items;
    const char *const *rights;
} menu_t;

static const char *menu_label(int i, void *arg, const char **right, bool *enabled)
{
    const menu_t *m = arg;
    if (m->rights) *right = m->rights[i];
    return m->items[i];
}

int app_menu(app_ctx_t *ctx, const char *title, const char *const *items, int count, int sel)
{
    return app_menu_ex(ctx, title, items, NULL, count, sel);
}

int app_menu_ex(app_ctx_t *ctx, const char *title, const char *const *items, const char *const *rights, int count,
                int sel)
{
    return app_menu_timeout(ctx, title, items, rights, count, sel, 0);
}

int app_menu_timeout(app_ctx_t *ctx, const char *title, const char *const *items, const char *const *rights, int count,
                     int sel, uint32_t timeout_ms)
{
    if (count <= 0) return -1;
    int64_t deadline = timeout_ms ? esp_timer_get_time() + timeout_ms * 1000LL : 0;
    int shown_secs = -1;
    menu_t m = {items, rights};
    int top = 0;
    // Il titolo del menu va nella status bar; poi si ripristina quello di
    // prima (non il nome dell'app: un menu può essere aperto da un altro).
    char saved[24];
    uint32_t saved_accent = ui_accent();
    ui_get_title(saved, sizeof(saved));
    if (title) ui_set_title(title, ctx->app->accent);
    stash_t st = {0};
    int result = -1;
    ui_marquee_reset();
    for (;;) {
        if (deadline) {
            // Secondi residui nel titolo, aggiornati solo quando cambiano
            int secs = (int)((deadline - esp_timer_get_time() + 999999) / 1000000);
            if (secs != shown_secs) {
                // Più grande del titolo (24): il troncamento lo fa ui_set_title
                char t[40];
                snprintf(t, sizeof(t), "%s %ds", title ? title : saved, secs);
                ui_set_title(t, ctx->app->accent);
                shown_secs = secs;
            }
        }
        top = ui_list_scroll(top, sel, count);
        ui_begin();
        ui_draw_list(top, sel, count, menu_label, &m);
        ui_end();

        app_event_t ev;
        // ~30 fotogrammi al secondo mentre la riga selezionata scorre
        uint32_t wait = ui_marquee_active() ? 33 : (deadline ? 250 : portMAX_DELAY);
        if (!app_next_event(ctx, &ev, wait)) {
            if (deadline && esp_timer_get_time() >= deadline) {
                result = sel; // scaduto: vale la voce selezionata
                break;
            }
            continue;
        }
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE) {
            if (deadline) {
                // L'utente è presente: niente più scadenza, titolo normale
                deadline = 0;
                ui_set_title(title ? title : saved, ctx->app->accent);
            }
        }
        if (ev.type == APP_EV_CLICK) sel = (sel + 1) % count;
        else if (ev.type == APP_EV_DOUBLE) sel = (sel + count - 1) % count;
        else if (ev.type == APP_EV_LONG) {
            result = sel;
            break;
        } else if (ev.type == APP_EV_ANSWER && ev.arg >= 0 && ev.arg < count) {
            result = ev.arg;
            break;
        } else stash_put(&st, &ev);
    }
    if (title || timeout_ms) ui_set_title(saved, saved_accent);
    stash_restore(ctx, &st);
    return result;
}

static void draw_button(int x, int y, int w, const char *label, bool selected, uint32_t accent)
{
    if (selected) gfx_round_rect(x, y, w, 13, 4, accent, true);
    else gfx_round_rect(x, y, w, 13, 4, UI_FAINT, false);
    gfx_text_aligned(&gfx_font_small, x, y + 3, w, GFX_ALIGN_CENTER, label, selected ? UI_BG : UI_DIM);
}

bool app_confirm(app_ctx_t *ctx, const char *title, const char *text, bool yes)
{
    return app_confirm_timeout(ctx, title, text, yes, 0);
}

bool app_confirm_timeout(app_ctx_t *ctx, const char *title, const char *text, bool yes, uint32_t timeout_ms)
{
    uint32_t accent = ctx->app->accent;
    int64_t deadline = timeout_ms ? esp_timer_get_time() + timeout_ms * 1000LL : 0;
    // 3 righe tra il titolo e i pulsanti; la barra di scorrimento sta nel margine destro
    ui_scroll_t sc;
    ui_scroll_init(&sc, &gfx_font_small, 6, UI_CONTENT_Y + 13, UI_W - 12, 3, text);
    stash_t st = {0};
    bool result;
    for (;;) {
        ui_begin();
        char left[8] = "";
        if (deadline) snprintf(left, sizeof(left), "%ds", (int)((deadline - esp_timer_get_time() + 999999) / 1000000));
        // Il conto alla rovescia sta a destra del titolo, che cede lo spazio
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12 - (deadline ? 30 : 0), title, accent);
        if (deadline) gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 2, UI_W - 6, GFX_ALIGN_RIGHT, left, UI_DIM);
        ui_scroll_draw(&sc, UI_TEXT);
        draw_button(24, UI_H - 16, 50, "No", !yes, accent);
        draw_button(86, UI_H - 16, 50, "Yes", yes, accent);
        ui_end();

        app_event_t ev;
        // Scatto di 250 ms per aggiornare i secondi residui; altrimenti ci si
        // sveglia solo per il prossimo passo dello scorrimento.
        uint32_t wait = ui_scroll_wait_ms(&sc);
        if (deadline && wait > 250) wait = 250;
        if (!app_next_event(ctx, &ev, wait)) {
            ui_scroll_tick(&sc);
            if (deadline && esp_timer_get_time() >= deadline) {
                result = yes; // scaduto: vale la scelta predefinita
                break;
            }
            continue;
        }
        if (ev.type == APP_EV_EXIT) {
            result = false;
            break;
        }
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE) {
            yes = !yes;
            deadline = 0; // l'utente è presente: niente più scadenza
        } else if (ev.type == APP_EV_LONG) {
            result = yes;
            break;
        } else if (ev.type == APP_EV_ANSWER) {
            result = ev.arg != 0;
            break;
        } else stash_put(&st, &ev);
    }
    stash_restore(ctx, &st);
    return result;
}

void app_message(app_ctx_t *ctx, const char *title, const char *text, uint32_t color)
{
    ui_scroll_t sc;
    ui_scroll_init(&sc, &gfx_font_small, 6, UI_CONTENT_Y + 13, UI_W - 12, 5, text);
    stash_t st = {0};
    for (;;) {
        ui_begin();
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12, title, color);
        ui_scroll_draw(&sc, UI_TEXT);
        ui_end();
        app_event_t ev;
        if (!app_next_event(ctx, &ev, ui_scroll_wait_ms(&sc))) {
            ui_scroll_tick(&sc);
            continue;
        }
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_LONG || ev.type == APP_EV_EXIT || ev.type == APP_EV_ANSWER) break;
        stash_put(&st, &ev);
    }
    stash_restore(ctx, &st);
}

// ---------------------------------------------------------------------------
// Scelta della rete

#define NET_CHOICE_MS 6000 // senza risposta: la rete più forte

static int choose_network(const net_candidate_t *c, int n, void *arg)
{
    app_ctx_t *ctx = arg;
    const char *items[NET_MAX_CANDIDATES];
    char rssi[NET_MAX_CANDIDATES][8];
    const char *rights[NET_MAX_CANDIDATES];
    for (int i = 0; i < n; i++) {
        items[i] = c[i].ssid;
        snprintf(rssi[i], sizeof(rssi[i]), "%d", c[i].rssi);
        rights[i] = rssi[i];
    }
    int sel = app_menu_timeout(ctx, "WiFi", items, rights, n, 0, NET_CHOICE_MS);
    if (sel >= 0) {
        // La connessione dura qualche secondo: il menu non deve restare a schermo
        ui_begin();
        gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 18, UI_W, GFX_ALIGN_CENTER, "Connecting to", UI_DIM);
        gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 30, UI_W, GFX_ALIGN_CENTER, c[sel].ssid, UI_TEXT);
        ui_end();
    }
    return sel;
}

esp_err_t app_net_acquire(app_ctx_t *ctx, net_mode_t mode, uint32_t timeout_ms)
{
    if (app_should_exit(ctx)) return ESP_ERR_NOT_FINISHED;
    return net_acquire_ex(mode, timeout_ms, choose_network, ctx, &ctx->exit_requested);
}
