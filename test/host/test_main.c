// Tests of the pure logic (keyboard layouts, button gestures) on the PC.
// Usage: make -C test/host

#include <stdio.h>
#include <string.h>
#include "gesture.h"
#include "hid_layout.h"
#include "netscan_util.h"
#include "secrets.h"
#include <stdlib.h>

static int s_fail;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);   \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
            s_fail++;                                     \
        }                                                 \
    } while (0)

static void expect_key(const char *layout, hid_os_t os, uint32_t cp, uint8_t key, uint8_t mods)
{
    hid_stroke_t s[8];
    bool np;
    int n = hid_translate(hid_layout_find(layout), os, cp, s, 8, &np);
    CHECK(n == 1 && s[0].key == key && s[0].mods == mods, "%s U+%04X: n=%d key=0x%02X mods=0x%02X", layout,
          (unsigned)cp, n, n ? s[0].key : 0, n ? s[0].mods : 0);
}

static void test_layouts(void)
{
    // US
    expect_key("us", HID_OS_WINDOWS, 'a', 0x04, 0);
    expect_key("us", HID_OS_WINDOWS, 'A', 0x04, HID_MOD_LSHIFT);
    expect_key("us", HID_OS_WINDOWS, '@', 0x1F, HID_MOD_LSHIFT);
    expect_key("us", HID_OS_WINDOWS, '\\', 0x31, 0);
    expect_key("us", HID_OS_WINDOWS, '\n', 0x28, 0);
    // IT
    expect_key("it", HID_OS_WINDOWS, '@', 0x33, HID_MOD_RALT);
    expect_key("it", HID_OS_WINDOWS, '#', 0x34, HID_MOD_RALT);
    expect_key("it", HID_OS_WINDOWS, '"', 0x1F, HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, '/', 0x24, HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, '-', 0x38, 0);
    expect_key("it", HID_OS_WINDOWS, '<', 0x64, 0);
    expect_key("it", HID_OS_WINDOWS, '{', 0x2F, HID_MOD_RALT | HID_MOD_LSHIFT);
    expect_key("it", HID_OS_WINDOWS, 0x20AC, 0x08, HID_MOD_RALT); // €
    expect_key("it", HID_OS_WINDOWS, 0x00E8, 0x2F, 0);           // è
    expect_key("it", HID_OS_LINUX, '~', 0x2E, HID_MOD_RALT);
    // Colemak
    expect_key("colemak", HID_OS_WINDOWS, 'f', 0x08, 0);
    expect_key("colemak", HID_OS_WINDOWS, 'o', 0x33, 0);
    expect_key("colemak", HID_OS_WINDOWS, ';', 0x13, 0);
    // Colemak on Linux (xkb us(colemak)): AltGr+a = á, AltGr+e = é, € = AltGr+5
    expect_key("colemak", HID_OS_LINUX, 0x00E1, 0x04, HID_MOD_RALT);
    expect_key("colemak", HID_OS_LINUX, 0x00E9, 0x0E, HID_MOD_RALT); // the Colemak 'e' is on the US physical key 'k'
    expect_key("colemak", HID_OS_LINUX, 0x20AC, 0x22, HID_MOD_RALT);

    // Windows fallback: ~ on IT = Alt + 0126 on the numeric keypad
    hid_stroke_t s[8];
    bool np;
    int n = hid_translate(hid_layout_find("it"), HID_OS_WINDOWS, '~', s, 8, &np);
    CHECK(n == 4 && np && s[0].key == 0x62 && s[1].key == 0x59 && s[2].key == 0x5A && s[3].key == 0x5E,
          "it/windows ~ fallback");
    // Linux fallback: ñ (U+00F1) = Ctrl+Shift+U, f, 1, space
    n = hid_translate(hid_layout_find("it"), HID_OS_LINUX, 0xF1, s, 8, &np);
    CHECK(n == 4 && s[0].key == 0x18 && s[3].key == 0x2C, "it/linux ñ fallback n=%d", n);
    // Colemak on Linux: à = AltGr+r (grave dead key, physical position 's' = 0x16), then a (0x04)
    // Ctrl+Shift+U fallback: the Colemak 'u' is on the US I key (0x0C)
    n = hid_translate(hid_layout_find("colemak"), HID_OS_LINUX, 0x0416, s, 8, &np);
    CHECK(n >= 3 && s[0].key == 0x0C && s[0].mods == (HID_MOD_LCTRL | HID_MOD_LSHIFT), "colemak/linux Ж: u=0x%02X",
          s[0].key);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_LINUX, 0x00E0, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x16 && s[0].mods == HID_MOD_RALT && s[1].key == 0x04 && s[1].mods == 0,
          "colemak/linux à n=%d", n);
    // È: same sequence, with Shift on the 'e' (physical key 0x0E)
    n = hid_translate(hid_layout_find("colemak"), HID_OS_LINUX, 0x00C8, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x16 && s[1].key == 0x0E && (s[1].mods & HID_MOD_LSHIFT), "colemak/linux È n=%d", n);
    // Colemak on Windows (official KLC): AltGr+a = á, à = AltGr+r then a. No numpad fallback
    expect_key("colemak", HID_OS_WINDOWS, 0x00E1, 0x04, HID_MOD_RALT);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_WINDOWS, 0x00E0, s, 8, &np);
    CHECK(n == 2 && !np && s[0].key == 0x16 && s[0].mods == HID_MOD_RALT && s[1].key == 0x04,
          "colemak/windows à n=%d", n);
    // A character that Windows Colemak lacks (Ω) stays on the Alt+keypad fallback if representable: here it is not
    n = hid_translate(hid_layout_find("colemak"), HID_OS_WINDOWS, 0x03A9, s, 8, &np);
    CHECK(n == 0, "colemak/windows Ω not typeable n=%d", n);
    // Colemak on macOS (from UCKeyTranslate): Option+a = å, Option+` then a = à, Option+e then e = é (the Colemak e is on physical key 0x0E, also as a dead key)
    expect_key("colemak", HID_OS_MAC, 0x00E5, 0x04, HID_MOD_LALT);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_MAC, 0x00E0, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x35 && s[0].mods == HID_MOD_LALT && s[1].key == 0x04,
          "colemak/mac à n=%d key0=0x%02X mods0=0x%02X", n, n ? s[0].key : 0, n ? s[0].mods : 0);
    n = hid_translate(hid_layout_find("colemak"), HID_OS_MAC, 0x00E9, s, 8, &np);
    CHECK(n == 2 && s[0].key == 0x0E && s[0].mods == HID_MOD_LALT && s[1].key == 0x0E, "colemak/mac é n=%d", n);
    // macOS "Italian - Pro" (from UCKeyTranslate): ~ = Option+5, ` = Option+\ (ISO key left of the 1),
    // @ and € as on Windows (Option acts as AltGr), à direct, À = Option+Shift+w
    expect_key("it", HID_OS_MAC, '~', 0x22, HID_MOD_LALT);
    expect_key("it", HID_OS_MAC, '`', 0x35, HID_MOD_LALT);
    expect_key("it", HID_OS_MAC, '\\', 0x35, 0);
    expect_key("it", HID_OS_MAC, '<', 0x64, 0);
    expect_key("it", HID_OS_MAC, '@', 0x33, HID_MOD_RALT);
    expect_key("it", HID_OS_MAC, 0x00E0, 0x34, 0);
    expect_key("it", HID_OS_MAC, 0x00C0, 0x1A, HID_MOD_LALT | HID_MOD_LSHIFT); // À = Option+Shift+w
    // macOS has no fallbacks for what the layout does not produce
    n = hid_translate(hid_layout_find("it"), HID_OS_MAC, 0x4E2D, s, 8, &np);
    CHECK(n == 0, "mac without fallback");

    // Every printable ASCII character is typeable on US
    for (int c = 0x20; c < 0x7F; c++) {
        n = hid_translate(hid_layout_find("us"), HID_OS_MAC, c, s, 8, &np);
        CHECK(n == 1, "us missing '%c'", c);
    }
    uint8_t key, mods;
    CHECK(hid_key_by_name("F5", &key, &mods) && key == 0x3E, "F5");
    CHECK(hid_key_by_name("ctrl", &key, &mods) && key == 0 && mods == HID_MOD_LCTRL, "ctrl");
}


static void test_netscan(void)
{
    // oui.bin: header + 32-byte records sorted by OUI
    FILE *f = tmpfile();
    uint8_t rec[32];
    fwrite("OUI1\2\0\0\0", 1, 8, f);
    memset(rec, 0, 32);
    memcpy(rec, "\x00\x1B\x63", 3);
    strcpy((char *)rec + 3, "Apple");
    fwrite(rec, 1, 32, f);
    memset(rec, 0, 32);
    memcpy(rec, "\xF4\x12\xFA", 3);
    strcpy((char *)rec + 3, "Espressif");
    fwrite(rec, 1, 32, f);
    char v[32];
    const uint8_t esp[6] = {0xF4, 0x12, 0xFA, 1, 2, 3}, none[6] = {0x00, 0x1C, 0x00, 0, 0, 0};
    CHECK(ns_oui_lookup(f, esp, v, sizeof(v)) && !strcmp(v, "Espressif"), "oui esp");
    CHECK(ns_oui_lookup(f, (const uint8_t[6]){0x00, 0x1B, 0x63}, v, 4) && !strcmp(v, "App"), "oui truncated");
    CHECK(!ns_oui_lookup(f, none, v, sizeof(v)), "oui absent");
    fclose(f);
    CHECK(ns_mac_is_private((const uint8_t[6]){0x02, 0, 0, 0, 0, 0}) && !ns_mac_is_private(esp), "private mac");

    // NBNS: reply with two names, the first a group (domain), the second is the machine
    uint8_t q[50];
    CHECK(ns_nbns_query(q) == 50 && q[13] == 'C' && q[14] == 'K', "nbns query");
    uint8_t r[128] = {0x47, 0x52, 0x84, 0, 0, 0, 0, 1};
    int p = 12;
    r[p++] = 0x20;
    memset(r + p, 'A', 32);
    p += 32;
    r[p++] = 0;
    p += 10; // type, class, ttl, rdlen (not checked)
    r[p++] = 2;
    memcpy(r + p, "WORKGROUP      ", 15), r[p + 15] = 0x00, r[p + 16] = 0x80, p += 18;
    memcpy(r + p, "PC-TEST1       ", 15), r[p + 15] = 0x00, r[p + 16] = 0x04, p += 18;
    char name[32];
    CHECK(ns_nbns_parse(r, p, name, sizeof(name)) && !strcmp(name, "PC-TEST1"), "nbns parse '%s'", name);
    CHECK(!ns_nbns_parse(r, 20, name, sizeof(name)), "nbns truncated");

    // mDNS: the query for 192.168.1.7 and a reply with compression
    uint8_t m[96];
    int ml = ns_mdns_ptr_query(m, sizeof(m), (const uint8_t[4]){192, 168, 1, 7});
    CHECK(ml == 42 && m[12] == 1 && m[13] == '7', "mdns query len %d", ml);
    uint8_t a[128] = {0x47, 0x52, 0x84, 0, 0, 1, 0, 1};
    memcpy(a + 12, m + 12, ml - 12);
    int ap = ml;
    a[ap++] = 0xC0, a[ap++] = 12; // name = pointer to the question
    a[ap++] = 0, a[ap++] = 12, a[ap++] = 0, a[ap++] = 1;
    a[ap++] = 0, a[ap++] = 0, a[ap++] = 0, a[ap++] = 120;
    const char host[] = "\x0Bmacbook-pro\5local";
    a[ap++] = 0, a[ap++] = sizeof(host);
    memcpy(a + ap, host, sizeof(host));
    ap += sizeof(host);
    CHECK(ns_mdns_ptr_parse(a, ap, name, sizeof(name)) && !strcmp(name, "macbook-pro"), "mdns parse '%s'", name);
    // Compression pointer that points to itself: must fail, not loop
    uint8_t loop[32] = {0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0xC0, 12};
    CHECK(!ns_mdns_ptr_parse(loop, 14, name, sizeof(name)), "mdns loop");

    // Text
    char t[40];
    ns_display_text(t, sizeof(t), "Caff\xC3\xA8 \x01WiFi");
    CHECK(!strcmp(t, "Caff? ?WiFi"), "display '%s'", t);
    ns_csv_field(t, sizeof(t), "Casa, \"mia\"");
    CHECK(!strcmp(t, "\"Casa, \"\"mia\"\"\""), "csv quote '%s'", t);
    ns_csv_field(t, sizeof(t), "=HYPERLINK(1)");
    CHECK(!strcmp(t, "'=HYPERLINK(1)"), "csv formula '%s'", t);
    // Control characters before the formula: they are removed, the apostrophe must stay
    ns_csv_field(t, sizeof(t), "\t=HYPERLINK(1)");
    CHECK(!strcmp(t, "'=HYPERLINK(1)"), "csv formula after tab '%s'", t);
    ns_csv_field(t, sizeof(t), "-77");
    CHECK(!strcmp(t, "-77"), "csv number '%s'", t);
    ns_csv_field(t, sizeof(t), "-1+cmd");
    CHECK(!strcmp(t, "'-1+cmd"), "csv formula with digits '%s'", t);
    ns_csv_field(t, sizeof(t), "a\nb");
    CHECK(!strcmp(t, "ab"), "csv newline");
    ns_csv_field(t, 6, "abcdefghij");
    CHECK(strlen(t) < 6, "csv truncated");
    ns_file_slug(t, 6, "Casa Mia!");
    CHECK(!strcmp(t, "Casa_"), "slug '%s'", t);

    // Sweep: the /22 is limited to the /24; the /26 stays as it is; /31 and /32 nothing
    uint32_t lo, hi;
    CHECK(ns_sweep_range(0xC0A80A17, 0xFFFFFC00, &lo, &hi) && lo == 0xC0A80A01 && hi == 0xC0A80AFE, "sweep /22");
    CHECK(ns_sweep_range(0xC0A80A45, 0xFFFFFFC0, &lo, &hi) && lo == 0xC0A80A41 && hi == 0xC0A80A7E, "sweep /26");
    CHECK(!ns_sweep_range(0xC0A80A01, 0xFFFFFFFE, &lo, &hi), "sweep /31");
}

static gesture_t run(gesture_ctx_t *g, bool pressed, uint32_t *t, uint32_t ms)
{
    gesture_t last = GESTURE_NONE;
    for (uint32_t i = 0; i < ms; i += 10) {
        *t += 10;
        gesture_t e = gesture_feed(g, pressed, *t, NULL);
        if (e != GESTURE_NONE && e != GESTURE_HOLD_PROGRESS && e != GESTURE_LONG_ARMED) last = e;
    }
    return last;
}

// One's complement sum over the whole `p`: 0 if the contained checksum is correct
static uint16_t fold(uint32_t s)
{
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return s;
}

static void test_dhcp(void)
{
    const uint8_t mac[6] = {0x24, 0x0a, 0xc4, 1, 2, 3}, src[4] = {192, 168, 1, 50};
    uint8_t b[400];
    int n = ns_dhcp_discover(b, sizeof(b), mac, 0x11223344, src);
    CHECK(n == 308, "discover len %d", n);
    CHECK(b[1] == 68 && b[3] == 67 && (b[4] << 8 | b[5]) == n, "udp header");
    CHECK(b[8] == 1 && b[8 + 4] == 0x11 && b[8 + 7] == 0x44 && b[8 + 10] == 0x80, "bootp fields");
    CHECK(!memcmp(b + 8 + 28, mac, 6) && b[8 + 236] == 0x63 && b[8 + 242] == 1, "chaddr, cookie, type");
    // Checksum verification: pseudo-header + segment must sum to 0xFFFF
    uint32_t s = (192 << 8 | 168) + (1 << 8 | 50) + 0xFFFF + 0xFFFF + 17 + n;
    for (int i = 0; i < n; i += 2) s += b[i] << 8 | b[i + 1];
    CHECK(fold(s) == 0xFFFF, "udp checksum");
    CHECK(ns_dhcp_discover(b, 100, mac, 1, src) == 0, "short buffer");

    // Hand-built OFFER: IP 20 bytes + UDP 8 + BOOTP 236 + options
    uint8_t p[340] = {0x45};
    p[9] = 17;
    memcpy(p + 12, (uint8_t[]){192, 168, 1, 254}, 4);
    uint8_t *u = p + 20, *d = u + 8;
    u[1] = 67;
    u[3] = 68;
    d[0] = 2;
    memcpy(d + 4, (uint8_t[]){0x11, 0x22, 0x33, 0x44}, 4);
    memcpy(d + 16, (uint8_t[]){192, 168, 1, 77}, 4);
    const uint8_t opt[] = {0x63, 0x82, 0x53, 0x63, 53, 1, 2, 54, 4, 192, 168, 1, 1, 1, 4, 255, 255, 255, 0,
                           3, 8, 192, 168, 1, 1, 10, 0, 0, 1, 0, 6, 4, 8, 8, 8, 8, 51, 4, 0, 1, 0x51, 0x80, 255};
    memcpy(d + 236, opt, sizeof(opt));
    int len = 20 + 8 + 236 + sizeof(opt);
    ns_dhcp_offer_t o;
    CHECK(ns_dhcp_parse_offer(p, len, 0x11223344, &o), "offer");
    CHECK(!memcmp(o.server, (uint8_t[]){192, 168, 1, 1}, 4), "server id wins over the source IP");
    CHECK(o.offered[3] == 77 && o.mask[2] == 255 && o.router[3] == 1 && o.dns[0] == 8 && o.lease_s == 86400,
          "offer fields");
    CHECK(!ns_dhcp_parse_offer(p, len, 0x11223345, &o), "different xid");
    d[236 + 6] = 5; // DHCPACK
    CHECK(!ns_dhcp_parse_offer(p, len, 0x11223344, &o), "ack is not offer");
    d[236 + 6] = 2;
    d[236 + 8] = 200; // option 54 length past the end
    CHECK(!ns_dhcp_parse_offer(p, len, 0x11223344, &o), "truncated option");
    d[236 + 8] = 4;
    d[236 + 7] = 250; // option 54 replaced by an unknown one: server = source IP
    CHECK(ns_dhcp_parse_offer(p, len, 0x11223344, &o) && o.server[3] == 254, "without server id");
    CHECK(!ns_dhcp_parse_offer(p, 30, 0x11223344, &o), "short packet");
}

static void test_gestures(void)
{
    gesture_ctx_t g;
    uint32_t t = 0;
    gesture_init(&g);
    run(&g, true, &t, 100);
    CHECK(run(&g, false, &t, 400) == GESTURE_CLICK, "click");
    run(&g, true, &t, 100);
    run(&g, false, &t, 100);
    run(&g, true, &t, 100);
    CHECK(run(&g, false, &t, 400) == GESTURE_DOUBLE, "double");
    run(&g, true, &t, 800);
    CHECK(run(&g, false, &t, 400) == GESTURE_LONG, "long");
    run(&g, true, &t, 1400);
    CHECK(run(&g, false, &t, 400) == GESTURE_LONG, "long at 1.4 s");
    run(&g, true, &t, 2000);
    CHECK(run(&g, false, &t, 400) == GESTURE_HOLD_CANCEL, "hold cancel");
    CHECK(run(&g, true, &t, 3100) == GESTURE_HOLD_EXIT, "hold exit");
    CHECK(run(&g, false, &t, 400) == GESTURE_NONE, "release after exit");
}

static void expect_mask(const char *src, const char *want)
{
    char *m = secrets_mask(src);
    CHECK(m && strcmp(m, want) == 0, "mask\n  src:  %s\n  got:  %s\n  want: %s", src, m ? m : "(null)", want);
    // Saving the masked text without touching it returns the original
    bool unknown;
    char *u = m ? secrets_unmask(m, src, &unknown) : NULL;
    CHECK(u && strcmp(u, src) == 0, "unmask round trip\n  got:  %s\n  want: %s", u ? u : "(null)", src);
    free(m);
    free(u);
}

static void test_secrets(void)
{
    expect_mask("wifi = { { ssid = \"Casa\", pass = \"segreta\" } }",
                "wifi = { { ssid = \"Casa\", pass = \"<secret:1>\" } }");
    expect_mask("hotspot = { pass = 'x\\'y' }, password=[[lunga]]",
                "hotspot = { pass = \"<secret:1>\" }, password=\"<secret:2>\"");
    expect_mask("secrets = { admin = \"a}b\", sudo = [==[s]]x]==] }, ui = { name = \"vis\" }",
                "secrets = { admin = \"<secret:1>\", sudo = \"<secret:2>\" }, ui = { name = \"vis\" }");
    // Tables inside secrets: these are hidden too; after the closing brace not
    expect_mask("secrets = { t = { \"a\" } }, x = \"b\"", "secrets = { t = { \"<secret:1>\" } }, x = \"b\"");
    // Comments and comparisons do not count
    expect_mask("-- pass = \"no\"\n--[[ secrets = { \"no\" } ]]\nok = pass == \"no\"",
                "-- pass = \"no\"\n--[[ secrets = { \"no\" } ]]\nok = pass == \"no\"");
    expect_mask("ssid = \"pass\", PASS = \"si\"", "ssid = \"pass\", PASS = \"<secret:1>\"");
    expect_mask("", "");

    // Edits from the page: new value, moved placeholder, text that only
    // resembles a placeholder
    const char *orig = "wifi = { { ssid = \"A\", pass = \"uno\" }, { ssid = \"B\", pass = \"due\" } }";
    bool unknown;
    char *u = secrets_unmask("wifi = { { ssid = \"B\", pass = \"<secret:2>\" }, { ssid = \"C\", pass = \"nuova\" } }, "
                             "x = '<secret:1>', z = \"<secret:1>x\"", orig, &unknown);
    CHECK(u && !unknown && strcmp(u, "wifi = { { ssid = \"B\", pass = \"due\" }, { ssid = \"C\", pass = \"nuova\" } }, "
                         "x = \"uno\", z = \"<secret:1>x\"") == 0, "unmask edit: %s", u ? u : "(null)");
    free(u);

    // Index the file does not have (editor still on the numbers of a previous
    // file): error, not the placeholder written as the password
    u = secrets_unmask("pass = \"<secret:9>\"", orig, &unknown);
    CHECK(!u && unknown, "unmask unknown index: %s", u ? u : "(null)");
    free(u);
}

int main(void)
{
    test_layouts();
    test_gestures();
    test_netscan();
    test_dhcp();
    test_secrets();
    printf(s_fail ? "%d tests failed\n" : "all tests passed\n", s_fail);
    return s_fail != 0;
}
