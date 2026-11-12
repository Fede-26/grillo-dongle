#include "usbdev.h"

#include <stdio.h>
#include <string.h>
#include "class/hid/hid_device.h"
#include "device/usbd_pvt.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/usb_serial_jtag_ll.h"
#include "sdkconfig.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"
#include "tusb.h"

static const char *TAG = "usbdev";

// A disabled Kconfig bool option is not defined at all: using it as a value
// did not compile with sdkconfig.release.
#ifdef CONFIG_GRILLO_USB_DEBUG_CDC
#define DEBUG_CDC 1
#else
#define DEBUG_CDC 0
#endif

static usb_profile_t s_profile = USB_PROFILE_NONE;
static uint32_t s_features;
static usbdev_event_cb_t s_cb;
static volatile bool s_mounted;
static volatile uint8_t s_leds;
static volatile bool s_msc_readonly;
static tinyusb_msc_storage_handle_t s_msc;
// The CDC is used by other tasks (console, host link): the mutex prevents
// writing to it while it is being destroyed during a profile change.
static SemaphoreHandle_t s_cdc_lock;
static bool s_cdc_active;
static SemaphoreHandle_t s_parked, s_unpark;

// Index of the keyboard and mouse HID instance (-1 if absent). TinyUSB
// numbers the HID instances in the order they appear in the descriptor.
static int s_hid_kbd = -1, s_hid_mouse = -1;

// ---------------------------------------------------------------------------
// Descriptors

#define VID 0x303A // Espressif
// Same scheme as the PIDs of the TinyUSB/esp_tinyusb examples: one bit per
// class. Personal use only: these are not officially assigned PIDs.
#define PID_BASE 0x4000
#define PID_CDC  0x01
#define PID_MSC  0x02
#define PID_HID  0x04
#define PID_NO_MOUSE 0x08 // composite without mouse: different interfaces, different PID

enum { STR_LANG, STR_MANUF, STR_PRODUCT, STR_SERIAL, STR_CDC, STR_COUNT };

static const uint8_t s_kbd_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
static const uint8_t s_mouse_report[] = {TUD_HID_REPORT_DESC_MOUSE()};

// Room for the largest configuration descriptor (all interfaces)
static uint8_t s_cfg[TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN + TUD_CDC_DESC_LEN];

static tusb_desc_device_t s_dev = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
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
    [STR_LANG] = (const char[]){0x09, 0x04}, // English (0x0409)
    [STR_MANUF] = "Grillo",
    [STR_CDC] = "Grillo Serial",
};

// Builds the configuration descriptor. Interfaces and endpoints are assigned
// in sequence; the CDC goes last so the other interfaces keep the same
// number with or without the CDC.
static size_t build_config(uint32_t f)
{
    uint8_t itf = 0, ep = 1;
    size_t len = TUD_CONFIG_DESC_LEN;
    uint8_t *p = s_cfg + TUD_CONFIG_DESC_LEN;
    int hid_instance = 0;
    s_hid_kbd = s_hid_mouse = -1;

    if (f & USB_F_KBD) {
        // bInterval 1 ms: up to 1000 reports/s, typing is not limited by
        // the host's polling.
        uint8_t d[] = {TUD_HID_DESCRIPTOR(itf, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(s_kbd_report), 0x80 | ep, 8, 1)};
        memcpy(p, d, sizeof(d));
        p += sizeof(d), len += sizeof(d), itf++, ep++;
        s_hid_kbd = hid_instance++;
    }
    if (f & USB_F_MOUSE) {
        uint8_t d[] = {TUD_HID_DESCRIPTOR(itf, 0, HID_ITF_PROTOCOL_MOUSE, sizeof(s_mouse_report), 0x80 | ep, 8, 1)};
        memcpy(p, d, sizeof(d));
        p += sizeof(d), len += sizeof(d), itf++, ep++;
        s_hid_mouse = hid_instance++;
    }
    if (f & USB_F_DISK) {
        uint8_t d[] = {TUD_MSC_DESCRIPTOR(itf, 0, ep, 0x80 | ep, 64)};
        memcpy(p, d, sizeof(d));
        p += sizeof(d), len += sizeof(d), itf++, ep++;
    }
    if (f & USB_F_CDC) {
        uint8_t d[] = {TUD_CDC_DESCRIPTOR(itf, STR_CDC, 0x80 | ep, 8, ep + 1, 0x80 | (ep + 1), 64)};
        memcpy(p, d, sizeof(d));
        p += sizeof(d), len += sizeof(d), itf += 2, ep += 2;
    }
    uint8_t head[] = {TUD_CONFIG_DESCRIPTOR(1, itf, 0, len, (f & USB_F_KBD) ? TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP : 0, 100)};
    memcpy(s_cfg, head, sizeof(head));
    return len;
}

// ---------------------------------------------------------------------------
// TinyUSB callbacks

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance == s_hid_kbd ? s_kbd_report : s_mouse_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t *buf,
                               uint16_t len)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type, uint8_t const *buf,
                           uint16_t len)
{
    // The only output report is the keyboard LEDs one: it tells whether
    // Caps Lock / Num Lock is active on the host.
    if (instance == s_hid_kbd && type == HID_REPORT_TYPE_OUTPUT && len >= 1) s_leds = buf[0];
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
    ESP_LOGD(TAG, "msc event %d (mount point %d)", e->id, e->mount_point);
}

// TinyUSB weak hooks that esp_tinyusb leaves free.

// Called after every SCSI command: we use it for diagnostics (debug-level
// logs) and to detect the eject.
void tud_msc_scsi_complete_cb(uint8_t lun, uint8_t const scsi_cmd[16])
{
    uint8_t op = scsi_cmd[0];
    // READ(10)/WRITE(10)/TEST UNIT READY are continuous: we do not log them
    if (op != 0x28 && op != 0x2A && op != 0x00) ESP_LOGD(TAG, "scsi 0x%02X", op);
    // START STOP UNIT with LoEj=1 and Start=0: the host ejected the disk and
    // has already written everything. esp_tinyusb with auto_mount_off does not
    // react, so we signal it ourselves; the app will call usbdev_msc_detach().
    if (op == 0x1B && (scsi_cmd[4] & 0x03) == 0x02 && s_cb) s_cb(USBDEV_EV_MSC_EJECTED);
}

// Read-only disk for the host (MODE SENSE with write-protect): needed when
// the firmware keeps the SD mounted in parallel.
bool tud_msc_is_writable_cb(uint8_t lun)
{
    return !s_msc_readonly;
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
    static const struct {
        usb_profile_t p;
        const char *name;
    } names[] = {
        {USB_PROFILE_NONE, "-"},
        {USB_PROFILE_HID, "HID"},
        {USB_PROFILE_HID_DISK, "HID+DISK"},
        {USB_PROFILE_MSC, "MSC"},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (names[i].p == p) return names[i].name;
    }
    return "custom";
}

bool usbdev_mounted(void)
{
    return s_mounted;
}

bool usbdev_has_mouse(void)
{
    return s_hid_mouse >= 0 && s_profile != USB_PROFILE_NONE;
}

// Reassigns the internal PHY to the USB-Serial-JTAG. usb_del_phy() (called
// by tinyusb_driver_uninstall) disables the pads but leaves the mux on the
// OTG: without this the serial/flash port does not reappear until unplugged.
static void restore_usj_phy(void)
{
    usb_serial_jtag_ll_phy_enable_external(false); // internal PHY -> USJ
    usb_serial_jtag_ll_phy_disable_pull_override();
    usb_serial_jtag_ll_phy_enable_pad(true);
}

// tinyusb_driver_uninstall deletes the TinyUSB task from outside
// (vTaskDelete) at an arbitrary point. If at that moment the task was serving
// a disk read, the SDMMC controller mutex stayed taken forever and the
// firmware's FAT hung on the first read: seen on 2026-11-10 with disk.detach
// while the Mac (Spotlight) was reading the disk.
// This function is run by the task itself between one event and the next,
// where it holds no resources, and stops it there until it is unblocked or
// deleted. Semaphores and not vTaskSuspend: an unblock requested before the
// task has stopped is not lost.
static void park_tinyusb(void *arg)
{
    xSemaphoreGive(s_parked);
    xSemaphoreTake(s_unpark, portMAX_DELAY);
}

static void park(void)
{
    usbd_defer_func(park_tinyusb, NULL, false);
    if (xSemaphoreTake(s_parked, pdMS_TO_TICKS(1000)) != pdTRUE) ESP_LOGW(TAG, "TinyUSB task not stopped");
}

static void teardown(void)
{
    if (s_profile == USB_PROFILE_NONE) return;
    // Signals left over from a previous teardown in which the task did not
    // stop in time: they would make a task look parked when it is not.
    xSemaphoreTake(s_parked, 0);
    xSemaphoreTake(s_unpark, 0);
    // Once we detach from the bus the host stops sending commands; the task
    // completes those already received before reaching the parking point.
    tud_disconnect();
    park();
    if (s_msc) {
        // With a copy in progress esp_tinyusb refuses to delete the storage
        // while deferred writes remain, which the TinyUSB task executes: it is
        // unblocked until it has written them (the queue is FIFO, so it runs
        // them before the next parking). Without this wait the MSC driver
        // stayed allocated and every later profile with the disk failed until
        // the reboot.
        esp_err_t e = ESP_FAIL;
        for (int i = 0; i < 200; i++) {
            e = tinyusb_msc_delete_storage(s_msc);
            if (e != ESP_ERR_INVALID_STATE) break;
            xSemaphoreGive(s_unpark);
            vTaskDelay(pdMS_TO_TICKS(10));
            park();
        }
        if (e == ESP_OK) e = tinyusb_msc_uninstall_driver();
        if (e != ESP_OK) ESP_LOGE(TAG, "msc teardown: %s", esp_err_to_name(e));
        s_msc = NULL;
    }
    if (s_features & USB_F_CDC) {
        xSemaphoreTake(s_cdc_lock, portMAX_DELAY);
        s_cdc_active = false;
        tinyusb_cdcacm_deinit(TINYUSB_CDC_ACM_0);
        xSemaphoreGive(s_cdc_lock);
    }
    esp_err_t err = tinyusb_driver_uninstall();
    if (err != ESP_OK) ESP_LOGE(TAG, "uninstall: %s", esp_err_to_name(err));
    s_mounted = false;
    s_leds = 0;
    s_features = 0;
    s_hid_kbd = s_hid_mouse = -1;
    s_profile = USB_PROFILE_NONE;
    restore_usj_phy();
}

// IN endpoints used, excluding EP0: one each for keyboard, mouse and disk,
// two for the CDC (notifications and data).
static int in_endpoints(uint32_t f)
{
    return !!(f & USB_F_KBD) + !!(f & USB_F_MOUSE) + !!(f & USB_F_DISK) + ((f & USB_F_CDC) ? 2 : 0);
}

// With keyboard + mouse + disk + CDC (5) the installation succeeds but the
// Mac enumerates only the HID and loses disk and serial (tested on the device
// on 2026-11-09); with 4 (keyboard + disk + CDC) it works. The limit is the
// measured one: better to refuse the profile than to enumerate it halfway.
#define MAX_IN_ENDPOINTS 4

static esp_err_t msc_create(sdmmc_card_t *card)
{
    // The storage must be created before installing the driver, so the disk is
    // already present at enumeration. auto_mount_off: no automatic remount on
    // the VFS on detach, storage.c manages ownership of the SD.
    tinyusb_msc_driver_config_t mcfg = {
        .user_flags.auto_mount_off = 1,
        .callback = on_msc_event,
    };
    ESP_RETURN_ON_ERROR(tinyusb_msc_install_driver(&mcfg), TAG, "msc driver");
    tinyusb_msc_storage_config_t scfg = {
        .medium.card = card,
        // Path other than /sd: it is where esp_tinyusb mounts the card after
        // the eject, and it must not overlap storage.c's VFS.
        .fat_fs.base_path = "/usbmsc",
        // Never format: a non-FAT card stays intact.
        .fat_fs.do_not_format = true,
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    esp_err_t err = tinyusb_msc_new_storage_sdmmc(&scfg, &s_msc);
    if (err != ESP_OK) {
        tinyusb_msc_uninstall_driver();
        ESP_LOGE(TAG, "msc storage: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t usbdev_apply(usb_profile_t profile, sdmmc_card_t *msc_card)
{
    if (!s_cdc_lock) s_cdc_lock = xSemaphoreCreateMutex();
    if (!s_parked) {
        s_parked = xSemaphoreCreateBinary();
        s_unpark = xSemaphoreCreateBinary();
    }
    if (profile == s_profile) return ESP_OK;
    teardown();
    if (profile == USB_PROFILE_NONE) {
        ESP_LOGI(TAG, "profile NONE");
        return ESP_OK;
    }
    // The debug serial is added to every profile; the active profile
    // (s_profile) does not include it, so the comparison with the requested one
    // stays exact.
    uint32_t f = profile | (DEBUG_CDC ? USB_F_CDC : 0);
    ESP_RETURN_ON_FALSE((f & USB_F_DISK) != USB_F_DISK, ESP_ERR_INVALID_ARG, TAG, "disk RW and RO together");
    ESP_RETURN_ON_FALSE(in_endpoints(f) <= MAX_IN_ENDPOINTS, ESP_ERR_NOT_SUPPORTED, TAG, "too many IN endpoints (0x%x)",
                        (unsigned)f);
    ESP_RETURN_ON_FALSE(!(f & USB_F_DISK) || msc_card, ESP_ERR_INVALID_ARG, TAG, "disk without a card");

    if (!s_serial[0]) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                 mac[5]);
        s_strings[STR_SERIAL] = s_serial;
    }
    s_dev.idProduct = PID_BASE | ((f & USB_F_CDC) ? PID_CDC : 0) | ((f & USB_F_DISK) ? PID_MSC : 0) |
                      ((f & USB_F_KBD) ? PID_HID : 0) | ((f & USB_F_KBD) && !(f & USB_F_MOUSE) ? PID_NO_MOUSE : 0);
    // With the CDC the IAD (Interface Association) is needed, hence class "misc"
    bool iad = f & USB_F_CDC;
    s_dev.bDeviceClass = iad ? TUSB_CLASS_MISC : 0;
    s_dev.bDeviceSubClass = iad ? MISC_SUBCLASS_COMMON : 0;
    s_dev.bDeviceProtocol = iad ? MISC_PROTOCOL_IAD : 0;
    s_strings[STR_PRODUCT] = (f & USB_F_KBD) ? "Grillo HID" : "Grillo Disk";
    build_config(f);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG(on_tinyusb_event, NULL);
    cfg.descriptor.device = &s_dev;
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = STR_COUNT;
    cfg.descriptor.full_speed_config = s_cfg;

    // When read-only the SD also stays mounted by the firmware
    s_msc_readonly = f & USB_F_DISK_RO;
    if (f & USB_F_DISK) ESP_RETURN_ON_ERROR(msc_create(msc_card), TAG, "msc");

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
    s_features = f;

    if (f & USB_F_CDC) {
        tinyusb_config_cdcacm_t acm = {.cdc_port = TINYUSB_CDC_ACM_0};
        if (tinyusb_cdcacm_init(&acm) == ESP_OK) {
            xSemaphoreTake(s_cdc_lock, portMAX_DELAY);
            s_cdc_active = true;
            xSemaphoreGive(s_cdc_lock);
        }
    }
    ESP_LOGI(TAG, "profile %s (PID 0x%04X)", usbdev_profile_name(profile), s_dev.idProduct);
    return ESP_OK;
}

void usbdev_msc_detach(void)
{
    // The storage goes "to the app": the host sees the medium as not present
    // and does not remount it (macOS retries a second after the eject).
    if (s_msc) tinyusb_msc_set_storage_mount_point(s_msc, TINYUSB_MSC_STORAGE_MOUNT_APP);
}

const char *usbdev_serial(void)
{
    return s_serial;
}

// ---------------------------------------------------------------------------
// HID

bool usbdev_hid_ready(void)
{
    return s_hid_kbd >= 0 && s_mounted && tud_hid_n_ready(s_hid_kbd);
}

static bool wait_ready(int instance, uint32_t timeout_ms)
{
    if (instance < 0) return false;
    uint32_t waited = 0;
    while (!tud_hid_n_ready(instance)) {
        if (s_profile == USB_PROFILE_NONE || waited >= timeout_ms) return false;
        // If the host suspended the bus, we wake it up (e.g. PC in standby
        // during a long installation).
        if (tud_suspended()) tud_remote_wakeup();
        vTaskDelay(1);
        waited++;
    }
    return true;
}

bool usbdev_kbd_report(uint8_t modifiers, const uint8_t keys[6], uint32_t timeout_ms)
{
    if (!wait_ready(s_hid_kbd, timeout_ms)) return false;
    return tud_hid_n_keyboard_report(s_hid_kbd, 0, modifiers, keys);
}

bool usbdev_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan, uint32_t timeout_ms)
{
    if (!wait_ready(s_hid_mouse, timeout_ms)) return false;
    return tud_hid_n_mouse_report(s_hid_mouse, 0, buttons, x, y, wheel, pan);
}

uint8_t usbdev_kbd_leds(void)
{
    return s_leds;
}

// ---------------------------------------------------------------------------
// CDC

bool usbdev_cdc_active(void)
{
    return s_cdc_active;
}

size_t usbdev_cdc_read(uint8_t *buf, size_t len)
{
    size_t n = 0;
    if (!s_cdc_lock || xSemaphoreTake(s_cdc_lock, pdMS_TO_TICKS(10)) != pdTRUE) return 0;
    if (s_cdc_active) tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, len, &n);
    xSemaphoreGive(s_cdc_lock);
    return n;
}

void usbdev_cdc_write(const uint8_t *buf, size_t len)
{
    // Short timeouts: the writer may be a log emitted by the TinyUSB task
    // itself, which must never get stuck here.
    if (!s_cdc_lock || xSemaphoreTake(s_cdc_lock, pdMS_TO_TICKS(10)) != pdTRUE) return;
    int stalls = 0;
    while (len && s_cdc_active && tud_cdc_n_connected(0)) {
        size_t n = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, len);
        tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
        if (n == 0) {
            // Buffer full: give the host time to read (up to ~100 ms), then
            // drop the rest rather than block.
            if (++stalls > 100) break;
            vTaskDelay(1);
            continue;
        }
        stalls = 0;
        buf += n;
        len -= n;
    }
    xSemaphoreGive(s_cdc_lock);
}
