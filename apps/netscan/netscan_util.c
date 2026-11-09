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

// --- DNS (formato comune a NBNS e mDNS) ---

// Offset subito dopo il nome che inizia a `off` (si ferma al primo puntatore
// di compressione). -1 se il pacchetto è troncato.
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

// Decodifica un nome in "a.b.c". Segue i puntatori, al massimo 8: un
// pacchetto malevolo potrebbe farli girare in cerchio.
static bool decode_name(const uint8_t *b, int len, int off, char *out, size_t n)
{
    size_t o = 0;
    for (int jumps = 0; off >= 0 && off < len;) {
        uint8_t l = b[off];
        if (l == 0) {
            if (o) o--; // il punto finale
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
    // Nome "*" + 15 NUL, ogni byte codificato come due lettere A..P
    buf[13] = 'C';
    buf[14] = 'K';
    for (int i = 15; i < 45; i++) buf[i] = 'A';
    buf[45] = 0;
    buf[46] = 0x00; // NBSTAT = 0x0021
    buf[47] = 0x21;
    buf[48] = 0x00; // classe IN
    buf[49] = 0x01;
    return 50;
}

bool ns_nbns_parse(const uint8_t *b, int len, char *name, size_t n)
{
    if (len < 12 || (b[3] & 0x0F) != 0 || (b[6] << 8 | b[7]) < 1) return false;
    int p = skip_name(b, len, 12);
    if (p < 0 || p + 11 > len) return false;
    p += 10; // tipo, classe, ttl, lunghezza dati
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
    memcpy(buf + o, tail, sizeof(tail)); // include il NUL finale
    o += sizeof(tail);
    buf[o++] = 0x00; // PTR = 12
    buf[o++] = 0x0C;
    buf[o++] = 0x00; // classe IN
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
            if (*s >= 0xC0) dst[o++] = '?'; // un solo '?' per carattere multibyte
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
    bool quote = false, formula = strchr("=+-@", *src) && *src;
    // Un numero con segno (l'RSSI, "-77") non è una formula: l'apice lo trasformerebbe in testo
    if (formula && src[1] && strspn(src + 1, "0123456789.") == strlen(src + 1) && *src != '=' && *src != '@') formula = false;
    for (const char *s = src; *s; s++) {
        if (*s == ',' || *s == '"') quote = true;
    }
    // Spazio per le virgolette (2), l'apice (1) e il NUL
    if (quote && o + 1 < n) dst[o++] = '"';
    if (formula && o + 1 < n) dst[o++] = '\'';
    for (const unsigned char *s = (const unsigned char *)src; *s && o + 3 < n; s++) {
        if (*s < 0x20 || *s == 0x7F) continue; // a capo e controlli spezzerebbero la riga
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
    // Una maschera più corta di /24 (rete più grande) si limita alla /24 dell'IP
    if (mask < 0xFFFFFF00u) mask = 0xFFFFFF00u;
    uint32_t net = ip & mask, bcast = net | ~mask;
    if (bcast - net < 3) return false; // /31, /32: nessun host da cercare
    *first = net + 1;
    *last = bcast - 1;
    return true;
}
