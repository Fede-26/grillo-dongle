// Esecuzione di uno script con la sua schermata e l'esito: comune
// all'Injector e al Web Injector, che differiscono solo per come si sceglie
// lo script.

#include <stdio.h>
#include <stdlib.h>
#include "engine.h"
#include "esp_timer.h"
#include "led.h"
#include "ui.h"
#include "usbdev.h"

static const inj_engine_t *const s_engines[] = {&engine_lua};

static const inj_engine_t *find_engine(const char *path)
{
    for (size_t i = 0; i < sizeof(s_engines) / sizeof(s_engines[0]); i++) {
        if (s_engines[i]->can_run(path)) return s_engines[i];
    }
    return NULL;
}

void inj_redraw(inj_run_t *r)
{
    uint32_t accent = r->ctx->app->accent;
    ui_begin();
    if (r->waiting_host) {
        gfx_text(&gfx_font_large, 6, UI_CONTENT_Y + 4, "Waiting", accent);
        gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 24, "for the USB host…", UI_TEXT);
    } else {
        int secs = (esp_timer_get_time() - r->started_us) / 1000000;
        char t[16];
        snprintf(t, sizeof(t), "%d:%02d", secs / 60, secs % 60);
        gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 40, r->meta->name[0] ? r->meta->name : "Script", accent);
        gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 2, UI_W - 6, GFX_ALIGN_RIGHT, t, UI_DIM);
        // Lo scroller va reinizializzato quando lo script cambia lo stato
        if (r->status_scroll.text != r->status) ui_scroll_init(&r->status_scroll, &gfx_font_small, 6, UI_CONTENT_Y + 16,
                                                             UI_W - 12, 2, r->status);
        ui_scroll_draw(&r->status_scroll, UI_TEXT);
        if (r->progress_max > 0) ui_draw_progress(6, UI_CONTENT_Y + 38, UI_W - 12, 5, r->progress, r->progress_max, accent);
    }
    ui_draw_hint("hold 3s to abort");
    ui_end();
}

bool inj_pump(inj_run_t *r)
{
    app_event_t ev;
    // Una volta al secondo (cronometro) o al passo dello scorrimento dello
    // stato: prima la schermata restava ferma durante un kb.type lungo.
    int64_t now = esp_timer_get_time();
    bool step = ui_scroll_tick(&r->status_scroll);
    if (!r->waiting_host && (step || now >= r->redraw_us)) {
        r->redraw_us = now + 1000000;
        inj_redraw(r);
    }
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

void inj_run_payload(app_ctx_t *ctx, const payload_meta_t *m, const inj_hooks_t *hooks)
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
    r->hooks = hooks;
    r->started_us = esp_timer_get_time();
    eng->run(r);
    // Prima di annullare l'uscita: da qui il front-end non chiede più l'abort
    // (EXIT), quindi un abort arrivato mentre lo script finiva viene tolto
    // sotto e non chiude l'app.
    INJ_HOOK(r, done, r);
    // L'hold 3 s (o il timeout) ha solo interrotto lo script: si resta
    // nell'app, e un secondo hold dalla lista esce. L'uscita va annullata
    // prima dell'esito: con l'EXIT ancora in coda il messaggio si chiudeva
    // subito e "Aborted" non si vedeva.
    app_cancel_exit(ctx);

    if (r->ok) {
        led_set(LED_LAYER_APP, LED_FX_SOLID, UI_OK, 0);
        app_message(ctx, "Done", m->name[0] ? m->name : "Script finished", UI_OK);
    } else {
        led_set(LED_LAYER_APP, LED_FX_BLINK, UI_ERR, 400);
        const char *why = r->timed_out ? "timeout" : (r->error[0] ? r->error : "failed");
        app_message(ctx, r->aborted && !r->timed_out ? "Aborted" : "Error", why, UI_ERR);
    }
    INJ_HOOK(r, dialog_end);
    led_clear(LED_LAYER_APP);
    free(r);
}
