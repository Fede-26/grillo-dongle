#include "usbdev.h"

#include <stdio.h>
#include <string.h>
#include "class/hid/hid_device.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"
#if CONFIG_GRILLO_USB_DEBUG_CDC
#include "tinyusb_cdc_acm.h"
#endif

static const char *TAG = "usbdev";

static usb_profile_t s_profile = USB_PROFILE_NONE;
static usbdev_event_cb_t s_cb;
static volatile bool s_mounted;
static volatile uint8_t s_leds;
static tinyusb_msc_storage_handle_t s_msc;
// La CDC di debug è usata dalla console (devcon) da altri task: il mutex
// evita di scriverci mentre viene distrutta durante un cambio profilo.
static SemaphoreHandle_t s_cdc_lock;
static bool s_cdc_active;

// ---------------------------------------------------------------------------
// Descrittori

#define VID 0x303A // Espressif
// Stesso schema dei PID degli esempi TinyUSB/esp_tinyusb: un bit per classe.
// Uso solo personale: non sono PID assegnati ufficialmente.
#define PID_BASE 0x4000
#define PID_CDC  0x01
#define PID_MSC  0x02
#define PID_HID  0x04

#if CONFIG_GRILLO_USB_DEBUG_CDC
#define HAS_CDC 1
#else
#define HAS_CDC 0
#endif

enum { STR_LANG, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_COUNT };

// Interfacce: prima quelle di classe, poi la CDC di debug in coda, così i
// numeri delle interfacce HID/MSC non cambiano tra build debug e release.
enum { ITF_KBD, ITF_MOUSE, ITF_HID_TOTAL };
enum { ITF_MSC, ITF_MSC_TOTAL };

#define EP_KBD    0x81
#define EP_MOUSE  0x82
#define EP_MSC_OUT 0x01
#define EP_MSC_IN  0x81
#define EP_CDC_NOTIF 0x83
#define EP_CDC_OUT   0x04
#define EP_CDC_IN    0x84

static const uint8_t s_kbd_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
static const uint8_t s_mouse_report[] = {TUD_HID_REPORT_DESC_MOUSE()};

#define CDC_ITF_LEN (HAS_CDC ? TUD_CDC_DESC_LEN : 0)

static const uint8_t s_cfg_hid[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_HID_TOTAL + 2 * HAS_CDC, 0,
                          TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + CDC_ITF_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_KBD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_kbd_report), EP_KBD, 8, 10),
    TUD_HID_DESCRIPTOR(ITF_MOUSE, 0, HID_ITF_PROTOCOL_MOUSE, sizeof(s_mouse_report), EP_MOUSE, 8, 10),
#if HAS_CDC
    TUD_CDC_DESCRIPTOR(ITF_HID_TOTAL, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
#endif
};

static const uint8_t s_cfg_msc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_MSC_TOTAL + 2 * HAS_CDC, 0, TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN + CDC_ITF_LEN,
                          0, 100),
    TUD_MSC_DESCRIPTOR(ITF_MSC, 0, EP_MSC_OUT, EP_MSC_IN, 64),
#if HAS_CDC
    TUD_CDC_DESCRIPTOR(ITF_MSC_TOTAL, STR_CDC, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
#endif
};

static tusb_desc_device_t s_dev = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    // Con la CDC serve l'IAD (Interface Association), quindi classe "misc"
    .bDeviceClass = HAS_CDC ? TUSB_CLASS_MISC : 0,
    .bDeviceSubClass = HAS_CDC ? MISC_SUBCLASS_COMMON : 0,
    .bDeviceProtocol = HAS_CDC ? MISC_PROTOCOL_IAD : 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = VID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_MANUF,
    .iProduct = STR_PRODUCT,
    .iSerialNumber = STR_SERIAL,
    .bNumConfigurations = 1,
};

static char s_serial[13];
static const char *s_strings[STR_COUNT] = {
    [STR_LANG] = (const char[]){0x09, 0x04}, // inglese (0x0409)
    [STR_MANUF] = "Grillo",
    [STR_CDC] = "Grillo Debug",
};

// ---------------------------------------------------------------------------
// Callback TinyUSB

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == ITF_KBD ? s_kbd_report : s_mouse_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buf,
                               uint16_t len)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buf,
                           uint16_t len)
{
    // L'unico output report è quello dei LED della tastiera: serve per
    // sapere se sull'host è attivo Caps Lock / Num Lock.
    if (instance == ITF_KBD && type == HID_REPORT_TYPE_OUTPUT && len >= 1) s_leds = buf[0];
}

static void on_tinyusb_event(tinyusb_event_t *event, void *arg)
{
    usbdev_event_t ev;
    switch (event->id) {
    case TINYUSB_EVENT_ATTACHED: s_mounted = true; ev = USBDEV_EV_MOUNTED; break;
    case TINYUSB_EVENT_DETACHED: s_mounted = false; ev = USBDEV_EV_UNMOUNTED; break;
    case TINYUSB_EVENT_SUSPENDED: ev = USBDEV_EV_SUSPENDED; break;
    case TINYUSB_EVENT_RESUMED: ev = USBDEV_EV_RESUMED; break;
    default: return;
    }
    if (s_cb) s_cb(ev);
}

static void on_msc_event(tinyusb_msc_storage_handle_t h, tinyusb_msc_event_t *e, void *arg)
{
    ESP_LOGI(TAG, "msc event %d (mount point %d)", e->id, e->mount_point);
}

// Hook deboli di TinyUSB che esp_tinyusb lascia liberi.

// Chiamata dopo ogni comando SCSI: la usiamo per diagnosi (log a livello
// debug) e per sapere quando l'host sta usando il disco.
void tud_msc_scsi_complete_cb(uint8_t lun, uint8_t const scsi_cmd[16])
{
    uint8_t op = scsi_cmd[0];
    // READ(10)/WRITE(10)/TEST UNIT READY sono continui: non li logghiamo
    if (op != 0x28 && op != 0x2A && op != 0x00) ESP_LOGD(TAG, "scsi 0x%02X", op);
    // START STOP UNIT con LoEj=1 e Start=0: l'host ha espulso il disco e ha
    // già scritto tutto. esp_tinyusb con auto_mount_off non reagisce, quindi
    // lo segnaliamo noi; l'app chiamerà usbdev_msc_detach().
    if (op == 0x1B && (scsi_cmd[4] & 0x03) == 0x02 && s_cb) s_cb(USBDEV_EV_MSC_EJECTED);
}

// ---------------------------------------------------------------------------

void usbdev_set_event_cb(usbdev_event_cb_t cb)
{
    s_cb = cb;
}

usb_profile_t usbdev_profile(void)
{
    return s_profile;
}

const char *usbdev_profile_name(usb_profile_t p)
{
    switch (p) {
    case USB_PROFILE_HID: return "HID";
    case USB_PROFILE_MSC: return "MSC";
    default: return "-";
    }
}

bool usbdev_mounted(void)
{
    return s_mounted;
}

// Riassegna la PHY interna alla USB-Serial-JTAG. usb_del_phy() (chiamata
// da tinyusb_driver_uninstall) disabilita i pad ma lascia il mux sull'OTG:
// senza questo la porta seriale/flash non riappare finché non si scollega.
static void restore_usj_phy(void)
{
    usb_serial_jtag_ll_phy_enable_external(false); // PHY interna -> USJ
    usb_serial_jtag_ll_phy_disable_pull_override();
    usb_serial_jtag_ll_phy_enable_pad(true);
}

static void teardown(void)
{
    if (s_profile == USB_PROFILE_NONE) return;
#if HAS_CDC
    xSemaphoreTake(s_cdc_lock, portMAX_DELAY);
    s_cdc_active = false;
    tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
    xSemaphoreGive(s_cdc_lock);
#endif
    if (s_msc) {
        tinyusb_msc_delete_storage(s_msc);
        tinyusb_msc_uninstall_driver();
        s_msc = NULL;
    }
    esp_err_t err = tinyusb_driver_uninstall();
    if (err != ESP_OK) ESP_LOGE(TAG, "uninstall: %s", esp_err_to_name(err));
    s_mounted = false;
    s_leds = 0;
    s_profile = USB_PROFILE_NONE;
    restore_usj_phy();
}

esp_err_t usbdev_apply(usb_profile_t profile, sdmmc_card_t *msc_card)
{
    if (!s_cdc_lock) s_cdc_lock = xSemaphoreCreateMutex();
    if (profile == s_profile) return ESP_OK;
    teardown();
    if (profile == USB_PROFILE_NONE) {
        ESP_LOGI(TAG, "profilo NONE");
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(profile != USB_PROFILE_MSC || msc_card, ESP_ERR_INVALID_ARG, TAG, "MSC senza scheda");

    if (!s_serial[0]) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
        s_strings[STR_SERIAL] = s_serial;
    }
    s_dev.idProduct = PID_BASE | (HAS_CDC ? PID_CDC : 0) | (profile == USB_PROFILE_HID ? PID_HID : PID_MSC);
    s_strings[STR_PRODUCT] = profile == USB_PROFILE_HID ? "Grillo HID" : "Grillo Disk";

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(on_tinyusb_event, NULL);
    cfg.descriptor.device = &s_dev;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STR_COUNT;
    cfg.descriptor.full_speed_config = profile == USB_PROFILE_HID ? s_cfg_hid : s_cfg_msc;

    if (profile == USB_PROFILE_MSC) {
        // Lo storage va creato prima di installare il driver, così all'enumerazione
        // il disco è già presente. auto_mount_off: niente rimontaggio automatico
        // sulla VFS al distacco, il possesso della SD lo gestisce storage.c.
        tinyusb_msc_driver_config_t mcfg = {
            .user_flags.auto_mount_off = 1,
            .callback = on_msc_event,
        };
        ESP_RETURN_ON_ERROR(tinyusb_msc_install_driver(&mcfg), TAG, "msc driver");
        tinyusb_msc_storage_config_t scfg = {
            .medium.card = msc_card,
            // Path diverso da /sd: è dove esp_tinyusb monta la scheda dopo
            // l'espulsione, e non deve sovrapporsi alla VFS di storage.c.
            .fat_fs.base_path = "/usbmsc",
            // Mai formattare: una scheda non FAT resta intatta.
            .fat_fs.do_not_format = true,
            .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
        };
        esp_err_t err = tinyusb_msc_new_storage_sdmmc(&scfg, &s_msc);
        if (err != ESP_OK) {
            tinyusb_msc_uninstall_driver();
            ESP_LOGE(TAG, "msc storage: %s", esp_err_to_name(err));
            return err;
        }
    }

    esp_err_t err = tinyusb_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "install: %s", esp_err_to_name(err));
        if (s_msc) {
            tinyusb_msc_delete_storage(s_msc);
            tinyusb_msc_uninstall_driver();
            s_msc = NULL;
        }
        return err;
    }
    s_profile = profile;

#if HAS_CDC
    // In build debug la console (log e comandi, vedi devcon) passa sulla
    // CDC: la USB-Serial-JTAG non è raggiungibile finché TinyUSB è attivo.
    tinyusb_config_cdcacm_t acm = {.cdc_port = TINYUSB_CDC_ACM_0};
    if (tinyusb_cdcacm_init(&acm) == ESP_OK) {
        xSemaphoreTake(s_cdc_lock, portMAX_DELAY);
        s_cdc_active = true;
        xSemaphoreGive(s_cdc_lock);
    }
#endif
    ESP_LOGI(TAG, "profilo %s (PID 0x%04X)", usbdev_profile_name(profile), s_dev.idProduct);
    return ESP_OK;
}

void usbdev_msc_detach(void)
{
    // Lo storage passa "all'app": l'host vede il supporto come non presente
    // e non lo rimonta (macOS ci riprova un secondo dopo l'espulsione).
    if (s_msc) tinyusb_msc_set_storage_mount_point(s_msc, TINYUSB_MSC_STORAGE_MOUNT_APP);
}

// ---------------------------------------------------------------------------
// HID

bool usbdev_hid_ready(void)
{
    return s_profile == USB_PROFILE_HID && s_mounted && tud_hid_n_ready(ITF_KBD);
}

static bool wait_ready(uint8_t itf, uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (!tud_hid_n_ready(itf)) {
        if (s_profile != USB_PROFILE_HID || waited >= timeout_ms) return false;
        // Se l'host ha sospeso il bus, lo risvegliamo (es. PC in standby
        // durante un'installazione lunga).
        if (tud_suspended()) tud_remote_wakeup();
        vTaskDelay(1);
        waited++;
    }
    return true;
}

bool usbdev_kbd_report(uint8_t modifiers, const uint8_t keys[6], uint32_t timeout_ms)
{
    if (!wait_ready(ITF_KBD, timeout_ms)) return false;
    return tud_hid_n_keyboard_report(ITF_KBD, 0, modifiers, keys);
}

bool usbdev_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan, uint32_t timeout_ms)
{
    if (!wait_ready(ITF_MOUSE, timeout_ms)) return false;
    return tud_hid_n_mouse_report(ITF_MOUSE, 0, buttons, x, y, wheel, pan);
}

uint8_t usbdev_kbd_leds(void)
{
    return s_leds;
}

// ---------------------------------------------------------------------------
// CDC di debug

bool usbdev_cdc_active(void)
{
    return s_cdc_active;
}

size_t usbdev_cdc_read(uint8_t *buf, size_t len)
{
    size_t n = 0;
#if HAS_CDC
    if (!s_cdc_lock || xSemaphoreTake(s_cdc_lock, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    if (s_cdc_active) tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, len, &n);
    xSemaphoreGive(s_cdc_lock);
#endif
    return n;
}

void usbdev_cdc_write(const uint8_t *buf, size_t len)
{
#if HAS_CDC
    // Timeout brevi: chi scrive può essere un log emesso dal task di
    // TinyUSB stesso, che non deve mai restare bloccato qui.
    if (!s_cdc_lock || xSemaphoreTake(s_cdc_lock, pdMS_TO_TICKS(10)) != pdTRUE) return;
    int stalls = 0;
    while (len && s_cdc_active && tud_cdc_n_connected(0)) {
        size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        if (n == 0) {
            // Buffer pieno: diamo tempo all'host di leggere (fino a ~100 ms),
            // poi lasciamo perdere il resto piuttosto che bloccare.
            if (++stalls > 100) break;
            vTaskDelay(1);
            continue;
        }
        stalls = 0;
        buf += n;
        len -= n;
    }
    xSemaphoreGive(s_cdc_lock);
#endif
}
