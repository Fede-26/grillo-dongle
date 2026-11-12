#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

// USB interfaces a profile can combine. A profile is the mask of the
// interfaces: a new app declares the one it needs without adding cases here.
// Each combination has a different PID, so Windows does not reuse the
// drivers associated with another combination of interfaces.
//
// The disk is always the SD, in one of two modes (never both):
// - USB_F_DISK_RW: the host uses it read-write and the firmware no longer
//   sees it (storage_lend());
// - USB_F_DISK_RO: the host sees it read-only and the firmware keeps it
//   mounted (storage_share_ro()).
// The one that hands the SD to the host is app_usb_apply(), not the apps.
enum {
    USB_F_KBD = 1 << 0,     // keyboard (boot protocol)
    USB_F_MOUSE = 1 << 1,
    USB_F_DISK_RW = 1 << 2,
    USB_F_DISK_RO = 1 << 3,
    USB_F_CDC = 1 << 4,     // serial: debug console and host.* back channel
};
#define USB_F_DISK (USB_F_DISK_RW | USB_F_DISK_RO)

typedef uint32_t usb_profile_t;

// Profiles used by the bundled apps. USB_PROFILE_NONE turns TinyUSB off: the
// PHY goes back to the USB-Serial-JTAG (logs and flashing). In the profiles
// with the keyboard the CDC is functional (host.ask), not just for debugging:
// it is always there.
#define USB_PROFILE_NONE     0
#define USB_PROFILE_HID      (USB_F_KBD | USB_F_MOUSE | USB_F_CDC)
// No mouse: the OTG's IN endpoints are not enough for keyboard + mouse +
// disk + CDC (see usbdev_apply).
#define USB_PROFILE_HID_DISK (USB_F_KBD | USB_F_DISK_RO | USB_F_CDC)
#define USB_PROFILE_MSC      USB_F_DISK_RW

typedef enum {
    USBDEV_EV_MOUNTED,   // the host configured the device
    USBDEV_EV_UNMOUNTED,
    USBDEV_EV_SUSPENDED,
    USBDEV_EV_RESUMED,
    USBDEV_EV_MSC_EJECTED, // the host ejected the disk: the SD can be taken back
} usbdev_event_t;

typedef void (*usbdev_event_cb_t)(usbdev_event_t ev);

void usbdev_set_event_cb(usbdev_event_cb_t cb);

// Changes profile: tears TinyUSB down and reinstalls it with the new
// descriptors. With the disk the card must already be prepared in the right
// mode (see USB_F_DISK_RW/RO): apps go through app_usb_apply(), which
// does it. With CONFIG_GRILLO_USB_DEBUG_CDC the CDC is added to every profile.
// ESP_ERR_NOT_SUPPORTED if the combination does not fit in the endpoints.
esp_err_t usbdev_apply(usb_profile_t profile, sdmmc_card_t *msc_card);
usb_profile_t usbdev_profile(void);
// Short profile name for the console ("HID", "HID+DISK", ...).
const char *usbdev_profile_name(usb_profile_t p);
bool usbdev_mounted(void);
bool usbdev_has_mouse(void);
// USB serial number (MAC in hex): identifies the dongle's serial port on the
// host. Valid after the first usbdev_apply().
const char *usbdev_serial(void);

// After USBDEV_EV_MSC_EJECTED: makes the disk "not present" for the host.
// Call from a normal task (it mounts the FAT on the firmware side on /usbmsc).
void usbdev_msc_detach(void);

// --- HID (only with USB_F_KBD / USB_F_MOUSE) ---

// Keyboard LED bits sent by the host (output report)
#define USBDEV_LED_NUMLOCK  0x01
#define USBDEV_LED_CAPSLOCK 0x02
#define USBDEV_LED_SCROLL   0x04

bool usbdev_hid_ready(void);
// Sends a keyboard report: modifiers + up to 6 keycodes (HID usage).
// Waits at most `timeout_ms` for the endpoint to be free.
bool usbdev_kbd_report(uint8_t modifiers, const uint8_t keys[6], uint32_t timeout_ms);
bool usbdev_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan, uint32_t timeout_ms);
uint8_t usbdev_kbd_leds(void);

// --- CDC serial (with USB_F_CDC, or always with CONFIG_GRILLO_USB_DEBUG_CDC) ---
// Safe to call from any task and also when the CDC does not exist
// (they do nothing).
bool usbdev_cdc_active(void);
size_t usbdev_cdc_read(uint8_t *buf, size_t len);
void usbdev_cdc_write(const uint8_t *buf, size_t len);
