#include "netscan_util.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

#define OUI_REC 32

bool ns_mac_is_private(const uint8_t mac[6])
{
    return mac[0] & 0x02;
}

void ns_mac_str(const uint8_t mac[6], char out[18])
{
    snprintf(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

bool ns_oui_lookup(FILE *f, const uint8_t mac[6], char *out, size_t n)
{
    uint8_t hdr[8];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(hdr, 1, 8, f) != 8 || memcmp(hdr, "OUI1", 4) != 0) return false;
    uint32_t count = hdr[4] | hdr[5] << 8 | hdr[6] << 16 | (uint32_t)hdr[7] << 24;
    uint32_t key = mac[0] << 16 | mac[1] << 8 | mac[2];
    long lo = 0, hi = (long)count - 1;
    while (lo <= hi) {
        long mid = (lo + hi) / 2;
        uint8_t rec[OUI_REC];
        if (fseek(f, 8 + mid * OUI_REC, SEEK_SET) != 0 || fread(rec, 1, OUI_REC, f) != OUI_REC) return false;
        uint32_t k = rec[0] << 16 | rec[1] << 8 | rec[2];
        if (k == key) {
            size_t len = strnlen((const char *)rec + 3, OUI_REC - 3);
            if (len >= n) len = n - 1;
            memcpy(out, rec + 3, len);
            out[len] = 0;
            return true;
        }
        if (k < key) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

// --- DNS (format shared by NBNS and mDNS) ---

// Offset right after the name starting at `off` (stops at the first compression
// pointer). -1 if the packet is truncated.
static int skip_name(const uint8_t *b, int len, int off)
{
    while (off < len) {
        uint8_t l = b[off];
        if (l == 0) return off + 1;
        if ((l & 0xC0) == 0xC0) return off + 2 <= len ? off + 2 : -1;
        off += 1 + l;
    }
    return -1;
}

// Decodes a name into "a.b.c". Follows pointers, at most 8: a
// malicious packet could make them loop around.
static bool decode_name(const uint8_t *b, int len, int off, char *out, size_t n)
{
    size_t o = 0;
    for (int jumps = 0; off >= 0 && off < len;) {
        uint8_t l = b[off];
        if (l == 0) {
            if (o) o--; // the trailing dot
            out[o < n ? o : n - 1] = 0;
            return true;
        }
        if ((l & 0xC0) == 0xC0) {
            if (off + 1 >= len || ++jumps > 8) return false;
            off = (l & 0x3F) << 8 | b[off + 1];
            continue;
        }
        if (off + 1 + l > len) return false;
        for (int i = 0; i < l && o + 2 < n; i++) out[o++] = b[off + 1 + i];
        if (o + 2 < n) out[o++] = '.';
        off += 1 + l;
    }
    return false;
}

int ns_nbns_query(uint8_t buf[50])
{
    static const uint8_t head[12] = {0x47, 0x52, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(buf, head, 12);
    buf[12] = 0x20;
    // Name "*" + 15 NULs, each byte encoded as two letters A..P
    buf[13] = 'C';
    buf[14] = 'K';
    for (int i = 15; i < 45; i++) buf[i] = 'A';
    buf[45] = 0;
    buf[46] = 0x00; // NBSTAT = 0x0021
    buf[47] = 0x21;
    buf[48] = 0x00; // class IN
    buf[49] = 0x01;
    return 50;
}

bool ns_nbns_parse(const uint8_t *b, int len, char *name, size_t n)
{
    if (len < 12 || (b[3] & 0x0F) != 0 || (b[6] << 8 | b[7]) < 1) return false;
    int p = skip_name(b, len, 12);
    if (p < 0 || p + 11 > len) return false;
    p += 10; // type, class, ttl, data length
    int count = b[p++];
    const uint8_t *pick = NULL;
    for (int i = 0; i < count && p + 18 <= len; i++, p += 18) {
        bool group = b[p + 16] & 0x80;
        if (b[p + 15] == 0x00 && !group) {
            pick = b + p;
            break;
        }
        if (!pick) pick = b + p;
    }
    if (!pick) return false;
    int l = 15;
    while (l > 0 && pick[l - 1] == ' ') l--;
    if (l == 0) return false;
    if ((size_t)l >= n) l = n - 1;
    memcpy(name, pick, l);
    name[l] = 0;
    return true;
}

int ns_mdns_ptr_query(uint8_t *buf, size_t n, const uint8_t ip[4])
{
    if (n < 64) return 0;
    static const uint8_t head[12] = {0x47, 0x52, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    memcpy(buf, head, 12);
    int o = 12;
    for (int i = 3; i >= 0; i--) {
        char d[4];
        int l = snprintf(d, sizeof(d), "%u", ip[i]);
        buf[o++] = l;
        memcpy(buf + o, d, l);
        o += l;
    }
    static const char tail[] = "\7in-addr\4arpa";
    memcpy(buf + o, tail, sizeof(tail)); // includes the trailing NUL
    o += sizeof(tail);
    buf[o++] = 0x00; // PTR = 12
    buf[o++] = 0x0C;
    buf[o++] = 0x00; // class IN
    buf[o++] = 0x01;
    return o;
}

bool ns_mdns_ptr_parse(const uint8_t *b, int len, char *name, size_t n)
{
    if (len < 12) return false;
    int qd = b[4] << 8 | b[5], an = b[6] << 8 | b[7];
    int p = 12;
    for (int i = 0; i < qd; i++) {
        p = skip_name(b, len, p);
        if (p < 0) return false;
        p += 4;
    }
    for (int i = 0; i < an; i++) {
        p = skip_name(b, len, p);
        if (p < 0 || p + 10 > len) return false;
        int type = b[p] << 8 | b[p + 1], rdlen = b[p + 8] << 8 | b[p + 9];
        p += 10;
        if (p + rdlen > len) return false;
        if (type == 12) {
            char full[64];
            if (!decode_name(b, len, p, full, sizeof(full))) return false;
            size_t l = strlen(full);
            if (l > 6 && strcasecmp(full + l - 6, ".local") == 0) full[l - 6] = 0;
            if (!*full) return false;
            strlcpy(name, full, n);
            return true;
        }
        p += rdlen;
    }
    return false;
}

void ns_display_text(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    for (const unsigned char *s = (const unsigned char *)src; *s && o + 1 < n; s++) {
        if (*s >= 0x80) {
            if (*s >= 0xC0) dst[o++] = '?'; // a single '?' per multibyte character
        } else {
            dst[o++] = (*s >= 0x20 && *s != 0x7F) ? *s : '?';
        }
    }
    dst[o] = 0;
}

size_t ns_csv_field(char *dst, size_t n, const char *src)
{
    if (n == 0) return 0;
    size_t o = 0;
    // The check must be done on the first character that will remain in the field: leading
    // control characters are removed below, and with "\t=HYPERLINK(...)" a
    // check on the first raw byte let the formula through.
    while (*src && ((unsigned char)*src < 0x20 || *src == 0x7F)) src++;
    bool quote = false, formula = strchr("=+-@", *src) && *src;
    // A signed number (the RSSI, "-77") is not a formula: the apostrophe would turn it into text
    if (formula && src[1] && strspn(src + 1, "0123456789.") == strlen(src + 1) && *src != '=' && *src != '@') formula = false;
    for (const char *s = src; *s; s++) {
        if (*s == ',' || *s == '"') quote = true;
    }
    // Room for the quotes (2), the apostrophe (1) and the NUL
    if (quote && o + 1 < n) dst[o++] = '"';
    if (formula && o + 1 < n) dst[o++] = '\'';
    for (const unsigned char *s = (const unsigned char *)src; *s && o + 3 < n; s++) {
        if (*s < 0x20 || *s == 0x7F) continue; // newlines and control characters would break the row
        if (*s == '"') dst[o++] = '"';
        dst[o++] = *s;
    }
    if (quote && o + 2 <= n) dst[o++] = '"';
    dst[o] = 0;
    return o;
}

void ns_file_slug(char *dst, size_t n, const char *src)
{
    size_t o = 0;
    for (; *src && o + 1 < n; src++) dst[o++] = isalnum((unsigned char)*src) || *src == '-' || *src == '_' ? *src : '_';
    dst[o] = 0;
}

bool ns_sweep_range(uint32_t ip, uint32_t mask, uint32_t *first, uint32_t *last)
{
    // A mask shorter than /24 (larger network) is limited to the IP's /24
    if (mask < 0xFFFFFF00u) mask = 0xFFFFFF00u;
    uint32_t net = ip & mask, bcast = net | ~mask;
    if (bcast - net < 3) return false; // /31, /32: no hosts to look for
    *first = net + 1;
    *last = bcast - 1;
    return true;
}

// --- DHCP ---

#define DHCP_FIXED   236 // BOOTP fields up to and including `file`
#define DHCP_MIN_LEN 300 // some servers drop shorter BOOTP messages (RFC 1542)
#define UDP_HDR      8

static uint32_t sum16(uint32_t s, const uint8_t *p, int len)
{
    for (int i = 0; i + 1 < len; i += 2) s += p[i] << 8 | p[i + 1];
    if (len & 1) s += p[len - 1] << 8;
    return s;
}

int ns_dhcp_discover(uint8_t *buf, size_t n, const uint8_t mac[6], uint32_t xid, const uint8_t src[4])
{
    int len = UDP_HDR + DHCP_MIN_LEN;
    if (n < (size_t)len) return 0;
    memset(buf, 0, len);
    uint8_t *d = buf + UDP_HDR;
    d[0] = 1; // BOOTREQUEST
    d[1] = 1; // Ethernet
    d[2] = 6;
    d[4] = xid >> 24;
    d[5] = xid >> 16;
    d[6] = xid >> 8;
    d[7] = xid;
    d[10] = 0x80; // broadcast flag
    memcpy(d + 28, mac, 6);
    static const uint8_t opts[] = {
        0x63, 0x82, 0x53, 0x63, // magic cookie
        53, 1, 1,               // DHCPDISCOVER
        55, 5, 1, 3, 6, 51, 54, // asks for mask, router, DNS, lease time, server id
        255,
    };
    memcpy(d + DHCP_FIXED, opts, sizeof(opts));

    buf[1] = 68;
    buf[3] = 67;
    buf[4] = len >> 8;
    buf[5] = len;
    // UDP checksum with the IPv4 pseudo-header. It is optional in IPv4, but
    // some servers or firewalls drop datagrams with a zero checksum.
    static const uint8_t bcast[4] = {255, 255, 255, 255};
    uint32_t c = sum16(0, src, 4);
    c = sum16(c, bcast, 4);
    c += 17 + len;
    c = sum16(c, buf, len);
    while (c >> 16) c = (c & 0xFFFF) + (c >> 16);
    uint16_t sum = ~c;
    if (sum == 0) sum = 0xFFFF; // 0 would mean "no checksum"
    buf[6] = sum >> 8;
    buf[7] = sum;
    return len;
}

bool ns_dhcp_parse_offer(const uint8_t *pkt, int len, uint32_t xid, ns_dhcp_offer_t *out)
{
    if (len < 20 || (pkt[0] >> 4) != 4 || pkt[9] != 17) return false;
    int ihl = (pkt[0] & 0x0F) * 4;
    if (ihl < 20 || len < ihl + UDP_HDR + DHCP_FIXED + 4) return false;
    const uint8_t *u = pkt + ihl;
    if ((u[0] << 8 | u[1]) != 67 || (u[2] << 8 | u[3]) != 68) return false;
    const uint8_t *d = u + UDP_HDR;
    int dl = len - ihl - UDP_HDR;
    uint32_t x = (uint32_t)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7];
    if (d[0] != 2 || x != xid || memcmp(d + DHCP_FIXED, "\x63\x82\x53\x63", 4) != 0) return false;

    memset(out, 0, sizeof(*out));
    memcpy(out->offered, d + 16, 4);
    memcpy(out->server, pkt + 12, 4);
    int type = 0;
    for (int i = DHCP_FIXED + 4; i < dl;) {
        uint8_t code = d[i];
        if (code == 255) break;
        if (code == 0) {
            i++;
            continue;
        }
        if (i + 1 >= dl || i + 2 + d[i + 1] > dl) return false; // truncated option
        int ol = d[i + 1];
        const uint8_t *v = d + i + 2;
        if (code == 53 && ol == 1) type = v[0];
        else if (code == 54 && ol == 4) memcpy(out->server, v, 4);
        else if (code == 1 && ol == 4) memcpy(out->mask, v, 4);
        else if (code == 3 && ol >= 4) memcpy(out->router, v, 4); // the first router
        else if (code == 6 && ol >= 4) memcpy(out->dns, v, 4);    // the first DNS
        else if (code == 51 && ol == 4) out->lease_s = (uint32_t)v[0] << 24 | v[1] << 16 | v[2] << 8 | v[3];
        i += 2 + ol;
    }
    return type == 2; // DHCPOFFER
}
