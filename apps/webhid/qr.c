#include <stdbool.h>
#include "gfx.h"
#include "qr.h"
#include "qrcode.h"

#define QUIET 2 // moduli bianchi attorno; la norma ne chiede 4, ma 2 si legge bene da un telefono su fondo scuro

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
    if (scale < 1) return; // non entra: ok resta false
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
        // Versione 3 = 29 moduli: con il bordo e scala 2 occupa 66 px, tutta
        // l'altezza dell'area contenuto. Regge 53 byte a ECC bassa, cioè
        // "WIFI:T:WPA;S:..;P:..;;" con SSID e password di media lunghezza.
        .max_qrcode_version = 3,
        .qrcode_ecc_level = ESP_QRCODE_ECC_LOW,
        .user_data = &job,
    };
    return esp_qrcode_generate(&cfg, text) == ESP_OK && job.ok;
}
