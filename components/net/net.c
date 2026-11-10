#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "net";

#define BIT_GOT_IP  BIT0
#define BIT_FAILED  BIT1

// Password dell'hotspot se config.lua non ne dà una valida: WPA2 vuole
// almeno 8 caratteri e un hotspot aperto sarebbe peggio.
#define AP_DEFAULT_PASS "grillo-dongle"

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_ev;
static bool s_inited;
static bool s_started;
// Lease per task: all'uscita di un'app l'app manager rilascia solo quelli
// del task dell'app. Un servizio con un proprio task (es. un futuro log via
// rete) non perde la connessione quando l'app in primo piano esce.
#define MAX_OWNERS 4
typedef struct {
    TaskHandle_t task;
    int count;
} owner_t;
static owner_t s_owners[MAX_OWNERS];
static net_mode_t s_mode; // modalità effettiva (NET_STA o NET_AP) quando s_started
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static net_event_cb_t s_cb;
static bool s_up;
static int s_clients;
static int s_retries;
static bool s_connecting;
static char s_ssid[33];
static char s_ap_ssid[33];
static char s_ap_pass[65];

void net_set_event_cb(net_event_cb_t cb)
{
    s_cb = cb;
}

static void notify(net_event_t ev)
{
    if (s_cb) s_cb(ev);
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_DISCONNECTED:
        if (s_up) {
            s_up = false;
            notify(NET_EV_DOWN);
        }
        // Durante net_acquire il chiamante conta i tentativi; a regime
        // (rete persa dopo la connessione) riproviamo senza fine.
        if (s_connecting) {
            if (++s_retries >= 3) xEventGroupSetBits(s_ev, BIT_FAILED);
            else esp_wifi_connect();
        } else if (s_started && s_mode == NET_STA) {
            esp_wifi_connect();
        }
        break;
    case WIFI_EVENT_AP_START:
        s_up = true;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        notify(NET_EV_UP);
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        s_clients++;
        notify(NET_EV_CLIENT);
        break;
    case WIFI_EVENT_AP_STADISCONNECTED:
        if (s_clients > 0) s_clients--;
        notify(NET_EV_CLIENT);
        break;
    default: break;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        s_up = true;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        notify(NET_EV_UP);
    }
}

static esp_err_t net_init_once(void)
{
    if (s_inited) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_ev = xEventGroupCreate();
    s_lock = xSemaphoreCreateMutex();
    if (!s_sta_netif || !s_ap_netif || !s_ev || !s_lock) return ESP_ERR_NO_MEM;

    // I valori di config si leggono qui e non in net_acquire: la config
    // viene ricaricata solo dopo un'uscita da USB Disk, mai a WiFi acceso.
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "Grillo-%02X%02X", mac[4], mac[5]);

    s_inited = true;
    return ESP_OK;
}

static void load_ap_config(void)
{
    const char *ssid = config_str("hotspot.ssid", "");
    if (*ssid) strlcpy(s_ap_ssid, ssid, sizeof(s_ap_ssid));
    const char *pass = config_str("hotspot.pass", "");
    if (strlen(pass) < 8) {
        if (*pass) ESP_LOGW(TAG, "hotspot.pass troppo corta (<8): uso la password di default");
        pass = AP_DEFAULT_PASS;
    }
    strlcpy(s_ap_pass, pass, sizeof(s_ap_pass));
}

const char *net_ap_ssid(void)
{
    return s_ap_ssid;
}

const char *net_ap_pass(void)
{
    return s_ap_pass;
}

static void wifi_stop(void)
{
    s_connecting = false;
    s_started = false;
    s_up = false;
    s_clients = 0;
    esp_wifi_stop();
    esp_wifi_deinit();
    notify(NET_EV_DOWN);
}

static esp_err_t wifi_begin(wifi_mode_t mode)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(mode);
    return ESP_OK;
}

// Scansiona e raccoglie le reti note visibili, la più forte per prima. Se
// ce n'è più d'una e il chiamante ha dato `choose`, decide lui (menu sul
// display); altrimenti vince la più forte. 1 = scelta fatta, 0 = nessuna
// rete nota visibile, -1 = scelta annullata.
static int pick_known(wifi_config_t *out, char *pass_out, size_t pass_len, net_choose_fn choose, void *arg)
{
    int known = config_count("wifi");
    if (known == 0) return 0;

    wifi_scan_config_t sc = {.show_hidden = false};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 20) n = 20; // limite di RAM: ogni record pesa circa 100 byte
    wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof(*recs));
    if (!recs) return 0;
    esp_wifi_scan_get_ap_records(&n, recs);

    net_candidate_t cand[NET_MAX_CANDIDATES];
    int idx[NET_MAX_CANDIDATES]; // voce di config.lua di ogni candidata
    int nc = 0;
    for (int i = 0; i < known; i++) {
        char key[24];
        snprintf(key, sizeof(key), "wifi.%d.ssid", i + 1);
        const char *ssid = config_str(key, "");
        if (!*ssid) continue;
        snprintf(key, sizeof(key), "wifi.%d.pass", i + 1);
        // Reti aperte non supportate (decisione del 2026-11-10): una voce
        // senza password non viene mai usata.
        if (!*config_str(key, "")) {
            ESP_LOGW(TAG, "rete \"%s\" senza password in config.lua: ignorata", ssid);
            continue;
        }
        int best = -1000;
        for (int j = 0; j < n; j++) {
            // Più access point con lo stesso SSID: conta il più forte
            if (strcmp((const char *)recs[j].ssid, ssid) == 0 && recs[j].rssi > best) best = recs[j].rssi;
        }
        if (best == -1000 || nc == NET_MAX_CANDIDATES) continue;
        // Inserimento ordinato per RSSI decrescente
        int k = nc++;
        for (; k > 0 && cand[k - 1].rssi < best; k--) {
            cand[k] = cand[k - 1];
            idx[k] = idx[k - 1];
        }
        strlcpy(cand[k].ssid, ssid, sizeof(cand[k].ssid));
        cand[k].rssi = best;
        idx[k] = i;
    }
    free(recs);
    if (nc == 0) return 0;

    int c = nc > 1 && choose ? choose(cand, nc, arg) : 0;
    if (c < 0 || c >= nc) return -1;
    memset(out, 0, sizeof(*out));
    strlcpy((char *)out->sta.ssid, cand[c].ssid, sizeof(out->sta.ssid));
    char key[24];
    snprintf(key, sizeof(key), "wifi.%d.pass", idx[c] + 1);
    strlcpy(pass_out, config_str(key, ""), pass_len);
    return 1;
}

static esp_err_t start_sta(uint32_t timeout_ms, net_choose_fn choose, void *arg, const volatile bool *abort)
{
    esp_err_t err = wifi_begin(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    esp_wifi_start();

    wifi_config_t wc;
    char pass[65];
    int picked = pick_known(&wc, pass, sizeof(pass), choose, arg);
    if (picked <= 0) {
        esp_wifi_stop();
        esp_wifi_deinit();
        return picked < 0 ? ESP_ERR_NOT_FINISHED : ESP_ERR_NOT_FOUND;
    }
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    // Solo PSK: soglia WPA2, quindi niente reti aperte né WEP anche se un
    // access point con lo stesso SSID si presentasse senza cifratura.
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wc.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    strlcpy(s_ssid, (char *)wc.sta.ssid, sizeof(s_ssid));

    ESP_LOGI(TAG, "connessione a \"%s\"", s_ssid);
    notify(NET_EV_CONNECTING);
    s_retries = 0;
    s_mode = NET_STA;
    s_connecting = true;
    xEventGroupClearBits(s_ev, BIT_GOT_IP | BIT_FAILED);
    esp_wifi_connect();
    // A passi di 100 ms per accorgersi dell'uscita dall'app: prima l'attesa
    // durava fino a timeout_ms (15 s) e l'hold di 3 s sembrava non funzionare.
    EventBits_t bits = 0;
    bool aborted = false;
    for (uint32_t waited = 0; waited < timeout_ms && !(bits & (BIT_GOT_IP | BIT_FAILED)); waited += 100) {
        if (abort && *abort) {
            aborted = true;
            break;
        }
        bits = xEventGroupWaitBits(s_ev, BIT_GOT_IP | BIT_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(100));
    }
    s_connecting = false;
    if (bits & BIT_GOT_IP) {
        // Il risparmio energetico di default (WIFI_PS_MIN_MODEM) fa dormire la
        // radio tra i beacon: ping a 120-180 ms, pagine lente e mouse a scatti.
        // Il dongle è alimentato da USB, quindi non serve.
        esp_wifi_set_ps(WIFI_PS_NONE);
        s_mode = NET_STA;
        s_started = true;
        return ESP_OK;
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    if (aborted) return ESP_ERR_NOT_FINISHED;
    return (bits & BIT_FAILED) ? ESP_ERR_INVALID_RESPONSE : ESP_ERR_TIMEOUT;
}

static esp_err_t start_ap(void)
{
    esp_err_t err = wifi_begin(WIFI_MODE_AP);
    if (err != ESP_OK) return err;
    wifi_config_t wc = {0};
    strlcpy((char *)wc.ap.ssid, s_ap_ssid, sizeof(wc.ap.ssid));
    strlcpy((char *)wc.ap.password, s_ap_pass, sizeof(wc.ap.password));
    wc.ap.ssid_len = strlen(s_ap_ssid);
    wc.ap.channel = 6;
    wc.ap.max_connection = 2; // basta: un solo client alla volta usa il Web HID
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &wc);
    xEventGroupClearBits(s_ev, BIT_GOT_IP | BIT_FAILED);
    s_mode = NET_AP;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_wifi_deinit();
        return err;
    }
    xEventGroupWaitBits(s_ev, BIT_GOT_IP, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));
    s_mode = NET_AP;
    s_started = true;
    return ESP_OK;
}

// Solo radio accesa: nessuna connessione, quindi nessun tentativo né attesa.
static esp_err_t start_scan_only(void)
{
    esp_err_t err = wifi_begin(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    err = esp_wifi_start();
    if (err != ESP_OK) {
        esp_wifi_deinit();
        return err;
    }
    s_mode = NET_SCAN;
    s_started = true;
    return ESP_OK;
}

static int total_leases(void)
{
    int n = 0;
    for (int i = 0; i < MAX_OWNERS; i++) n += s_owners[i].count;
    return n;
}

// Voce del task corrente; con `create` ne occupa una libera. NULL se sono
// tutte occupate da altri task.
static owner_t *current_owner(bool create)
{
    TaskHandle_t me = xTaskGetCurrentTaskHandle();
    owner_t *free_slot = NULL;
    for (int i = 0; i < MAX_OWNERS; i++) {
        if (s_owners[i].count && s_owners[i].task == me) return &s_owners[i];
        if (!s_owners[i].count && !free_slot) free_slot = &s_owners[i];
    }
    if (!create || !free_slot) return NULL;
    free_slot->task = me;
    return free_slot;
}

esp_err_t net_acquire(net_mode_t mode, uint32_t timeout_ms)
{
    return net_acquire_ex(mode, timeout_ms, NULL, NULL, NULL);
}

esp_err_t net_acquire_ex(net_mode_t mode, uint32_t timeout_ms, net_choose_fn choose, void *arg,
                         const volatile bool *abort)
{
    esp_err_t err = net_init_once();
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    owner_t *owner = current_owner(true);
    if (!owner) {
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "troppi task con un lease");
        return ESP_ERR_NO_MEM;
    }

    if (s_started) {
        // NET_AUTO vuole una rete vera: con la radio solo in scansione il
        // chiamante avrebbe avuto un lease senza IP.
        bool ok = (mode == NET_AUTO && s_mode != NET_SCAN) || (mode == NET_STA && s_mode == NET_STA) ||
                  (mode == NET_AP && s_mode == NET_AP) ||
                  (mode == NET_SCAN && (s_mode == NET_SCAN || s_mode == NET_STA));
        if (!ok) {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_STATE;
        }
        owner->count++;
        xSemaphoreGive(s_lock);
        return ESP_OK;
    }

    load_ap_config();
    s_clients = 0;
    // Handler registrati una sola volta, ma dopo la creazione dell'event loop
    static bool handlers;
    if (!handlers) {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip_event, NULL);
        handlers = true;
    }

    if (mode == NET_SCAN) {
        err = start_scan_only();
    } else {
        err = ESP_ERR_NOT_FOUND;
        if (mode != NET_AP) err = start_sta(timeout_ms, choose, arg, abort);
        // Scelta annullata o uscita dall'app durante l'attesa: niente hotspot
        if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED && mode != NET_STA) {
            if (mode == NET_AUTO) ESP_LOGW(TAG, "nessuna rete nota (%s): avvio l'hotspot", esp_err_to_name(err));
            err = start_ap();
        }
    }
    if (err == ESP_OK) owner->count = 1;
    else if (err == ESP_ERR_NOT_FINISHED) ESP_LOGI(TAG, "WiFi non avviato: annullato");
    else ESP_LOGE(TAG, "WiFi non avviato: %s", esp_err_to_name(err));
    xSemaphoreGive(s_lock);
    return err;
}

void net_release(void)
{
    if (!s_inited) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    owner_t *owner = current_owner(false);
    if (owner) owner->count--;
    if (owner && total_leases() == 0 && s_started) wifi_stop();
    xSemaphoreGive(s_lock);
}

void net_release_task(TaskHandle_t task)
{
    if (!s_inited) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_OWNERS; i++) {
        if (s_owners[i].count && s_owners[i].task == task) {
            ESP_LOGI(TAG, "rilascio %d lease lasciati da un task terminato", s_owners[i].count);
            s_owners[i].count = 0;
        }
    }
    if (total_leases() == 0 && s_started) wifi_stop();
    xSemaphoreGive(s_lock);
}

void net_get_info(net_info_t *info)
{
    memset(info, 0, sizeof(*info));
    // s_started arriva dopo l'evento "up": basta s_up, e s_mode è già impostato
    if (!s_inited || !s_up) return;
    info->up = true;
    info->mode = s_mode;
    esp_netif_ip_info_t ip;
    esp_netif_t *nif = s_mode == NET_AP ? s_ap_netif : s_sta_netif;
    if (esp_netif_get_ip_info(nif, &ip) == ESP_OK) esp_ip4addr_ntoa(&ip.ip, info->ip, sizeof(info->ip));
    if (s_mode == NET_AP) {
        strlcpy(info->ssid, s_ap_ssid, sizeof(info->ssid));
        info->clients = s_clients;
    } else {
        strlcpy(info->ssid, s_ssid, sizeof(info->ssid));
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) info->rssi = ap.rssi;
    }
}

esp_netif_t *net_sta_netif(void)
{
    return s_sta_netif;
}

static int cmp_rssi(const void *a, const void *b)
{
    return ((const net_ap_t *)b)->rssi - ((const net_ap_t *)a)->rssi;
}

int net_scan(net_ap_t *out, int max)
{
    if (!s_started || s_mode == NET_AP) return -ESP_ERR_INVALID_STATE;
    // Con show_hidden le reti nascoste compaiono con SSID vuoto
    wifi_scan_config_t sc = {.show_hidden = true};
    esp_err_t err = esp_wifi_scan_start(&sc, true);
    if (err != ESP_OK) return -err;
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n > 40) n = 40; // ogni record pesa circa 100 byte: il limite protegge l'heap
    wifi_ap_record_t *recs = calloc(n ? n : 1, sizeof(*recs));
    if (!recs) return -ESP_ERR_NO_MEM;
    esp_wifi_scan_get_ap_records(&n, recs);
    int count = n < max ? n : max;
    for (int i = 0; i < count; i++) {
        strlcpy(out[i].ssid, (const char *)recs[i].ssid, sizeof(out[i].ssid));
        memcpy(out[i].bssid, recs[i].bssid, 6);
        out[i].rssi = recs[i].rssi;
        out[i].channel = recs[i].primary;
        out[i].auth = recs[i].authmode;
    }
    free(recs);
    qsort(out, count, sizeof(*out), cmp_rssi);
    return count;
}

const char *net_auth_name(uint8_t auth)
{
    switch (auth) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_ENTERPRISE: return "WPA2-Ent";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "?";
    }
}
