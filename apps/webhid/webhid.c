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

#define ACCENT 0x4070FF
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

// IP del client senza la parte in comune con quello del dongle ("...166" o
// "...213.166"): quella parte è la rete, già visibile sopra. Non conosciamo la
// maschera, quindi si tolgono gli ottetti iniziali uguali (al massimo 3).
static void client_label(const char *client, const char *own, char *out, size_t len)
{
    const char *c = client, *o = own;
    int same = 0;
    while (same < 3) {
        const char *ce = strchr(c, '.'), *oe = strchr(o, '.');
        if (!ce || !oe || ce - c != oe - o || strncmp(c, o, ce - c) != 0) break;
        c = ce + 1;
        o = oe + 1;
        same++;
    }
    if (same) snprintf(out, len, "...%s", c);
    else strlcpy(out, client, len);
}

// Nel formato WIFI: dei QR i caratteri \ ; , : " vanno preceduti da \,
// altrimenti una password con ';' produceva un QR che non si collegava.
static void qr_escape(char *out, size_t len, const char *in)
{
    size_t o = 0;
    for (; *in && o + 2 < len; in++) {
        if (strchr("\\;,:\"", *in)) out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = 0;
}

// page 0: URL della pagina (STA e AP); page 1 (solo AP): credenziali WiFi
static void draw_main(const net_info_t *ni, const char *pin, int page, const char *client)
{
    char url[48];
    snprintf(url, sizeof(url), "http://%s/", ni->ip);
    char ssid[70], pass[130], wifi[220];
    qr_escape(ssid, sizeof(ssid), net_ap_ssid());
    qr_escape(pass, sizeof(pass), net_ap_pass());
    snprintf(wifi, sizeof(wifi), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);

    ui_begin();
    bool ok = qr_draw(page == 1 ? wifi : url, 0, UI_CONTENT_Y, QR_BOX);
    if (!ok) {
        gfx_rect(2, UI_CONTENT_Y + 2, QR_BOX - 4, QR_BOX - 4, UI_FAINT);
        gfx_text_aligned(&gfx_font_small, 2, UI_CONTENT_Y + 28, QR_BOX - 4, GFX_ALIGN_CENTER, "no QR", UI_DIM);
    }

    // Colonna stretta al massimo: un IP di 15 caratteri (192.168.100.100) è largo circa 89 px
    int x = QR_BOX + 2, w = UI_W - x - 1, y = UI_CONTENT_Y;
    if (page == 1) {
        gfx_text_fit(&gfx_font_small, x, y, w, "Join WiFi:", UI_DIM);
        gfx_text_fit(&gfx_font_small, x, y + 10, w, net_ap_ssid(), UI_TEXT);
        gfx_text_fit(&gfx_font_small, x, y + 20, w, net_ap_pass(), UI_DIM);
    } else {
        gfx_text_fit(&gfx_font_small, x, y, w, ni->mode == NET_AP ? net_ap_ssid() : ni->ssid, UI_DIM);
        if (gfx_text_width(&gfx_font_small, ni->ip) <= w) {
            gfx_text(&gfx_font_small, x, y + 10, ni->ip, UI_TEXT);
        } else {
            // Non entra in una riga: a capo dopo il secondo punto ("192.168." / "100.100")
            const char *dot = strchr(ni->ip, '.');
            dot = dot ? strchr(dot + 1, '.') : NULL;
            char first[16];
            size_t n = dot ? (size_t)(dot - ni->ip) + 1 : 0;
            memcpy(first, ni->ip, n);
            first[n] = 0;
            gfx_text(&gfx_font_small, x, y + 10, first, UI_TEXT);
            gfx_text(&gfx_font_small, x, y + 19, ni->ip + n, UI_TEXT);
        }
    }
    // Il PIN è la cosa che si legge a colpo d'occhio: font grande ingrandito 2x (6 cifre ≈ 84 px)
    const char *code = *pin ? pin : "none";
    int scale = gfx_text_scaled_width(&gfx_font_large, code, 2) <= w ? 2 : 1;
    gfx_text_scaled(&gfx_font_large, x, y + (scale == 2 ? 29 : 40), code, *pin ? ACCENT : UI_WARN, scale);
    // Niente ui_draw_hint: la barra dei suggerimenti è larga tutto lo schermo e taglierebbe il QR
    if (*client) {
        char label[20];
        client_label(client, ni->ip, label, sizeof(label));
        gfx_text_fit(&gfx_font_small, x, y + 58, w, label, UI_OK);
    }
    else gfx_text_fit(&gfx_font_small, x, y + 58, w, ni->mode == NET_AP ? "click: swap QR" : "hold 3s: exit", UI_DIM);
    ui_end();
}

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
    draw_connecting();
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
    // In hotspot si parte dalle credenziali WiFi: il QR dell'indirizzo non
    // serve finché il telefono non è sulla rete del dongle.
    int page = -1;
    char last_client[16] = "";
    for (;;) {
        net_info_t ni;
        net_get_info(&ni);
        const char *client = web_client_ip();
        if (strcmp(client, last_client) != 0) {
            strlcpy(last_client, client, sizeof(last_client));
            led_set(LED_LAYER_APP, *client ? LED_FX_SOLID : LED_FX_PULSE, *client ? UI_OK : ACCENT, 2000);
        }
        if (ni.up && page < 0) page = ni.mode == NET_AP ? 1 : 0;
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
