// Launcher: carosello delle app. Click = successiva, doppio = precedente,
// lungo = avvia. Al primo avvio gestisce anche l'autorun da config.lua.

#include <stdio.h>
#include <string.h>
#include "appmgr.h"
#include "config.h"
#include "esp_timer.h"
#include "launcher.h"
#include "led.h"
#include "ui.h"

#define ANIM_FRAMES 7

static int s_sel;
static bool s_autorun_done;

static void draw_card(const app_desc_t *app, int xoff)
{
    const char *why = appmgr_unavailable(app);
    uint32_t accent = why ? UI_FAINT : app->accent;
    int cx = UI_W / 2 + xoff;

    if (app->icon) gfx_icon(app->icon, cx - app->icon->w, UI_CONTENT_Y + 1, 2, accent);
    int nw = gfx_text_width(&gfx_font_large, app->name);
    gfx_text(&gfx_font_large, cx - nw / 2, UI_CONTENT_Y + 26, app->name, why ? UI_DIM : UI_TEXT);

    char sub[40] = "";
    if (why) strlcpy(sub, why, sizeof(sub));
    else if (app->subtitle) app->subtitle(sub, sizeof(sub));
    int sw = gfx_text_width(&gfx_font_small, sub);
    if (sw > UI_W - 24) sw = UI_W - 24;
    gfx_text_fit(&gfx_font_small, cx - sw / 2, UI_CONTENT_Y + 44, UI_W - 24, sub, why ? UI_ERR : UI_DIM);
}

static void draw(int sel, int prev, int dir, int frame)
{
    int n = appmgr_count();
    ui_begin();
    if (frame < ANIM_FRAMES && prev >= 0) {
        // Ease-out: il movimento rallenta verso la fine
        int t = (frame + 1) * 256 / ANIM_FRAMES;
        int k = 256 - (256 - t) * (256 - t) / 256;
        int shift = UI_W * k / 256;
        draw_card(appmgr_get(prev), -dir * shift);
        draw_card(appmgr_get(sel), dir * (UI_W - shift));
    } else {
        draw_card(appmgr_get(sel), 0);
    }
    if (n > 1) {
        gfx_text(&gfx_font_small, 3, UI_CONTENT_Y + 30, "‹", UI_FAINT);
        gfx_text(&gfx_font_small, UI_W - 7, UI_CONTENT_Y + 30, "›", UI_FAINT);
    }
    ui_draw_pager(UI_H - 4, n, sel, appmgr_get(sel)->accent);
    ui_end();
}

static void set_ambient(const app_desc_t *app)
{
    // Stessa intensità dell'ambient di un'app in esecuzione (appmgr.c): più
    // bassa, il colore dell'app selezionata si riconosceva a fatica.
    led_set(LED_LAYER_AMBIENT, LED_FX_SOLID, gfx_mix(0, app->accent, 90), 0);
}

static void animate(int from, int to, int dir)
{
    for (int f = 0; f < ANIM_FRAMES; f++) draw(to, from, dir, f);
}

// Countdown dell'autorun: un click annulla. true se l'app va avviata.
static bool autorun_countdown(app_ctx_t *ctx, const app_desc_t *app, const char *arg, int secs)
{
    int64_t end = esp_timer_get_time() + secs * 1000000LL;
    led_set(LED_LAYER_APP, LED_FX_PULSE, app->accent, 700);
    for (;;) {
        int64_t left = end - esp_timer_get_time();
        if (left <= 0) break;
        ui_begin();
        gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 2, UI_W, GFX_ALIGN_CENTER, "Autorun", UI_DIM);
        gfx_text_aligned(&gfx_font_large, 0, UI_CONTENT_Y + 12, UI_W, GFX_ALIGN_CENTER, app->name, app->accent);
        if (arg) gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 30, UI_W, GFX_ALIGN_CENTER, arg, UI_TEXT);
        ui_draw_progress(20, UI_CONTENT_Y + 43, UI_W - 40, 5, (int)(secs * 1000000LL - left), secs * 1000000, app->accent);
        ui_draw_hint("click to cancel");
        ui_end();
        app_event_t ev;
        if (app_next_event(ctx, &ev, 50) && (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE)) {
            led_clear(LED_LAYER_APP);
            ui_toast("Autorun cancelled", UI_WARN, 1500);
            return false;
        }
    }
    led_clear(LED_LAYER_APP);
    return true;
}

static void launcher_run(app_ctx_t *ctx)
{
    ui_set_title("Grillo", UI_GRILLO);
    int n = appmgr_count();
    if (n == 0) {
        app_message(ctx, "Grillo", "No apps installed", UI_ERR);
        return;
    }

    if (!s_autorun_done) {
        s_autorun_done = true;
        const char *id = config_str("autorun.app", NULL);
        const app_desc_t *app = id ? appmgr_find(id) : NULL;
        if (id && !app) ui_toast("autorun: unknown app", UI_ERR, 2500);
        if (app && !appmgr_unavailable(app)) {
            const char *arg = config_str("autorun.payload", NULL);
            if (autorun_countdown(ctx, app, arg, config_int("autorun.delay", 3))) {
                appmgr_launch(app, arg);
                return;
            }
        }
    }

    if (s_sel >= n) s_sel = 0;
    set_ambient(appmgr_get(s_sel));
    draw(s_sel, -1, 0, ANIM_FRAMES);
    for (;;) {
        app_event_t ev;
        // Timeout: i sottotitoli (es. spazio SD) vengono aggiornati ogni 2 s
        if (!app_next_event(ctx, &ev, 2000)) {
            draw(s_sel, -1, 0, ANIM_FRAMES);
            continue;
        }
        int prev = s_sel;
        switch (ev.type) {
        case APP_EV_EXIT:
            // Solo dalla console (comando launch): l'app da avviare è già impostata
            return;
        case APP_EV_CLICK:
            s_sel = (s_sel + 1) % n;
            animate(prev, s_sel, 1);
            set_ambient(appmgr_get(s_sel));
            break;
        case APP_EV_DOUBLE:
            s_sel = (s_sel + n - 1) % n;
            animate(prev, s_sel, -1);
            set_ambient(appmgr_get(s_sel));
            break;
        case APP_EV_LONG: {
            const app_desc_t *app = appmgr_get(s_sel);
            const char *why = appmgr_unavailable(app);
            if (why) {
                ui_toast(why, UI_ERR, 2000);
                led_flash(UI_ERR, 200);
                break;
            }
            appmgr_launch(app, NULL);
            return;
        }
        default:
            draw(s_sel, -1, 0, ANIM_FRAMES);
            break;
        }
    }
}

const app_desc_t launcher_app = {
    .id = "launcher",
    .name = "Grillo",
    .accent = UI_GRILLO,
    .usb = USB_PROFILE_NONE,
    .stack = 4096,
    .run = launcher_run,
};
