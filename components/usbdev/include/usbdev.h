#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

// Profili USB del dongle. Il profilo viene scelto dall'app manager prima
// di avviare un'app (campo `usb` del descrittore) e riportato a NONE
// all'uscita. Ogni profilo ha un PID diverso, così Windows non riusa i
// driver associati a un'altra combinazione di interfacce.
typedef enum {
    USB_PROFILE_NONE, // TinyUSB spento: la PHY torna alla USB-Serial-JTAG (log e flash)
    USB_PROFILE_HID,  // tastiera (boot protocol) + mouse
    USB_PROFILE_MSC,  // SD esposta come disco
} usb_profile_t;

typedef enum {
    USBDEV_EV_MOUNTED,   // l'host ha configurato il dispositivo
    USBDEV_EV_UNMOUNTED,
    USBDEV_EV_SUSPENDED,
    USBDEV_EV_RESUMED,
    USBDEV_EV_MSC_EJECTED, // l'host ha espulso il disco: si può riprendere la SD
} usbdev_event_t;

typedef void (*usbdev_event_cb_t)(usbdev_event_t ev);

void usbdev_set_event_cb(usbdev_event_cb_t cb);

// Cambia profilo: smonta TinyUSB e lo reinstalla con i nuovi descrittori.
// Per MSC serve la scheda già sottratta alla VFS (storage_lend()).
esp_err_t usbdev_apply(usb_profile_t profile, sdmmc_card_t *msc_card);
usb_profile_t usbdev_profile(void);
const char *usbdev_profile_name(usb_profile_t p);
bool usbdev_mounted(void);

// Dopo USBDEV_EV_MSC_EJECTED: rende il disco "non presente" per l'host.
// Da chiamare da un task normale (monta la FAT lato firmware su /usbmsc).
void usbdev_msc_detach(void);

// --- HID (solo con USB_PROFILE_HID) ---

// Bit dei LED tastiera inviati dall'host (output report)
#define USBDEV_LED_NUMLOCK  0x01
#define USBDEV_LED_CAPSLOCK 0x02
#define USBDEV_LED_SCROLL   0x04

bool usbdev_hid_ready(void);
// Invia un report tastiera: modificatori + fino a 6 keycode (HID usage).
// Attende al massimo `timeout_ms` che l'endpoint sia libero.
bool usbdev_kbd_report(uint8_t modifiers, const uint8_t keys[6], uint32_t timeout_ms);
bool usbdev_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan, uint32_t timeout_ms);
uint8_t usbdev_kbd_leds(void);

// --- CDC di debug (solo con CONFIG_GRILLO_USB_DEBUG_CDC) ---
// Usate dalla console di debug: sicure da chiamare da qualsiasi task e
// anche quando la CDC non esiste (non fanno nulla).
bool usbdev_cdc_active(void);
size_t usbdev_cdc_read(uint8_t *buf, size_t len);
void usbdev_cdc_write(const uint8_t *buf, size_t len);
