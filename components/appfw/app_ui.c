// Dialoghi bloccanti comuni a tutte le app: menu, conferma, messaggio.

#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "app.h"
#include "ui.h"

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
    if (count <= 0) return -1;
    menu_t m = {items, rights};
    int top = 0;
    char saved[24];
    // Il titolo del menu va nella status bar; poi si ripristina il nome dell'app
    strlcpy(saved, ctx->app->name, sizeof(saved));
    if (title) ui_set_title(title, ctx->app->accent);
    int result = -1;
    for (;;) {
        top = ui_list_scroll(top, sel, count);
        ui_begin();
        ui_draw_list(top, sel, count, menu_label, &m);
        ui_end();

        app_event_t ev;
        if (!app_next_event(ctx, &ev, portMAX_DELAY)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK) sel = (sel + 1) % count;
        else if (ev.type == APP_EV_DOUBLE) sel = (sel + count - 1) % count;
        else if (ev.type == APP_EV_LONG) {
            result = sel;
            break;
        }
    }
    if (title) ui_set_title(saved, ctx->app->accent);
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
    for (;;) {
        ui_begin();
        char left[8] = "";
        if (deadline) snprintf(left, sizeof(left), "%ds", (int)((deadline - esp_timer_get_time() + 999999) / 1000000));
        // Il conto alla rovescia sta a destra del titolo, che cede lo spazio
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12 - (deadline ? 30 : 0), title, accent);
        if (deadline) gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 2, UI_W - 6, GFX_ALIGN_RIGHT, left, UI_DIM);
        ui_draw_wrapped(&gfx_font_small, 6, UI_CONTENT_Y + 13, UI_W - 12, 3, text, UI_TEXT);
        draw_button(24, UI_H - 16, 50, "No", !yes, accent);
        draw_button(86, UI_H - 16, 50, "Yes", yes, accent);
        ui_end();

        app_event_t ev;
        // Scatto di 250 ms per aggiornare i secondi residui
        if (!app_next_event(ctx, &ev, deadline ? 250 : portMAX_DELAY)) {
            if (deadline && esp_timer_get_time() >= deadline) return yes; // scaduto: vale la scelta predefinita
            continue;
        }
        if (ev.type == APP_EV_EXIT) return false;
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE) {
            yes = !yes;
            deadline = 0; // l'utente è presente: niente più scadenza
        } else if (ev.type == APP_EV_LONG) return yes;
    }
}

void app_message(app_ctx_t *ctx, const char *title, const char *text, uint32_t color)
{
    ui_begin();
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12, title, color);
    ui_draw_wrapped(&gfx_font_small, 6, UI_CONTENT_Y + 13, UI_W - 12, 5, text, UI_TEXT);
    ui_end();
    app_event_t ev;
    while (app_next_event(ctx, &ev, portMAX_DELAY)) {
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_LONG || ev.type == APP_EV_EXIT) return;
    }
}
