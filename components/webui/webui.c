// Screens shared by the apps driven by a web page (Web HID, Web Injector):
// network search, then QR code, address and PIN.

#include <stdio.h>
#include <string.h>
#include "config.h"
#include "esp_random.h"
#include "net.h"
#include "qr.h"
#include "ui.h"
#include "webui.h"

#define QR_BOX 66 // full height of the content area

void webui_make_pin(const char *pin_key, char *pin, size_t len)
{
    net_info_t ni;
    net_get_info(&ni);
    pin[0] = 0;
    if (config_bool(pin_key, true) && !ni.trusted) snprintf(pin, len, "%06u", (unsigned)(esp_random() % 1000000));
}

void webui_draw_connecting(const gfx_icon_t *icon, uint32_t accent)
{
    ui_begin();
    gfx_icon(icon, 8, UI_CONTENT_Y + 6, 3, accent);
    gfx_text(&gfx_font_large, 50, UI_CONTENT_Y + 6, "WiFi…", accent);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 28, "Looking for", UI_TEXT);
    gfx_text(&gfx_font_small, 50, UI_CONTENT_Y + 38, "a known network", UI_TEXT);
    ui_end();
}

// Client IP without the part it shares with the dongle's ("...166" or
// "...213.166"): that part is the network, already visible above. We do not
// know the netmask, so the equal leading octets are removed (at most 3).
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

// page 0: page URL (STA and AP); page 1 (AP only): WiFi credentials
void webui_draw_main(const net_info_t *ni, const char *pin, int page, const char *client, uint32_t accent)
{
    char url[48];
    snprintf(url, sizeof(url), "http://%s/", ni->ip);
    char wifi[220];
    qr_wifi_text(wifi, sizeof(wifi), net_ap_ssid(), net_ap_pass());

    ui_begin();
    bool ok = qr_draw(page == 1 ? wifi : url, 0, UI_CONTENT_Y, QR_BOX);
    if (!ok) {
        gfx_rect(2, UI_CONTENT_Y + 2, QR_BOX - 4, QR_BOX - 4, UI_FAINT);
        gfx_text_aligned(&gfx_font_small, 2, UI_CONTENT_Y + 28, QR_BOX - 4, GFX_ALIGN_CENTER, "no QR", UI_DIM);
    }

    // Column as narrow as possible: a 15-character IP (192.168.100.100) is about 89 px wide
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
            // Does not fit on one line: wrap after the second dot ("192.168." / "100.100")
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
    // The PIN is what gets read at a glance: large font scaled 2x (6 digits ≈ 84 px)
    // Without a PIN: "trusted" in green if the network decides it, "none" in
    // yellow if it is disabled by the app's config (applies on any network)
    const char *code = *pin ? pin : (ni->trusted ? "trusted" : "none");
    int scale = gfx_text_scaled_width(&gfx_font_large, code, 2) <= w ? 2 : 1;
    gfx_text_scaled(&gfx_font_large, x, y + (scale == 2 ? 29 : 40), code, *pin ? accent : (ni->trusted ? UI_OK : UI_WARN), scale);
    // No ui_draw_hint: the hint bar spans the whole screen and would cut the QR
    if (*client) {
        char label[20];
        client_label(client, ni->ip, label, sizeof(label));
        gfx_text_fit(&gfx_font_small, x, y + 58, w, label, UI_OK);
    }
    else gfx_text_fit(&gfx_font_small, x, y + 58, w, ni->mode == NET_AP ? "click: swap QR" : "hold 3s: exit", UI_DIM);
    ui_end();
}

void webui_qr_update(webui_qr_t *q, const net_info_t *ni)
{
    if (!ni->up) return;
    if (q->page < 0) q->page = ni->mode == NET_AP ? 1 : 0;
    else if (ni->mode == NET_AP && ni->clients > q->clients) q->page = 0;
    q->clients = ni->clients;
}

void webui_qr_toggle(webui_qr_t *q, const net_info_t *ni)
{
    if (ni->mode == NET_AP && q->page >= 0) q->page ^= 1;
}
