#include "lan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/etharp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "net.h"
#include "netscan_util.h"
#include "storage.h"

static const char *TAG = "lan";

// How many ARP requests go out before reading the table. lwip's table
// has 10 entries (ARP_TABLE_SIZE): a larger batch would lose some of the replies.
#define BATCH      6
#define BATCH_WAIT 150 // ms: time for a batch's replies
#define NAME_WAIT  1500 // ms: time for NBNS/mDNS replies

#define OUI_PATH STORAGE_ROOT "/oui.bin"

// lwip is not thread-safe towards other tasks (CONFIG_LWIP_TCPIP_CORE_LOCKING is
// off): etharp functions run in the tcpip_thread task.
typedef struct {
    struct netif *nif;
    uint32_t first; // host order
    int count;
    struct {
        uint32_t ip; // host order
        uint8_t mac[6];
    } table[ARP_TABLE_SIZE];
    int table_n;
    SemaphoreHandle_t done;
} arp_job_t;

static void arp_send_cb(void *arg)
{
    arp_job_t *j = arg;
    for (int i = 0; i < j->count; i++) {
        ip4_addr_t a;
        a.addr = lwip_htonl(j->first + i);
        etharp_request(j->nif, &a);
    }
    xSemaphoreGive(j->done);
}

static void arp_read_cb(void *arg)
{
    arp_job_t *j = arg;
    j->table_n = 0;
    for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t *ip;
        struct netif *nif;
        struct eth_addr *eth;
        if (!etharp_get_entry(i, &ip, &nif, &eth) || nif != j->nif) continue;
        j->table[j->table_n].ip = lwip_ntohl(ip->addr);
        memcpy(j->table[j->table_n].mac, eth->addr, 6);
        j->table_n++;
    }
    xSemaphoreGive(j->done);
}

static bool run_in_tcpip(arp_job_t *j, tcpip_callback_fn fn)
{
    if (tcpip_callback(fn, j) != ERR_OK) return false;
    if (xSemaphoreTake(j->done, pdMS_TO_TICKS(2000)) == pdTRUE) return true;
    // The callback is still queued and will use `j`: the caller aborts the
    // sweep and frees `j`, so it must be awaited first (otherwise use-after-free,
    // or a late Give that spuriously unblocks the next Take).
    xSemaphoreTake(j->done, portMAX_DELAY);
    return false;
}

static void put_ip(uint8_t out[4], uint32_t host_order)
{
    out[0] = host_order >> 24;
    out[1] = host_order >> 16;
    out[2] = host_order >> 8;
    out[3] = host_order;
}

static int find_host(const lan_host_t *h, int n, const uint8_t ip[4])
{
    for (int i = 0; i < n; i++) {
        if (memcmp(h[i].ip, ip, 4) == 0) return i;
    }
    return -1;
}

static int cmp_ip(const void *a, const void *b)
{
    return memcmp(((const lan_host_t *)a)->ip, ((const lan_host_t *)b)->ip, 4);
}

// A non-blocking UDP socket already bound to an arbitrary local port
static int udp_socket(void)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (s < 0) return -1;
    fcntl(s, F_SETFL, O_NONBLOCK);
    return s;
}

static void send_udp(int s, const uint8_t ip[4], int port, const uint8_t *buf, int len)
{
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(port)};
    memcpy(&to.sin_addr.s_addr, ip, 4);
    sendto(s, buf, len, 0, (struct sockaddr *)&to, sizeof(to));
}

// Reads the replies received on `s` and assigns names to the hosts that lack one
static void collect_names(int s, lan_host_t *h, int n, bool (*parse)(const uint8_t *, int, char *, size_t))
{
    uint8_t buf[512];
    struct sockaddr_in from;
    socklen_t fl = sizeof(from);
    int r;
    while ((r = recvfrom(s, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl)) > 0) {
        fl = sizeof(from);
        int i = find_host(h, n, (const uint8_t *)&from.sin_addr.s_addr);
        char raw[24];
        if (i < 0 || h[i].name[0] || !parse(buf, r, raw, sizeof(raw))) continue;
        ns_display_text(h[i].name, sizeof(h[i].name), raw);
    }
}

static void resolve_names(lan_host_t *h, int n, lan_progress_fn cb, void *arg)
{
    int nb = udp_socket(), md = udp_socket();
    if (nb < 0 || md < 0) {
        if (nb >= 0) close(nb);
        if (md >= 0) close(md);
        return;
    }
    uint8_t q_nb[50], q_md[64];
    ns_nbns_query(q_nb);
    int sent = 0;
    for (int i = 0; i < n; i++) {
        if (h[i].flags & LAN_SELF) continue;
        send_udp(nb, h[i].ip, 137, q_nb, sizeof(q_nb));
        int l = ns_mdns_ptr_query(q_md, sizeof(q_md), h[i].ip);
        send_udp(md, h[i].ip, 5353, q_md, l);
        if (++sent % 8 == 0) vTaskDelay(pdMS_TO_TICKS(5)); // do not exhaust the pbufs
    }
    int64_t end = esp_timer_get_time() + NAME_WAIT * 1000LL;
    for (int64_t now; (now = esp_timer_get_time()) < end;) {
        collect_names(nb, h, n, ns_nbns_parse);
        collect_names(md, h, n, ns_mdns_ptr_parse);
        if (cb && !cb(1, NAME_WAIT - (int)((end - now) / 1000), NAME_WAIT, n, arg)) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    close(nb);
    close(md);
}

static void fill_vendors(lan_host_t *h, int n)
{
    FILE *f = fopen(OUI_PATH, "rb"); // optional: without the file only the MAC remains
    for (int i = 0; i < n; i++) {
        if (ns_mac_is_private(h[i].mac)) {
            strlcpy(h[i].vendor, "(private MAC)", sizeof(h[i].vendor));
        } else if (f) {
            char v[29];
            if (ns_oui_lookup(f, h[i].mac, v, sizeof(v))) ns_display_text(h[i].vendor, sizeof(h[i].vendor), v);
        }
    }
    if (f) fclose(f);
}

int lan_sweep(lan_host_t *out, int max, lan_progress_fn cb, void *arg)
{
    esp_netif_t *en = net_sta_netif();
    esp_netif_ip_info_t ipi;
    if (!en || esp_netif_get_ip_info(en, &ipi) != ESP_OK || ipi.ip.addr == 0) return -1;
    uint32_t first, last, self = lwip_ntohl(ipi.ip.addr), gw = lwip_ntohl(ipi.gw.addr);
    if (!ns_sweep_range(self, lwip_ntohl(ipi.netmask.addr), &first, &last)) return -1;

    arp_job_t *j = calloc(1, sizeof(*j));
    if (!j) return -1;
    j->nif = esp_netif_get_netif_impl(en);
    j->done = xSemaphoreCreateBinary();
    if (!j->nif || !j->done) {
        if (j->done) vSemaphoreDelete(j->done);
        free(j);
        return -1;
    }

    int n = 0, total = last - first + 1;
    // The dongle does not answer itself via ARP: add it by hand
    put_ip(out[n].ip, self);
    esp_netif_get_mac(en, out[n].mac);
    out[n].flags = LAN_SELF;
    memset(out[n].name, 0, sizeof(out[n].name));
    memset(out[n].vendor, 0, sizeof(out[n].vendor));
    n++;

    bool aborted = false;
    for (int done = 0; done < total && !aborted; done += BATCH) {
        j->first = first + done;
        j->count = total - done < BATCH ? total - done : BATCH;
        if (!run_in_tcpip(j, arp_send_cb)) {
            ESP_LOGW(TAG, "tcpip_thread not responding");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(BATCH_WAIT));
        if (run_in_tcpip(j, arp_read_cb)) {
            for (int k = 0; k < j->table_n; k++) {
                uint32_t ip = j->table[k].ip;
                uint8_t b[4];
                put_ip(b, ip);
                if (ip < first || ip > last || ip == self || find_host(out, n, b) >= 0 || n >= max) continue;
                memcpy(out[n].ip, b, 4);
                memcpy(out[n].mac, j->table[k].mac, 6);
                out[n].flags = ip == gw ? LAN_GW : 0;
                memset(out[n].name, 0, sizeof(out[n].name));
                memset(out[n].vendor, 0, sizeof(out[n].vendor));
                n++;
            }
        }
        if (cb && !cb(0, done + j->count, total, n, arg)) aborted = true;
    }
    vSemaphoreDelete(j->done);
    free(j);
    if (aborted) return -1;

    qsort(out, n, sizeof(*out), cmp_ip);
    resolve_names(out, n, cb, arg);
    fill_vendors(out, n);
    return n;
}

// --- DHCP servers ---

#define DHCP_RX_MAX 1500 // an OFFER fits in an Ethernet frame
#define DHCP_SENDS  3

// MAC of a network host via ARP (request, short wait, table read)
static bool arp_lookup(struct netif *nif, uint32_t ip_be, uint8_t mac[6])
{
    arp_job_t *j = calloc(1, sizeof(*j));
    if (!j) return false;
    j->nif = nif;
    j->first = lwip_ntohl(ip_be);
    j->count = 1;
    j->done = xSemaphoreCreateBinary();
    bool found = false;
    if (j->done && run_in_tcpip(j, arp_send_cb)) {
        vTaskDelay(pdMS_TO_TICKS(300));
        if (run_in_tcpip(j, arp_read_cb)) {
            for (int k = 0; k < j->table_n && !found; k++) {
                if (j->table[k].ip != j->first) continue;
                memcpy(mac, j->table[k].mac, 6);
                found = true;
            }
        }
    }
    if (j->done) vSemaphoreDelete(j->done);
    free(j);
    return found;
}

int lan_dhcp_probe(lan_dhcp_server_t *out, int max, int listen_ms, lan_dhcp_progress_fn cb, void *arg)
{
    esp_netif_t *en = net_sta_netif();
    esp_netif_ip_info_t ipi;
    uint8_t mac[6];
    if (!en || esp_netif_get_ip_info(en, &ipi) != ESP_OK || ipi.ip.addr == 0 || esp_netif_get_mac(en, mac) != ESP_OK) {
        return -1;
    }
    // Port 68 belongs to lwIP's DHCP client, which does not share it (its pcb
    // has no SO_REUSEADDR). A raw UDP socket instead sees every datagram
    // before demultiplexing and does not consume it: the OFFERs also reach
    // lwIP's client, which discards them because the xid is not its own.
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
    uint8_t *buf = malloc(DHCP_RX_MAX);
    if (s < 0 || !buf) {
        if (s >= 0) close(s);
        free(buf);
        return -1;
    }
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    fcntl(s, F_SETFL, O_NONBLOCK);

    uint32_t xid = esp_random();
    uint8_t disc[320];
    int dl = ns_dhcp_discover(disc, sizeof(disc), mac, xid, (const uint8_t *)&ipi.ip.addr);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_BROADCAST)};
    int n = 0;
    bool aborted = sendto(s, disc, dl, 0, (struct sockaddr *)&to, sizeof(to)) != dl;
    if (aborted) ESP_LOGW(TAG, "DISCOVER not sent: errno %d", errno);
    int sent = 1;
    int64_t t0 = esp_timer_get_time();
    while (!aborted) {
        int elapsed = (esp_timer_get_time() - t0) / 1000;
        if (elapsed >= listen_ms) break;
        // Two more sends at 1/3 and 2/3, with the same xid: WiFi broadcast
        // has no retransmissions, and a DISCOVER or an OFFER gets lost
        if (sent < DHCP_SENDS && elapsed >= listen_ms * sent / DHCP_SENDS) {
            sendto(s, disc, dl, 0, (struct sockaddr *)&to, sizeof(to));
            sent++;
        }
        int r;
        while ((r = recv(s, buf, DHCP_RX_MAX, 0)) > 0) {
            ns_dhcp_offer_t o;
            if (!ns_dhcp_parse_offer(buf, r, xid, &o)) continue;
            int i = 0;
            while (i < n && memcmp(out[i].offer.server, o.server, 4) != 0) i++;
            if (i < n || n >= max) continue; // same server twice (it answers both sends)
            memset(&out[n], 0, sizeof(out[n]));
            out[n++].offer = o;
        }
        if (cb && !cb(elapsed, n, arg)) aborted = true;
        else vTaskDelay(pdMS_TO_TICKS(100));
    }
    close(s);
    free(buf);
    if (aborted) return -1;

    // The MAC tells what device the server is: a router or a PC with a
    // DHCP service turned on by mistake
    struct netif *nif = esp_netif_get_netif_impl(en);
    FILE *f = fopen(OUI_PATH, "rb");
    for (int i = 0; i < n; i++) {
        uint32_t ip;
        memcpy(&ip, out[i].offer.server, 4);
        if ((ip & ipi.netmask.addr) != (ipi.ip.addr & ipi.netmask.addr)) continue; // behind a relay
        if (cb && !cb(listen_ms, n, arg)) break;
        if (!nif || !arp_lookup(nif, ip, out[i].mac)) continue;
        if (ns_mac_is_private(out[i].mac)) {
            strlcpy(out[i].vendor, "(private MAC)", sizeof(out[i].vendor));
        } else if (f) {
            char v[29];
            if (ns_oui_lookup(f, out[i].mac, v, sizeof(v))) ns_display_text(out[i].vendor, sizeof(out[i].vendor), v);
        }
    }
    if (f) fclose(f);
    return n;
}

// --- Probes ---

static uint16_t inet_sum(const uint8_t *p, int len)
{
    uint32_t s = 0;
    for (int i = 0; i + 1 < len; i += 2) s += p[i] << 8 | p[i + 1];
    if (len & 1) s += p[len - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return ~s;
}

int probe_ping(uint32_t ip_be, int timeout_ms)
{
    int s = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (s < 0) return -1;
    struct timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = (timeout_ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t pkt[8 + 32] = {8, 0, 0, 0, 0x47, 0x52, 0, 1}; // echo request, id "GR", seq 1
    memset(pkt + 8, 'g', 32);
    uint16_t c = inet_sum(pkt, sizeof(pkt));
    pkt[2] = c >> 8;
    pkt[3] = c;
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_addr.s_addr = ip_be};
    int64_t t0 = esp_timer_get_time(), end = t0 + timeout_ms * 1000LL;
    int ms = -1;
    if (sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&to, sizeof(to)) == sizeof(pkt)) {
        uint8_t rx[128];
        // A raw socket also receives replies meant for others: look for ours
        while (esp_timer_get_time() < end) {
            int r = recv(s, rx, sizeof(rx), 0);
            if (r < 0) break;
            int ihl = (rx[0] & 0x0F) * 4;
            if (r >= ihl + 8 && rx[ihl] == 0 && rx[ihl + 4] == 0x47 && rx[ihl + 5] == 0x52) {
                ms = (esp_timer_get_time() - t0) / 1000;
                break;
            }
        }
    }
    close(s);
    return ms;
}

int probe_dns(const char *host, uint32_t *ip_be)
{
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM}, *res = NULL;
    int64_t t0 = esp_timer_get_time();
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return -1;
    *ip_be = ((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr;
    freeaddrinfo(res);
    return (esp_timer_get_time() - t0) / 1000;
}

// connect() with a deadline: SO_SNDTIMEO does not apply to connect in lwIP, and with
// a captive portal that drops SYNs the wait lasted tens of seconds
// (SYN retransmissions), with the app deaf to the exit hold.
static bool connect_timeout(int s, const struct sockaddr *to, socklen_t len, int ms)
{
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    bool ok = connect(s, to, len) == 0;
    if (!ok && errno == EINPROGRESS) {
        fd_set w;
        FD_ZERO(&w);
        FD_SET(s, &w);
        struct timeval tv = {.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
        int err = 0;
        socklen_t el = sizeof(err);
        ok = select(s + 1, NULL, &w, NULL, &tv) == 1 && getsockopt(s, SOL_SOCKET, SO_ERROR, &err, &el) == 0 && !err;
    }
    fcntl(s, F_SETFL, fl);
    return ok;
}

int probe_http(uint32_t ip_be, const char *host, const char *path, int *code)
{
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) return -1;
    struct timeval tv = {.tv_sec = 4};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons(80), .sin_addr.s_addr = ip_be};
    int64_t t0 = esp_timer_get_time();
    int ms = -1;
    char req[160], resp[48];
    int rl = snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (connect_timeout(s, (struct sockaddr *)&to, sizeof(to), 4000) && send(s, req, rl, 0) == rl) {
        int r = recv(s, resp, sizeof(resp) - 1, 0);
        resp[r > 0 ? r : 0] = 0;
        // "HTTP/1.1 204 No Content"
        if (r > 12 && strncmp(resp, "HTTP/", 5) == 0) {
            *code = atoi(resp + 9);
            ms = (esp_timer_get_time() - t0) / 1000;
        }
    }
    close(s);
    return ms;
}
