// USB disk: espone la microSD al PC come disco (MSC).
//
// Il sistema presta la scheda all'host prima di avviare l'app (profilo
// USB_PROFILE_MSC) e la riprende all'uscita. L'app mostra lo stato e
// protegge dall'uscita senza espulsione, che può corrompere il filesystem.

#include <stdio.h>
#include "esp_timer.h"
#include "icons.h"
#include "led.h"
#include "storage.h"
#include "ui.h"
#include "usbdisk.h"
#include "usbdev.h"

#define ACCENT 0xF0A030

static void draw(bool mounted, bool ejected)
{
    ui_begin();
    uint32_t c = ejected ? UI_OK : (mounted ? ACCENT : UI_DIM);
    gfx_icon(&icon_disk, 8, UI_CONTENT_Y + 6, 3, c);

    const char *title = ejected ? "Ejected" : (mounted ? "Connected" : "Waiting…");
    const char *line1 = ejected ? "Safe to exit" : (mounted ? "SD on the PC" : "Plug into a PC");
    const char *line2 = ejected ? "" : (mounted ? "Eject it first" : "");
    gfx_text(&gfx_font_large, 50, UI_CONTENT_Y + 6, title, c);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 26, line1, UI_TEXT);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 36, line2, UI_DIM);

    if (mounted && !ejected) {
        // Tre puntini che scorrono: segnala che il disco è "vivo"
        int phase = (esp_timer_get_time() / 250000) % 3;
        for (int i = 0; i < 3; i++) gfx_fill_rect(50 + i * 6, UI_CONTENT_Y + 48, 3, 3, i == phase ? ACCENT : UI_FAINT);
    }
    ui_draw_hint(ejected ? "hold to exit" : "hold 3s to exit");
    ui_end();
}

static void usbdisk_run(app_ctx_t *ctx)
{
    bool ejected = false;
    for (;;) {
        bool mounted = usbdev_mounted();
        led_set(LED_LAYER_APP, ejected ? LED_FX_SOLID : (mounted ? LED_FX_PULSE : LED_FX_BLINK),
                ejected ? UI_OK : ACCENT, mounted ? 1500 : 800);
        draw(mounted, ejected);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 250)) continue;
        if (ev.type == APP_EV_USB) {
            if (ev.arg == USBDEV_EV_MSC_EJECTED) {
                usbdev_msc_detach();
                ejected = true;
                ui_toast("Disk ejected", UI_OK, 1500);
            } else if (ev.arg == USBDEV_EV_UNMOUNTED) {
                // Scollegato dal PC: se non era stato espulso non possiamo
                // farci nulla, ma almeno lo segnaliamo.
                if (!ejected) ui_toast("Host gone", UI_WARN, 2000);
            }
        } else if (ev.type == APP_EV_EXIT) {
            if (ejected || !usbdev_mounted()) return;
            if (app_confirm(ctx, "Not ejected!", "Exit anyway? Unsaved data on the SD may be lost.", false)) return;
            // Uscita annullata: si resta nell'app
            ctx->exit_requested = false;
        }
    }
}

static void usbdisk_subtitle(char *buf, size_t len)
{
    uint64_t total, free_b;
    if (storage_info(&total, &free_b)) {
        snprintf(buf, len, "%.1f GB free of %.1f", free_b / 1e9, total / 1e9);
    } else {
        snprintf(buf, len, "SD %s", storage_state_name(storage_state()));
    }
}

const app_desc_t usbdisk_app = {
    .id = "usbdisk",
    .name = "USB Disk",
    .icon = &icon_disk,
    .accent = ACCENT,
    // Basta che la scheda ci sia: anche se non è FAT32 la si può
    // riformattare dal PC proprio tramite questa app.
    .requires = APP_REQ_CARD,
    .usb = USB_PROFILE_MSC,
    .stack = 4096,
    .run = usbdisk_run,
    .subtitle = usbdisk_subtitle,
};
