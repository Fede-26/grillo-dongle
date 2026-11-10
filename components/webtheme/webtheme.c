#include "webtheme.h"

#include "websrv.h"

#define EMBED(name)                                                   \
    extern const uint8_t name##_start[] asm("_binary_" #name "_start"); \
    extern const uint8_t name##_end[] asm("_binary_" #name "_end")

EMBED(theme_css_gz);
EMBED(theme_js_gz);
EMBED(grillo_svg_gz);
EMBED(silkscreen_woff2);
EMBED(iosevka_woff2);

esp_err_t webtheme_register(void)
{
    static const struct {
        const char *uri, *type;
        const uint8_t *start, *end;
        unsigned flags;
    } files[] = {
        {"/theme.css", "text/css", theme_css_gz_start, theme_css_gz_end, WEBSRV_GZIP},
        {"/theme.js", "text/javascript", theme_js_gz_start, theme_js_gz_end, WEBSRV_GZIP},
        {"/grillo.svg", "image/svg+xml", grillo_svg_gz_start, grillo_svg_gz_end, WEBSRV_GZIP},
        // woff2 è già compresso: niente gzip, e cambia solo con un nuovo font
        {"/fonts/silkscreen.woff2", "font/woff2", silkscreen_woff2_start, silkscreen_woff2_end, WEBSRV_CACHE},
        {"/fonts/iosevka.woff2", "font/woff2", iosevka_woff2_start, iosevka_woff2_end, WEBSRV_CACHE},
    };
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        esp_err_t err = websrv_add_file(files[i].uri, files[i].type, files[i].start, files[i].end - files[i].start,
                                        files[i].flags);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
