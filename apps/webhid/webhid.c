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
#include "qr.h"
#include "ui.h"
#include "usbdev.h"
#include "web_server.h"
#include "webhid.h"

#define ACCENT 0x30B0F0
#define QR_BOX 66 // tutta l'altezza dell'area contenuto

static void draw_connecting(void)
{
    ui_begin();
    gfx_icon(&icon_mouse, 8, UI_CONTENT_Y + 6, 3, ACCENT);
    gfx_text(&gfx_font_large, 50, UI_CONTENT_Y + 6, "WiFi…", ACCENT);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 28, "Looking for", UI_TEXT);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 38, "a known network", UI_TEXT);
    ui_end();
}

// page 0: URL della pagina (STA e AP); page 1 (solo AP): credenziali WiFi
static void draw_main(const net_info_t *ni, const char *pin, int page, const char *client)
{
    char url[48];
    snprintf(url, sizeof(url), "http://%s/", ni->ip);
    char wifi[128];
    snprintf(wifi, sizeof(wifi), "WIFI:T:WPA;S:%s;P:%s;;", net_ap_ssid(), net_ap_pass());

    ui_begin();
    bool ok = qr_draw(page == 1 ? wifi : url, 0, UI_CONTENT_Y, QR_BOX);
    if (!ok) {
        gfx_rect(2, UI_CONTENT_Y + 2, QR_BOX - 4, QR_BOX - 4, UI_FAINT);
        gfx_text_aligned(&gfx_font_small, 2, UI_CONTENT_Y + 28, QR_BOX - 4, GFX_ALIGN_CENTER, "no QR", UI_DIM);
    }

    int x = QR_BOX + 4, w = UI_W - x - 2, y = UI_CONTENT_Y;
    if (page == 1) {
        gfx_text_fit(&gfx_font_small, x, y, w, "Join WiFi:", UI_DIM);
        gfx_text_fit(&gfx_font_small, x, y + 10, w, net_ap_ssid(), UI_TEXT);
        gfx_text_fit(&gfx_font_small, x, y + 20, w, net_ap_pass(), UI_DIM);
    } else {
        gfx_text_fit(&gfx_font_small, x, y, w, ni->mode == NET_AP ? net_ap_ssid() : ni->ssid, UI_DIM);
        gfx_text_fit(&gfx_font_small, x, y + 10, w, ni->ip, UI_TEXT);
    }
    gfx_text(&gfx_font_small, x, y + 31, "PIN", UI_DIM);
    gfx_text(&gfx_font_large, x, y + 40, pin, ACCENT);
    if (*client) gfx_text_fit(&gfx_font_small, x, y + 58, w, client, UI_OK);
    else ui_draw_hint(ni->mode == NET_AP ? "click: wifi/url" : "hold 3s to exit");
    ui_end();
}

static void apply_keyboard_config(void)
{
    hid_reset();
    hid_set_layout(hid_layout_find(config_str("keyboard.layout", "us")));
    hid_os_t os;
    if (hid_os_parse(config_str("keyboard.os", "windows"), &os)) hid_set_os(os);
    hid_timing_t *t = hid_timing();
    t->key_delay = config_int("keyboard.key_delay", 8);
    t->hold = config_int("keyboard.hold", 5);
    t->jitter = config_int("keyboard.jitter", 0);
}

static void webhid_run(app_ctx_t *ctx)
{
    draw_connecting();
    esp_err_t err = net_acquire(NET_AUTO, 15000);
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "WiFi failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web HID", msg, UI_ERR);
        return;
    }

    apply_keyboard_config();
    char pin[8];
    snprintf(pin, sizeof(pin), "%06u", (unsigned)(esp_random() % 1000000));
    err = web_start(pin);
    if (err != ESP_OK) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server failed: %s", esp_err_to_name(err));
        app_message(ctx, "Web HID", msg, UI_ERR);
        return;
    }

    led_set(LED_LAYER_APP, LED_FX_PULSE, ACCENT, 2000);
    int page = 0;
    char last_client[16] = "";
    for (;;) {
        net_info_t ni;
        net_get_info(&ni);
        const char *client = web_client_ip();
        if (strcmp(client, last_client) != 0) {
            strlcpy(last_client, client, sizeof(last_client));
            led_set(LED_LAYER_APP, *client ? LED_FX_SOLID : LED_FX_PULSE, *client ? UI_OK : ACCENT, 2000);
        }
        if (ni.up) draw_main(&ni, pin, page, client);
        else draw_connecting();

        app_event_t ev;
        if (!app_next_event(ctx, &ev, 500)) continue;
        if (ev.type == APP_EV_EXIT) break;
        if (ev.type == APP_EV_CLICK && ni.mode == NET_AP) page ^= 1;
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
