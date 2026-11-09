// Netscan: reti WiFi visibili, controlli di rete e host della LAN.
//
// Sola lettura della rete: nessuna scansione di porte, nessun pacchetto che
// non sia ARP, ICMP, DNS o una GET a un solo indirizzo. I risultati di WiFi
// e LAN finiscono anche in /sd/scans/ in CSV.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "icons.h"
#include "lan.h"
#include "led.h"
#include "net.h"
#include "netscan.h"
#include "netscan_util.h"
#include "storage.h"
#include "ui.h"

static const char *TAG = "netscan";

#define ACCENT 0x38BDF8

#define SCAN_DIR      STORAGE_ROOT "/scans"
#define MAX_APS       40
// Il default è il controllo di connettività di Google (restituisce 204 senza
// corpo); la chiave netscan.probe_host lo sostituisce, per chi non vuole
// contattarlo.
#define PROBE_HOST    "connectivitycheck.gstatic.com"
#define PROBE_PATH    "/generate_204"
// Fuso orario POSIX per i nomi dei file CSV (CET/CEST); config.lua: timezone
#define DEFAULT_TZ    "CET-1CEST,M3.5.0,M10.5.0/3"
#define VALID_TIME    1700000000 // novembre 2023: prima di questa data l'orologio non è stato sincronizzato

// --- Orologio e file CSV ---

static void clock_start(void)
{
    setenv("TZ", config_str("timezone", DEFAULT_TZ), 1);
    tzset();
    if (esp_sntp_enabled()) return;
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, config_str("netscan.ntp", "pool.ntp.org"));
    esp_sntp_init();
}

static void clock_stop(void)
{
    if (esp_sntp_enabled()) esp_sntp_stop();
}

// Apre un nuovo CSV in /sd/scans. Il dongle non ha batteria per l'orologio: se
// l'ora non è nota il nome ha un numero progressivo invece della data.
static FILE *csv_open(const char *prefix, const char *tag, char *path, size_t n)
{
    if (storage_state() != STORAGE_MOUNTED) return NULL;
    mkdir(SCAN_DIR, 0775);
    char slug[20] = "", stamp[24];
    if (tag && *tag) {
        ns_file_slug(slug, sizeof(slug), tag);
        strlcat(slug, "-", sizeof(slug));
    }
    time_t now = time(NULL);
    if (now > VALID_TIME) {
        struct tm tm;
        localtime_r(&now, &tm);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm);
        snprintf(path, n, SCAN_DIR "/%s-%s%s.csv", prefix, slug, stamp);
    } else {
        struct stat st;
        for (int i = 1; i < 1000; i++) {
            snprintf(path, n, SCAN_DIR "/%s-%s%d.csv", prefix, slug, i);
            if (stat(path, &st) != 0) break;
        }
    }
    return fopen(path, "w");
}

static void csv_row(FILE *f, int cols, const char *const *v)
{
    char field[160];
    for (int i = 0; i < cols; i++) {
        ns_csv_field(field, sizeof(field), v[i]);
        fputs(field, f);
        fputc(i + 1 < cols ? ',' : '\n', f);
    }
}

static void saved_toast(FILE *f, const char *path)
{
    bool ok = f && fclose(f) == 0;
    ui_toast(ok ? "Saved to SD" : "SD write failed", ok ? UI_OK : UI_WARN, 1500);
    if (ok) ESP_LOGI(TAG, "salvato %s", path);
}

// --- Schermate di servizio ---

static void draw_busy(const char *line1, const char *line2)
{
    ui_begin();
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 18, UI_W, GFX_ALIGN_CENTER, line1, UI_TEXT);
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 30, UI_W, GFX_ALIGN_CENTER, line2, UI_DIM);
    ui_end();
}

// Lease STA con messaggio d'errore a schermo. false se non ottenuto.
static bool join_network(app_ctx_t *ctx, const char *title)
{
    draw_busy("Connecting…", "");
    esp_err_t err = net_acquire(NET_STA, 15000);
    if (err == ESP_OK) return true;
    if (!app_should_exit(ctx)) {
        app_message(ctx, title, err == ESP_ERR_NOT_FOUND ? "No known network: add it to wifi in config.lua" : "Connection failed", UI_ERR);
    }
    return false;
}

// Attende un gesto qualunque: true se l'app deve uscire
static bool wait_back(app_ctx_t *ctx)
{
    for (;;) {
        app_event_t ev;
        if (!app_next_event(ctx, &ev, portMAX_DELAY)) continue;
        if (ev.type == APP_EV_EXIT) return true;
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE || ev.type == APP_EV_LONG) return false;
    }
}

// --- Reti WiFi ---

static void wifi_detail(app_ctx_t *ctx, const net_ap_t *ap)
{
    char ssid[40], mac[18], l[48];
    ns_display_text(ssid, sizeof(ssid), ap->ssid);
    ns_mac_str(ap->bssid, mac);
    ui_begin();
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12, *ap->ssid ? ssid : "(hidden)", ACCENT);
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 14, mac, UI_TEXT);
    // Il vendor dell'AP aiuta a capire che apparato è (router di casa, mesh, hotspot)
    char vendor[29] = "";
    FILE *f = fopen(STORAGE_ROOT "/oui.bin", "rb");
    if (f) {
        if (!ns_oui_lookup(f, ap->bssid, vendor, sizeof(vendor))) vendor[0] = 0;
        fclose(f);
    }
    char v[32];
    ns_display_text(v, sizeof(v), vendor);
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 24, UI_W - 12, ns_mac_is_private(ap->bssid) ? "(private MAC)" : v, UI_DIM);
    snprintf(l, sizeof(l), "ch %d   %d dBm", ap->channel, ap->rssi);
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 36, l, UI_TEXT);
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 46, net_auth_name(ap->auth), ap->auth == 0 ? UI_WARN : UI_DIM);
    ui_draw_hint("click: back");
    ui_end();
    wait_back(ctx);
}

static void save_wifi(const net_ap_t *aps, int n)
{
    char path[80];
    FILE *f = csv_open("wifi", NULL, path, sizeof(path));
    if (!f) return;
    csv_row(f, 6, (const char *[]){"ssid", "bssid", "channel", "rssi", "security", "vendor"});
    FILE *oui = fopen(STORAGE_ROOT "/oui.bin", "rb");
    for (int i = 0; i < n; i++) {
        char mac[18], ch[8], rssi[8], vendor[29] = "";
        ns_mac_str(aps[i].bssid, mac);
        snprintf(ch, sizeof(ch), "%d", aps[i].channel);
        snprintf(rssi, sizeof(rssi), "%d", aps[i].rssi);
        if (oui && !ns_oui_lookup(oui, aps[i].bssid, vendor, sizeof(vendor))) vendor[0] = 0;
        csv_row(f, 6, (const char *[]){aps[i].ssid, mac, ch, rssi, net_auth_name(aps[i].auth), vendor});
    }
    if (oui) fclose(oui);
    saved_toast(f, path);
}

static void screen_wifi(app_ctx_t *ctx)
{
    if (net_acquire(NET_SCAN, 0) != ESP_OK) {
        app_message(ctx, "WiFi networks", "Radio start failed", UI_ERR);
        return;
    }
    net_ap_t *aps = calloc(MAX_APS, sizeof(*aps));
    // Righe del menu: Rescan, le reti, Back. Il testo vive finché il menu è aperto.
    char (*names)[34] = calloc(MAX_APS, sizeof(*names));
    char (*rights)[12] = calloc(MAX_APS, sizeof(*rights));
    const char **items = calloc(MAX_APS + 2, sizeof(*items));
    const char **rtext = calloc(MAX_APS + 2, sizeof(*rtext));
    if (!aps || !names || !rights || !items || !rtext) {
        app_message(ctx, "WiFi networks", "Out of memory", UI_ERR);
        goto out;
    }

    bool rescan = true;
    int sel = 1, n = 0;
    while (!app_should_exit(ctx)) {
        if (rescan) {
            draw_busy("Scanning…", "");
            n = net_scan(aps, MAX_APS);
            if (n < 0) {
                app_message(ctx, "WiFi networks", "Scan failed", UI_ERR);
                break;
            }
            save_wifi(aps, n);
            items[0] = "Rescan";
            for (int i = 0; i < n; i++) {
                if (aps[i].ssid[0]) ns_display_text(names[i], sizeof(names[i]), aps[i].ssid);
                else strlcpy(names[i], "(hidden)", sizeof(names[i]));
                snprintf(rights[i], sizeof(rights[i]), "ch%d %d", aps[i].channel, aps[i].rssi);
                items[i + 1] = names[i];
                rtext[i + 1] = rights[i];
            }
            items[n + 1] = "Back";
            rescan = false;
            sel = n ? 1 : 0;
        }
        char title[24];
        snprintf(title, sizeof(title), "WiFi (%d)", n);
        int c = app_menu_ex(ctx, title, items, rtext, n + 2, sel);
        if (c < 0 || c == n + 1) break;
        sel = c;
        if (c == 0) rescan = true;
        else wifi_detail(ctx, &aps[c - 1]);
    }
out:
    free(aps);
    free(names);
    free(rights);
    free(items);
    free(rtext);
    net_release();
}

// --- Network info ---

typedef struct {
    char ssid[33], ip[24], gw[16], dns[16];
    int rssi;
    // Esito di ogni controllo: -2 non ancora eseguito, -1 fallito, altrimenti ms
    int gw_ms, dns_ms, web_ms, web_code;
} info_t;

static void draw_info(const info_t *r)
{
    static const char *labels[] = {"WiFi", "IP", "GW", "DNS", "Web"};
    char val[5][40];
    uint32_t col[5];
    snprintf(val[0], sizeof(val[0]), "%s  %d dBm", r->ssid, r->rssi);
    snprintf(val[1], sizeof(val[1]), "%s", r->ip);
    col[0] = col[1] = UI_TEXT;
    // Un gateway che non risponde al ping è un avviso, non un errore: molti lo filtrano
    // Il gateway sta quasi sempre nella stessa rete dell'IP già mostrato: bastano gli ultimi ottetti
    const char *gw_tail = strrchr(r->gw, '.');
    snprintf(val[2], sizeof(val[2]), r->gw_ms == -2 ? "…" : (r->gw_ms < 0 ? "%s no reply" : "%s %dms"), gw_tail ? gw_tail : r->gw, r->gw_ms);
    col[2] = r->gw_ms == -2 ? UI_DIM : (r->gw_ms < 0 ? UI_WARN : UI_OK);
    snprintf(val[3], sizeof(val[3]), r->dns_ms == -2 ? "…" : (r->dns_ms < 0 ? "%s failed" : "%s %dms"), r->dns, r->dns_ms);
    col[3] = r->dns_ms == -2 ? UI_DIM : (r->dns_ms < 0 ? UI_ERR : UI_OK);
    if (r->web_ms == -2) {
        strcpy(val[4], "…");
        col[4] = UI_DIM;
    } else if (r->web_ms < 0) {
        strcpy(val[4], "No internet");
        col[4] = UI_ERR;
    } else if (r->web_code == 204) {
        snprintf(val[4], sizeof(val[4]), "Online  %d ms", r->web_ms);
        col[4] = UI_OK;
    } else if (r->web_code == 200 || (r->web_code >= 300 && r->web_code < 400)) {
        strcpy(val[4], "Captive portal");
        col[4] = UI_WARN;
    } else {
        snprintf(val[4], sizeof(val[4]), "HTTP %d", r->web_code);
        col[4] = UI_WARN;
    }
    ui_begin();
    for (int i = 0; i < 5; i++) {
        int y = UI_CONTENT_Y + i * 10;
        gfx_text(&gfx_font_small, 4, y, labels[i], UI_DIM);
        gfx_text_fit(&gfx_font_small, 34, y, UI_W - 38, val[i], col[i]);
    }
    ui_end();
}

static void screen_info(app_ctx_t *ctx)
{
    if (!join_network(ctx, "Network info")) return;
    clock_start();
    esp_netif_ip_info_t ipi;
    esp_netif_dns_info_t dns;
    esp_netif_t *en = net_sta_netif();
    esp_netif_get_ip_info(en, &ipi);
    esp_netif_get_dns_info(en, ESP_NETIF_DNS_MAIN, &dns);

    for (;;) {
        info_t r = {.gw_ms = -2, .dns_ms = -2, .web_ms = -2};
        net_info_t ni;
        net_get_info(&ni);
        strlcpy(r.ssid, ni.ssid, sizeof(r.ssid));
        r.rssi = ni.rssi;
        int prefix = __builtin_popcount(ipi.netmask.addr);
        snprintf(r.ip, sizeof(r.ip), IPSTR "/%d", IP2STR(&ipi.ip), prefix);
        snprintf(r.gw, sizeof(r.gw), IPSTR, IP2STR(&ipi.gw));
        snprintf(r.dns, sizeof(r.dns), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
        led_set(LED_LAYER_APP, LED_FX_PULSE, ACCENT, 800);

        draw_info(&r);
        r.gw_ms = probe_ping(ipi.gw.addr, 1000);
        draw_info(&r);
        const char *host = config_str("netscan.probe_host", PROBE_HOST);
        uint32_t web_ip = 0;
        r.dns_ms = probe_dns(host, &web_ip);
        draw_info(&r);
        r.web_ms = r.dns_ms < 0 ? -1 : probe_http(web_ip, host, PROBE_PATH, &r.web_code);
        draw_info(&r);

        bool all_ok = r.dns_ms >= 0 && r.web_ms >= 0 && r.web_code == 204;
        led_set(LED_LAYER_APP, LED_FX_SOLID, all_ok ? UI_OK : UI_WARN, 0);
        ui_toast(all_ok ? "All good" : "Check the rows", all_ok ? UI_OK : UI_WARN, 1500);

        bool again = false;
        while (!again) {
            app_event_t ev;
            if (!app_next_event(ctx, &ev, portMAX_DELAY)) continue;
            if (ev.type == APP_EV_EXIT || ev.type == APP_EV_DOUBLE) goto out;
            if (ev.type == APP_EV_CLICK || ev.type == APP_EV_LONG) again = true;
        }
    }
out:
    clock_stop();
    net_release();
}

// --- LAN hosts ---

typedef struct {
    app_ctx_t *ctx;
    int last_phase;
} sweep_ui_t;

static bool sweep_progress(int phase, int done, int total, int found, void *arg)
{
    sweep_ui_t *u = arg;
    char l[32];
    snprintf(l, sizeof(l), "%d host%s", found, found == 1 ? "" : "s");
    ui_begin();
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 10, UI_W, GFX_ALIGN_CENTER, phase == 0 ? "Scanning the LAN…" : "Looking up names…", UI_TEXT);
    ui_draw_progress(14, UI_CONTENT_Y + 26, UI_W - 28, 6, done, total, ACCENT);
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 38, UI_W, GFX_ALIGN_CENTER, l, UI_DIM);
    ui_end();
    return !app_should_exit(u->ctx);
}

static void ip_str(const uint8_t ip[4], char *out, size_t n)
{
    snprintf(out, n, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

static void host_detail(app_ctx_t *ctx, const lan_host_t *h)
{
    char ip[16], mac[18];
    ip_str(h->ip, ip, sizeof(ip));
    ns_mac_str(h->mac, mac);
    ui_begin();
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 2, ip, ACCENT);
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 14, mac, UI_TEXT);
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 24, UI_W - 12, h->vendor[0] ? h->vendor : "(unknown vendor)", UI_DIM);
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 36, UI_W - 12, h->name[0] ? h->name : "(no name)", UI_TEXT);
    if (h->flags & LAN_SELF) gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 46, "this dongle", UI_OK);
    else if (h->flags & LAN_GW) gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 46, "gateway", UI_WARN);
    ui_draw_hint("click: back");
    ui_end();
    wait_back(ctx);
}

static void save_lan(const lan_host_t *h, int n, const char *ssid)
{
    char path[96];
    FILE *f = csv_open("lan", ssid, path, sizeof(path));
    if (!f) return;
    csv_row(f, 5, (const char *[]){"ip", "mac", "vendor", "name", "role"});
    for (int i = 0; i < n; i++) {
        char ip[16], mac[18];
        ip_str(h[i].ip, ip, sizeof(ip));
        ns_mac_str(h[i].mac, mac);
        csv_row(f, 5, (const char *[]){ip, mac, h[i].vendor, h[i].name, (h[i].flags & LAN_SELF) ? "dongle" : ((h[i].flags & LAN_GW) ? "gateway" : "")});
    }
    saved_toast(f, path);
}

static void screen_lan(app_ctx_t *ctx)
{
    if (!join_network(ctx, "LAN hosts")) return;
    clock_start();
    net_info_t ni;
    net_get_info(&ni);

    lan_host_t *hosts = calloc(LAN_MAX_HOSTS + 1, sizeof(*hosts));
    char (*ips)[16] = calloc(LAN_MAX_HOSTS + 1, sizeof(*ips));
    char (*rights)[24] = calloc(LAN_MAX_HOSTS + 1, sizeof(*rights));
    const char **items = calloc(LAN_MAX_HOSTS + 3, sizeof(*items));
    const char **rtext = calloc(LAN_MAX_HOSTS + 3, sizeof(*rtext));
    if (!hosts || !ips || !rights || !items || !rtext) {
        app_message(ctx, "LAN hosts", "Out of memory", UI_ERR);
        goto out;
    }

    bool rescan = true;
    int sel = 1, n = 0;
    while (!app_should_exit(ctx)) {
        if (rescan) {
            led_set(LED_LAYER_APP, LED_FX_PULSE, ACCENT, 800);
            sweep_ui_t u = {.ctx = ctx};
            n = lan_sweep(hosts, LAN_MAX_HOSTS + 1, sweep_progress, &u);
            if (n < 0) {
                if (!app_should_exit(ctx)) app_message(ctx, "LAN hosts", "Scan failed", UI_ERR);
                break;
            }
            led_set(LED_LAYER_APP, LED_FX_SOLID, UI_OK, 0);
            save_lan(hosts, n, ni.ssid);
            items[0] = "Rescan";
            for (int i = 0; i < n; i++) {
                ip_str(hosts[i].ip, ips[i], sizeof(ips[i]));
                const char *r = hosts[i].name[0] ? hosts[i].name : hosts[i].vendor;
                if (!*r) r = (hosts[i].flags & LAN_SELF) ? "this dongle" : ((hosts[i].flags & LAN_GW) ? "gateway" : "");
                strlcpy(rights[i], r, sizeof(rights[i]));
                items[i + 1] = ips[i];
                rtext[i + 1] = rights[i];
            }
            items[n + 1] = "Back";
            rescan = false;
            sel = n > 1 ? 1 : 0;
        }
        char title[24];
        snprintf(title, sizeof(title), "LAN (%d)", n);
        int c = app_menu_ex(ctx, title, items, rtext, n + 2, sel);
        if (c < 0 || c == n + 1) break;
        sel = c;
        if (c == 0) rescan = true;
        else host_detail(ctx, &hosts[c - 1]);
    }
out:
    free(hosts);
    free(ips);
    free(rights);
    free(items);
    free(rtext);
    clock_stop();
    net_release();
}

// --- App ---

static void netscan_run(app_ctx_t *ctx)
{
    static const char *const items[] = {"WiFi networks", "Network info", "LAN hosts"};
    int sel = 0;
    while (!app_should_exit(ctx)) {
        led_set(LED_LAYER_APP, LED_FX_SOLID, gfx_mix(UI_BG, ACCENT, 80), 0);
        int c = app_menu(ctx, NULL, items, 3, sel);
        if (c < 0) return;
        sel = c;
        if (c == 0) screen_wifi(ctx);
        else if (c == 1) screen_info(ctx);
        else screen_lan(ctx);
    }
}

static void netscan_subtitle(char *buf, size_t len)
{
    int known = config_count("wifi");
    snprintf(buf, len, "%d known network%s", known, known == 1 ? "" : "s");
}

const app_desc_t netscan_app = {
    .id = "netscan",
    .name = "Netscan",
    .icon = &icon_radar,
    .accent = ACCENT,
    .requires = APP_REQ_NET,
    .usb = USB_PROFILE_NONE,
    .stack = 8192,
    .run = netscan_run,
    .subtitle = netscan_subtitle,
};
