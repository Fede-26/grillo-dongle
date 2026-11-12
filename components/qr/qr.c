#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "qr.h"
#include "qrcode.h"

#define QUIET 2 // white modules around it; the standard asks for 4, but 2 read fine from a phone on a dark background

typedef struct {
    int x, y, box;
    bool ok;
} qr_job_t;

static void draw_cb(esp_qrcode_handle_t qr, void *user)
{
    qr_job_t *j = user;
    int n = esp_qrcode_get_size(qr);
    int total = n + 2 * QUIET;
    int scale = j->box / total;
    if (scale < 1) return; // does not fit: ok stays false
    int side = total * scale;
    int ox = j->x + (j->box - side) / 2, oy = j->y + (j->box - side) / 2;
    gfx_fill_rect(ox, oy, side, side, 0xFFFFFF);
    for (int my = 0; my < n; my++) {
        for (int mx = 0; mx < n; mx++) {
            if (esp_qrcode_get_module(qr, mx, my)) {
                gfx_fill_rect(ox + (mx + QUIET) * scale, oy + (my + QUIET) * scale, scale, scale, 0x000000);
            }
        }
    }
    j->ok = true;
}

bool qr_draw(const char *text, int x, int y, int box)
{
    qr_job_t job = {x, y, box, false};
    esp_qrcode_config_t cfg = {
        .display_func_with_cb = draw_cb,
        // Version 3 = 29 modules: with the border and scale 2 it takes 66 px, the
        // full height of the content area. It holds 53 bytes at low ECC, i.e.
        // "WIFI:T:WPA;S:..;P:..;;" with an SSID and password of average length.
        .max_qrcode_version = 3,
        .qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
        .user_data = &job,
    };
    return esp_qrcode_generate(&cfg, text) == ESP_OK && job.ok;
}

// In the QR WIFI: format the characters \ ; , : " must be preceded by \,
// otherwise a password with ';' produced a QR that did not connect.
static void escape(char *out, size_t len, const char *in)
{
    size_t o = 0;
    for (; *in && o + 2 < len; in++) {
        if (strchr("\\;,:\"", *in)) out[o++] = '\\';
        out[o++] = *in;
    }
    out[o] = 0;
}

void qr_wifi_text(char *out, size_t len, const char *ssid, const char *pass)
{
    // SSID up to 32 bytes and password up to 64, doubled by escaping
    char s[70], p[130];
    escape(s, sizeof(s), ssid);
    escape(p, sizeof(p), pass);
    snprintf(out, len, "WIFI:T:WPA;S:%s;P:%s;;", s, p);
}
