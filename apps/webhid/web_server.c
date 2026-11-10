#include "web_server.h"

#include <stdio.h>
#include <string.h>
#include "hid.h"
#include "led.h"
#include "websrv.h"

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

#define MAX_FRAME      2048 // la pagina spezza gli incolla in blocchi più piccoli
#define MAX_ERASE      500
#define ACCENT         0x4070FF

// Lunghezza in byte del carattere UTF-8 in `s`, 0 se la sequenza non è
// valida o è troncata (fine del frame): così non si legge oltre il buffer.
static int utf8_len(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    int n = p[0] < 0x80 ? 1 : (p[0] & 0xE0) == 0xC0 ? 2 : (p[0] & 0xF0) == 0xE0 ? 3 : (p[0] & 0xF8) == 0xF0 ? 4 : 0;
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return 0;
    }
    return n;
}

static uint32_t utf8_first(const char *s)
{
    const uint8_t *p = (const uint8_t *)s;
    switch (utf8_len(s)) {
    case 1: return p[0];
    case 2: return ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
    case 3: return ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    case 4: return ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
    default: return 0xFFFD;
    }
}

// Digita il testo un carattere alla volta. Un carattere che il layout non ha
// diventa '?': prima la digitazione si fermava lì e perdeva il resto del
// blocco, e nel live typing la pagina contava un carattere che sul PC non
// c'era (i backspace successivi ne cancellavano uno di troppo).
static hid_result_t type_text(const char *s, int *skipped)
{
    char one[5];
    while (*s) {
        int n = utf8_len(s);
        if (n == 0) n = 1; // byte non valido: lo si tratta come carattere a sé
        memcpy(one, s, n);
        one[n] = 0;
        s += n;
        hid_result_t r = hid_type(one, NULL);
        if (r == HID_UNTYPEABLE) {
            (*skipped)++;
            r = hid_type("?", NULL);
            if (r == HID_UNTYPEABLE) r = HID_OK;
        }
        if (r != HID_OK) return r;
    }
    return HID_OK;
}

static bool keep_typing(void *arg)
{
    return !websrv_stopping();
}

static int clamp(int v, int lim)
{
    return v < -lim ? -lim : (v > lim ? lim : v);
}

// Comandi (testo, un carattere di tipo + argomenti):
//   m dx dy     muove il mouse        b mask      stato dei pulsanti
//   w dy dx     rotella (dx facoltativo)
//   t testo     digita il testo nel layout dell'host
//   c mods ch   combinazione: modificatori + carattere (es. Ctrl+C)
//   k mods key  combinazione con usage HID grezzo (tasti speciali, F-key)
//   e n         n backspace di seguito, 1..MAX_ERASE (il live typing cancella così)
//   l nome      layout dell'host (it, us, colemak)
//   o nome      OS dell'host (windows, linux, mac): sceglie i ripieghi per i caratteri mancanti
static hid_result_t command(const char *m, int *skipped)
{
    int a = 0, b = 0;
    switch (m[0]) {
    case 'm':
        // Un dx enorme teneva occupato il task httpd per ore di report
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) return hid_mouse_move(clamp(a, 2000), clamp(b, 2000));
        break;
    case 'b':
        if (sscanf(m + 1, "%d", &a) == 1) return hid_mouse_buttons(a);
        break;
    case 'w':
        sscanf(m + 1, "%d %d", &a, &b);
        return hid_mouse_scroll(clamp(a, 127), clamp(b, 127));
    case 't': return type_text(m + 1, skipped);
    case 'c':
        if (sscanf(m + 1, "%d", &a) == 1) {
            const char *ch = strchr(m + 1, ' ');
            uint8_t key, mods;
            if (ch && ch[1] && hid_char_key(utf8_first(ch + 1), &key, &mods)) return hid_chord(a | mods, &key, 1);
        }
        break;
    case 'e':
        // La pagina spezza le cancellazioni lunghe in blocchi da MAX_ERASE
        if (sscanf(m + 1, "%d", &a) == 1 && a > 0 && a <= MAX_ERASE) {
            uint8_t bs = 0x2A; // Backspace
            for (int i = 0; i < a; i++) {
                hid_result_t r = hid_chord(0, &bs, 1);
                if (r != HID_OK) return r;
            }
        }
        break;
    case 'l': {
        const hid_layout_t *l = hid_layout_find(m + 1);
        if (l) hid_set_layout(l);
        break;
    }
    case 'o': {
        hid_os_t os;
        if (hid_os_parse(m + 1, &os)) hid_set_os(os);
        break;
    }
    case 'k':
        if (sscanf(m + 1, "%d %d", &a, &b) == 2) {
            uint8_t key = b;
            return hid_chord(a, &key, b ? 1 : 0);
        }
        break;
    default: break;
    }
    return HID_OK;
}

// La sessione finisce (pagina chiusa, telefono che si riconnette, uscita
// dall'app): nessun tasto o pulsante del mouse deve restare premuto sul PC.
static void release_all(void)
{
    hid_release_all();
    hid_mouse_buttons(0);
}

// Layout e OS correnti, per allineare i selettori della pagina
static void on_auth(httpd_req_t *req)
{
    static const char *const os_names[] = {"windows", "linux", "mac"};
    char st[48];
    snprintf(st, sizeof(st), "L%s %s", hid_get_layout()->name, os_names[hid_get_os()]);
    websrv_reply(req, st);
}

static void on_message(httpd_req_t *req, char *msg, size_t len)
{
    led_flash(ACCENT, 15);
    int skipped = 0;
    hid_result_t r = command(msg, &skipped);
    // "hostok" solo al ritorno del PC, non a ogni comando: toglie
    // l'avviso "PC not ready" dalla pagina, che prima restava per sempre.
    static bool host_bad;
    if (r == HID_NO_HOST) websrv_reply(req, "nohost");
    else if (r == HID_OK && host_bad) websrv_reply(req, "hostok");
    host_bad = r == HID_NO_HOST;
    if (skipped) {
        char m[24];
        snprintf(m, sizeof(m), "untypeable %d", skipped);
        websrv_reply(req, m);
    }
}

esp_err_t web_start(const char *pin)
{
    hid_set_poll(keep_typing, NULL);
    websrv_config_t cfg = {
        .pin = pin,
        .stack_size = 6144, // hid_type usa array di tasti sullo stack
        .max_frame = MAX_FRAME,
        .on_message = on_message,
        .on_auth = on_auth,
        .on_session_end = release_all,
    };
    esp_err_t err = websrv_start(&cfg);
    if (err == ESP_OK) {
        err = websrv_add_page("/", "text/html", index_html_gz_start, index_html_gz_end - index_html_gz_start);
    }
    if (err != ESP_OK) web_stop();
    return err;
}

void web_stop(void)
{
    websrv_stop();
    hid_set_poll(NULL, NULL);
}
