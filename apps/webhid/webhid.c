// Web HID: tastiera e mouse del PC comandati da un browser sulla stessa rete.
//
// Il dongle prende un lease di rete (rete nota da config.lua, altrimenti
// hotspot), avvia il server e mostra su LCD un QR code, l'IP e un PIN a 6
// cifre generato a ogni avvio. In modalità hotspot il QR ha due pagine:
// prima le credenziali WiFi, poi l'indirizzo della pagina.

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_random.h"
#include "hid.h"
#include "icons.h"
#include "led.h"
#include "net.h"
#include "ui.h"
#include "usbdev.h"
#include "web_server.h"
#include "websrv.h"
#include "webhid.h"
#include "webui.h"

#define ACCENT 0x4070FF

// Tempo in ms da config.lua, limitato a 0..60000 (i campi di hid_timing_t
// sono uint16_t: un -1 diventava 65535).
static int config_ms(const char *key, int def)
{
    int v = config_int(key, def);
    return v < 0 ? 0 : (v > 60000 ? 60000 : v);
}

static void apply_keyboard_config(void)
{
    hid_reset();
    hid_set_layout(hid_layout_find(config_str("keyboard.layout", "us")));
    hid_os_t os;
    if (hid_os_parse(config_str("keyboard.os", "windows"), &os)) hid_set_os(os);
    hid_timing_t *t = hid_timing();
    t->key_delay = config_ms("keyboard.key_delay", 8);
    t->hold = config_ms("keyboard.hold", 5);
    t->jitter = config_ms("keyboard.jitter", 0);
    // Anche line_delay: la documentazione dice che valgono tutte le keyboard.*
    t->line_delay = config_ms("keyboard.line_delay", 0);
}

static void webhid_run(app_ctx_t *ctx)
{
    webui_draw_connecting(&icon_mouse, ACCENT);
    esp_err_t err = app_net_acquire(ctx, NET_AUTO, 15000);
    if (err == ESP_ERR_NOT_FINISHED) return; // uscita durante la scelta della rete
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "WiFi failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web HID", msg, UI_ERR);
        return;
    }

    apply_keyboard_config();
    // webhid.pin = false: nessun PIN, chiunque sulla rete del dongle comanda il PC
    char pin[8] = "";
    if (config_bool("webhid.pin", true)) snprintf(pin, sizeof(pin), "%06u", (unsigned)(esp_random() % 1000000));
    err = web_start(pin);
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web HID", msg, UI_ERR);
        return;
    }

    led_set(LED_LAYER_APP, LED_FX_PULSE, ACCENT, 2000);
    // Pagina del QR: credenziali o indirizzo, vedi webui.h
    webui_qr_t qr = WEBUI_QR_INIT;
    char last_client[16] = "";
    for (;;) {
        net_info_t ni;
        net_get_info(&ni);
        const char *client = websrv_client_ip();
        if (strcmp(client, last_client) != 0) {
            strlcpy(last_client, client, sizeof(last_client));
            led_set(LED_LAYER_APP, *client ? LED_FX_SOLID : LED_FX_PULSE, *client ? UI_OK : ACCENT, 2000);
        }
        webui_qr_update(&qr, &ni);
        if (ni.up) webui_draw_main(&ni, pin, qr.page, client, ACCENT);
        else webui_draw_connecting(&icon_mouse, ACCENT);

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK) webui_qr_toggle(&qr, &ni);
    }
    web_stop();
}

static void webhid_subtitle(char *buf, size_t len)
{
    snprintf(buf, len, "%s · %s", config_str("keyboard.layout", "us"), config_count("wifi") ? "WiFi" : "hotspot");
}

const app_desc_t webhid_app = {
    .id = "webhid",
    .name = "Web HID",
    .icon = &icon_mouse,
    .accent = ACCENT,
    .requires = APP_REQ_NET,
    .usb = USB_PROFILE_HID,
    .stack = 6144,
    .run = webhid_run,
    .subtitle = webhid_subtitle,
};
