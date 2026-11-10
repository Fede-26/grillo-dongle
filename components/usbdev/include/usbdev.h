#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

// Interfacce USB che un profilo può combinare. Un profilo è la maschera
// delle interfacce: un'app nuova dichiara quella che le serve senza
// aggiungere casi qui. Ogni combinazione ha un PID diverso, così Windows non
// riusa i driver associati a un'altra combinazione di interfacce.
//
// Il disco è sempre la SD, in uno dei due modi (mai entrambi):
// - USB_F_DISK_RW: l'host la usa in lettura e scrittura e il firmware non
//   la vede più (storage_lend());
// - USB_F_DISK_RO: l'host la vede in sola lettura e il firmware la tiene
//   montata (storage_share_ro()).
// Chi passa la SD all'host è appmgr_usb_apply(), non le app.
enum {
    USB_F_KBD = 1 << 0,     // tastiera (boot protocol)
    USB_F_MOUSE = 1 << 1,
    USB_F_DISK_RW = 1 << 2,
    USB_F_DISK_RO = 1 << 3,
    USB_F_CDC = 1 << 4,     // seriale: console di debug e canale di ritorno host.*
};
#define USB_F_DISK (USB_F_DISK_RW | USB_F_DISK_RO)

typedef uint32_t usb_profile_t;

// Profili usati dalle app incluse. USB_PROFILE_NONE spegne TinyUSB: la PHY
// torna alla USB-Serial-JTAG (log e flash). Nei profili con la tastiera la
// CDC è funzionale (host.ask), non solo di debug: c'è sempre.
#define USB_PROFILE_NONE     0
#define USB_PROFILE_HID      (USB_F_KBD | USB_F_MOUSE | USB_F_CDC)
// Niente mouse: gli endpoint IN dell'OTG non bastano per tastiera + mouse +
// disco + CDC (vedi usbdev_apply).
#define USB_PROFILE_HID_DISK (USB_F_KBD | USB_F_DISK_RO | USB_F_CDC)
#define USB_PROFILE_MSC      USB_F_DISK_RW

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
// Con il disco serve la scheda già preparata nel modo giusto (vedi
// USB_F_DISK_RW/RO): le app passano da appmgr_usb_apply(), che lo fa.
// Con CONFIG_GRILLO_USB_DEBUG_CDC la CDC si aggiunge a ogni profilo.
// ESP_ERR_NOT_SUPPORTED se la combinazione non entra negli endpoint.
esp_err_t usbdev_apply(usb_profile_t profile, sdmmc_card_t *msc_card);
usb_profile_t usbdev_profile(void);
// Nome breve del profilo per la console ("HID", "HID+DISK", ...).
const char *usbdev_profile_name(usb_profile_t p);
bool usbdev_mounted(void);
bool usbdev_has_mouse(void);
// Numero di serie USB (MAC in esadecimale): identifica la porta seriale
// del dongle sull'host. Valido dopo il primo usbdev_apply().
const char *usbdev_serial(void);

// Dopo USBDEV_EV_MSC_EJECTED: rende il disco "non presente" per l'host.
// Da chiamare da un task normale (monta la FAT lato firmware su /usbmsc).
void usbdev_msc_detach(void);

// --- HID (solo con USB_F_KBD / USB_F_MOUSE) ---

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

// --- Seriale CDC (con USB_F_CDC, o sempre con CONFIG_GRILLO_USB_DEBUG_CDC) ---
// Sicure da chiamare da qualsiasi task e anche quando la CDC non esiste
// (non fanno nulla).
bool usbdev_cdc_active(void);
size_t usbdev_cdc_read(uint8_t *buf, size_t len);
void usbdev_cdc_write(const uint8_t *buf, size_t len);
