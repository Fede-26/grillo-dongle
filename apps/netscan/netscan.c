// Netscan: visible WiFi networks, network checks and LAN hosts.
//
// Read-only on the network: no port scanning, no packet that is not
// ARP, ICMP, DNS, a GET to a single address or a DHCPDISCOVER (never
// followed by a REQUEST: no address is taken). The WiFi,
// LAN and DHCP results also go to /sd/scans/ as CSV.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "clock.h"
#include "config.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "icons.h"
#include "lan.h"
#include "led.h"
#include "net.h"
#include "netscan.h"
#include "netscan_util.h"
#include "storage.h"
#include "ui.h"

static const char *TAG = "netscan";

#define ACCENT 0x20D0D0

#define SCAN_DIR      STORAGE_ROOT "/scans"
#define MAX_APS       40
#define DHCP_MAX      8
#define DHCP_LISTEN   5000 // ms spent listening for OFFERs
// The default is Google's connectivity check (returns 204 with no
// body); the netscan.probe_host key replaces it, for those who do not want
// to contact it.
#define PROBE_HOST    "connectivitycheck.gstatic.com"
#define PROBE_PATH    "/generate_204"

// --- CSV files ---

// Opens a new CSV in /sd/scans. The dongle has no clock battery: if
// the time is unknown the name has a sequence number instead of the date.
static FILE *csv_open(const char *prefix, const char *tag, char *path, size_t n)
{
    if (storage_state() != STORAGE_MOUNTED) return NULL;
    mkdir(SCAN_DIR, 0775);
    char slug[20] = "", stamp[24];
    if (tag && *tag) {
        // One byte is left for the dash: with a full slug it was lost
        ns_file_slug(slug, sizeof(slug) - 1, tag);
        strlcat(slug, "-", sizeof(slug));
    }
    time_t now = time(NULL);
    if (clock_valid()) {
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
    if (ok) ESP_LOGI(TAG, "saved %s", path);
}

// --- Utility screens ---

static void draw_busy(const char *line1, const char *line2)
{
    ui_begin();
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 18, UI_W, GFX_ALIGN_CENTER, line1, UI_TEXT);
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 30, UI_W, GFX_ALIGN_CENTER, line2, UI_DIM);
    ui_end();
}

// STA lease with an on-screen error message. false if not obtained.
static bool join_network(app_ctx_t *ctx, const char *title)
{
    draw_busy("Connecting…", "");
    esp_err_t err = app_net_acquire(ctx, NET_STA, 15000);
    if (err == ESP_OK) return true;
    if (!app_should_exit(ctx)) {
        app_message(ctx, title, err == ESP_ERR_NOT_FOUND ? "No known network: add it to wifi in config.lua" : "Connection failed", UI_ERR);
    }
    return false;
}

// Waits for any gesture: true if the app must exit
static bool wait_back(app_ctx_t *ctx)
{
    for (;;) {
        app_event_t ev;
        if (!app_next_event(ctx, &ev, portMAX_DELAY)) continue;
        if (ev.type == APP_EV_EXIT) return true;
        if (ev.type == APP_EV_CLICK || ev.type == APP_EV_DOUBLE || ev.type == APP_EV_LONG) return false;
    }
}

// --- WiFi networks ---

static void wifi_detail(app_ctx_t *ctx, const net_ap_t *ap)
{
    char ssid[40], mac[18], l[48];
    ns_display_text(ssid, sizeof(ssid), ap->ssid);
    ns_mac_str(ap->bssid, mac);
    ui_begin();
    gfx_text_fit(&gfx_font_small, 6, UI_CONTENT_Y + 2, UI_W - 12, *ap->ssid ? ssid : "(hidden)", ACCENT);
    gfx_text(&gfx_font_small, 6, UI_CONTENT_Y + 14, mac, UI_TEXT);
    // The AP's vendor helps tell what device it is (home router, mesh, hotspot)
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
    // Menu rows: Rescan, the networks, Back. The text lives as long as the menu is open.
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
    // Result of each check: -2 not run yet, -1 failed, otherwise ms
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
    // A gateway that does not answer ping is a warning, not an error: many filter it
    // The gateway is almost always on the same network as the IP already shown: the last octets are enough
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
    net_release();
}

// --- DHCP servers ---

typedef struct {
    app_ctx_t *ctx;
} dhcp_ui_t;

static bool dhcp_progress(int elapsed, int found, void *arg)
{
    dhcp_ui_t *u = arg;
    char l[32];
    snprintf(l, sizeof(l), "%d server%s", found, found == 1 ? "" : "s");
    ui_begin();
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 10, UI_W, GFX_ALIGN_CENTER, "Asking DHCP servers…", UI_TEXT);
    ui_draw_progress(14, UI_CONTENT_Y + 26, UI_W - 28, 6, elapsed, DHCP_LISTEN, ACCENT);
    gfx_text_aligned(&gfx_font_small, 0, UI_CONTENT_Y + 38, UI_W, GFX_ALIGN_CENTER, l, UI_DIM);
    ui_end();
    return !app_should_exit(u->ctx);
}

// Human-readable lease time: "24h", "1h30m", "59m59s"
static void lease_str(uint32_t s, char *out, size_t n)
{
    unsigned h = s / 3600, m = s / 60 % 60, sec = s % 60;
    if (!s) snprintf(out, n, "?");
    else if (h) snprintf(out, n, m ? "%uh%um" : "%uh", h, m);
    else snprintf(out, n, sec ? "%um%us" : "%um", m, sec);
}

static void dhcp_detail(app_ctx_t *ctx, const lan_dhcp_server_t *d)
{
    static const uint8_t zero[6];
    const ns_dhcp_offer_t *o = &d->offer;
    char ip[16], mac[18], l[48], a[16], lease[12];
    ip_str(o->server, ip, sizeof(ip));
    ns_mac_str(d->mac, mac);
    bool has_mac = memcmp(d->mac, zero, 6) != 0;
    ui_begin();
    int y = UI_CONTENT_Y;
    gfx_text(&gfx_font_small, 6, y, ip, ACCENT);
    gfx_text(&gfx_font_small, 6, y + 9, has_mac ? mac : "(no ARP reply)", has_mac ? UI_TEXT : UI_DIM);
    gfx_text_fit(&gfx_font_small, 6, y + 18, UI_W - 12, d->vendor[0] ? d->vendor : "(unknown vendor)", UI_DIM);
    ip_str(o->offered, a, sizeof(a));
    lease_str(o->lease_s, lease, sizeof(lease));
    uint32_t mask;
    memcpy(&mask, o->mask, 4);
    snprintf(l, sizeof(l), "Offer %s/%d", a, __builtin_popcount(mask));
    gfx_text_fit(&gfx_font_small, 6, y + 27, UI_W - 12, l, UI_TEXT);
    ip_str(o->router, a, sizeof(a));
    snprintf(l, sizeof(l), "GW  %s", a);
    gfx_text_fit(&gfx_font_small, 6, y + 36, UI_W - 12, l, UI_TEXT);
    ip_str(o->dns, a, sizeof(a));
    snprintf(l, sizeof(l), "DNS %s", a);
    gfx_text_fit(&gfx_font_small, 6, y + 45, UI_W - 12, l, UI_TEXT);
    // No "click: back" hint: the row is needed for the lease time
    snprintf(l, sizeof(l), "Lease %s", lease);
    gfx_text_fit(&gfx_font_small, 6, y + 54, UI_W - 12, l, UI_TEXT);
    ui_end();
    wait_back(ctx);
}

static void save_dhcp(const lan_dhcp_server_t *d, int n, const char *ssid)
{
    char path[96];
    FILE *f = csv_open("dhcp", ssid, path, sizeof(path));
    if (!f) return;
    csv_row(f, 8, (const char *[]){"server", "mac", "vendor", "offered", "mask", "router", "dns", "lease_s"});
    for (int i = 0; i < n; i++) {
        const ns_dhcp_offer_t *o = &d[i].offer;
        char sv[16], mac[18], of[16], mk[16], gw[16], dns[16], lease[12];
        ip_str(o->server, sv, sizeof(sv));
        ns_mac_str(d[i].mac, mac);
        ip_str(o->offered, of, sizeof(of));
        ip_str(o->mask, mk, sizeof(mk));
        ip_str(o->router, gw, sizeof(gw));
        ip_str(o->dns, dns, sizeof(dns));
        snprintf(lease, sizeof(lease), "%lu", (unsigned long)o->lease_s);
        csv_row(f, 8, (const char *[]){sv, mac, d[i].vendor, of, mk, gw, dns, lease});
    }
    saved_toast(f, path);
}

static void screen_dhcp(app_ctx_t *ctx)
{
    if (!join_network(ctx, "DHCP servers")) return;
    net_info_t ni;
    net_get_info(&ni);

    lan_dhcp_server_t srv[DHCP_MAX];
    char ips[DHCP_MAX][16], rights[DHCP_MAX][10];
    const char *items[DHCP_MAX + 2], *rtext[DHCP_MAX + 2] = {0};
    bool rescan = true;
    int sel = 1, n = 0;
    while (!app_should_exit(ctx)) {
        if (rescan) {
            led_set(LED_LAYER_APP, LED_FX_PULSE, ACCENT, 800);
            dhcp_ui_t u = {.ctx = ctx};
            n = lan_dhcp_probe(srv, DHCP_MAX, DHCP_LISTEN, dhcp_progress, &u);
            if (n < 0) {
                if (!app_should_exit(ctx)) app_message(ctx, "DHCP servers", "Probe failed", UI_ERR);
                break;
            }
            save_dhcp(srv, n, ni.ssid);
            // One server is normal; none is a fault; more than one means
            // someone turned on another one (router plugged in the wrong
            // way round, PC with connection sharing, attack)
            uint32_t col = n == 1 ? UI_OK : UI_ERR;
            led_set(LED_LAYER_APP, LED_FX_SOLID, col, 0);
            if (n > 1) ui_toast("More than one DHCP server!", UI_ERR, 3000);
            else if (n == 0) ui_toast("No DHCP server answered", UI_ERR, 3000);
            items[0] = "Rescan";
            for (int i = 0; i < n; i++) {
                ip_str(srv[i].offer.server, ips[i], sizeof(ips[i]));
                // Short right-hand text: the server's IP is what matters and must not be truncated
                strlcpy(rights[i], ns_mac_is_private(srv[i].mac) ? "private" : srv[i].vendor, sizeof(rights[i]));
                items[i + 1] = ips[i];
                rtext[i + 1] = rights[i];
            }
            items[n + 1] = "Back";
            rescan = false;
            sel = n ? 1 : 0;
        }
        char title[24];
        snprintf(title, sizeof(title), "DHCP (%d)", n);
        int c = app_menu_ex(ctx, title, items, rtext, n + 2, sel);
        if (c < 0 || c == n + 1) break;
        sel = c;
        if (c == 0) rescan = true;
        else dhcp_detail(ctx, &srv[c - 1]);
    }
    net_release();
}

// --- App ---

static void netscan_run(app_ctx_t *ctx)
{
    static const char *const items[] = {"WiFi networks", "Network info", "LAN hosts", "DHCP servers"};
    int sel = 0;
    while (!app_should_exit(ctx)) {
        led_set(LED_LAYER_APP, LED_FX_SOLID, gfx_mix(UI_BG, ACCENT, 80), 0);
        int c = app_menu(ctx, NULL, items, 4, sel);
        if (c < 0) return;
        sel = c;
        if (c == 0) screen_wifi(ctx);
        else if (c == 1) screen_info(ctx);
        else if (c == 2) screen_lan(ctx);
        else screen_dhcp(ctx);
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
