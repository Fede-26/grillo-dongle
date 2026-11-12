#include "devcon.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "usbdev.h"

// In release la CDC appartiene solo al canale con l'host: niente log né
// comandi di debug, che finirebbero mescolati alle risposte di host.ask.
// Un'opzione Kconfig bool disattivata non è definita affatto: usarla come
// valore non compilava con sdkconfig.release.
#ifdef CONFIG_GRILLO_USB_DEBUG_CDC
#define CDC_CONSOLE 1
#else
#define CDC_CONSOLE 0
#endif

#define MAX_CMDS 32
// Anche le righe "@G<nonce>:<valore>" del canale host (valore fino a 256 caratteri)
#define LINE_LEN 320
#define MAX_ARGS 8

typedef struct {
    const char *name;
    const char *usage;
    devcon_cmd_fn fn;
} cmd_t;

static cmd_t s_cmds[MAX_CMDS];
static int s_ncmds;
static SemaphoreHandle_t s_wlock;
static devcon_line_filter_t s_filter;

void devcon_set_line_filter(devcon_line_filter_t filter)
{
    s_filter = filter;
}

void devcon_cdc_write(const char *buf, int len)
{
    if (len > 0) usbdev_cdc_write((const uint8_t *)buf, len);
}

void devcon_write(const char *buf, int len)
{
    if (len <= 0) return;
    // Un solo scrittore alla volta: altrimenti log e output dei comandi si
    // mescolerebbero a metà riga.
    if (s_wlock && xSemaphoreTake(s_wlock, pdMS_TO_TICKS(50)) != pdTRUE) return;
    if (usbdev_cdc_active()) {
        if (CDC_CONSOLE) usbdev_cdc_write((const uint8_t *)buf, len);
    } else if (usb_serial_jtag_is_connected()) {
        usb_serial_jtag_write_bytes(buf, len, pdMS_TO_TICKS(20));
    }
    if (s_wlock) xSemaphoreGive(s_wlock);
}

int devcon_printf(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    devcon_write(buf, n);
    return n;
}

// Sostituisce l'output dei log di ESP-IDF: scrive sul trasporto attivo
// invece che sullo stdout fisso della console di boot.
static int log_vprintf(const char *fmt, va_list ap)
{
    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    devcon_write(buf, n);
    return n;
}

void devcon_register(const char *name, const char *usage, devcon_cmd_fn fn)
{
    if (s_ncmds < MAX_CMDS) s_cmds[s_ncmds++] = (cmd_t){name, usage, fn};
}

static int cmd_help(int argc, char **argv)
{
    for (int i = 0; i < s_ncmds; i++) devcon_printf("  %-8s %s\n", s_cmds[i].name, s_cmds[i].usage);
    return 0;
}

static void execute(char *line)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    // Divide per spazi; le virgolette doppie raggruppano un argomento
    char *p = line;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '"') {
            argv[argc++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
        }
        if (*p) *p++ = 0;
    }
    if (argc == 0) return;
    int rc = -1;
    bool found = false;
    for (int i = 0; i < s_ncmds; i++) {
        if (strcmp(s_cmds[i].name, argv[0]) == 0) {
            found = true;
            rc = s_cmds[i].fn(argc, argv);
            break;
        }
    }
    if (!found) devcon_printf("unknown command '%s' (try help)\n", argv[0]);
    devcon_printf(">>> rc=%d\n", rc);
}

static size_t read_input(uint8_t *buf, size_t len)
{
    if (usbdev_cdc_active()) {
        size_t n = usbdev_cdc_read(buf, len);
        if (!n) vTaskDelay(pdMS_TO_TICKS(10));
        return n;
    }
    int n = usb_serial_jtag_read_bytes(buf, len, pdMS_TO_TICKS(10));
    return n > 0 ? n : 0;
}

static void devcon_task(void *arg)
{
    char line[LINE_LEN];
    int len = 0;
    uint8_t buf[64];
    for (;;) {
        size_t n = read_input(buf, sizeof(buf));
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r' || c == '\n') {
                line[len] = 0;
                // Letto una volta: set_line_filter(NULL) da un altro task tra
                // il controllo e la chiamata faceva saltare a NULL.
                devcon_line_filter_t filter = s_filter;
                if (len && !(filter && filter(line))) {
                    // Sulla CDC in release si accettano solo le righe del filtro
                    if (CDC_CONSOLE || !usbdev_cdc_active()) execute(line);
                }
                len = 0;
            } else if (len < LINE_LEN - 1) {
                line[len++] = c;
            }
        }
    }
}

void devcon_init(void)
{
    s_wlock = xSemaphoreCreateMutex();
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
    // Anche printf/stdout passa dal driver: senza, driver e VFS scriverebbero
    // in parallelo nella stessa FIFO.
    usb_serial_jtag_vfs_use_driver();
    esp_log_set_vprintf(log_vprintf);
    devcon_register("help", "list commands", cmd_help);
    xTaskCreate(devcon_task, "devcon", 6144, NULL, 3, NULL);
}
