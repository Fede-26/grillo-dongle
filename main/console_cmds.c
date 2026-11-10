// Comandi di sistema della console di debug (vedi components/devcon).

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "appmgr.h"
#include "config.h"
#include "console_cmds.h"
#include "devcon.h"
#include "display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "mbedtls/base64.h"
#include "storage.h"
#include "ui.h"
#include "usbdev.h"

static int cmd_state(int argc, char **argv)
{
    const char *cur = appmgr_current_id();
    devcon_printf("app      %s\n", cur ? cur : "-");
    devcon_printf("usb      %s mounted=%d leds=0x%02x\n", usbdev_profile_name(usbdev_profile()), usbdev_mounted(),
                  usbdev_kbd_leds());
    devcon_printf("sd       %s\n", storage_state_name(storage_state()));
    devcon_printf("heap     free=%u min=%u largest=%u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    const char *err = config_error();
    devcon_printf("config   %s\n", err ? err : "ok");
    return 0;
}

static int cmd_apps(int argc, char **argv)
{
    for (size_t i = 0; i < appmgr_count(); i++) {
        const app_desc_t *a = appmgr_get(i);
        const char *why = appmgr_unavailable(a);
        devcon_printf("  %-10s %-12s %s\n", a->id, a->name, why ? why : "");
    }
    return 0;
}

static int cmd_launch(int argc, char **argv)
{
    if (argc < 2) {
        devcon_printf("usage: launch <id> [arg]\n");
        return 1;
    }
    const app_desc_t *a = appmgr_find(argv[1]);
    if (!a) {
        devcon_printf("unknown app\n");
        return 1;
    }
    appmgr_launch(a, argc > 2 ? argv[2] : NULL);
    appmgr_request_exit(); // l'app corrente (anche il launcher) cede il posto
    return 0;
}

static int cmd_exit(int argc, char **argv)
{
    appmgr_request_exit();
    return 0;
}

static int cmd_btn(int argc, char **argv)
{
    static const struct {
        const char *name;
        gesture_t g;
    } map[] = {
        {"click", GESTURE_CLICK}, {"double", GESTURE_DOUBLE}, {"long", GESTURE_LONG}, {"hold", GESTURE_HOLD_EXIT},
    };
    for (size_t i = 0; argc > 1 && i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcmp(argv[1], map[i].name) == 0) {
            appmgr_inject(map[i].g, 0);
            return 0;
        }
    }
    devcon_printf("usage: btn click|double|long|hold\n");
    return 1;
}

// Screenshot: framebuffer RGB565 (byte-swapped) in base64, righe da 76 caratteri.
static int cmd_shot(int argc, char **argv)
{
    const size_t total = DISPLAY_W * DISPLAY_H * 2;
    ui_lock();
    const uint8_t *fb = (const uint8_t *)display_fb();
    devcon_printf("SHOT %d %d rgb565be\n", DISPLAY_W, DISPLAY_H);
    char out[80];
    for (size_t off = 0; off < total; off += 57) {
        size_t n = total - off < 57 ? total - off : 57, olen = 0;
        mbedtls_base64_encode((unsigned char *)out, sizeof(out) - 1, &olen, fb + off, n);
        out[olen++] = '\n';
        devcon_write(out, olen);
    }
    ui_unlock();
    devcon_printf("END\n");
    return 0;
}

static int cmd_ls(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : STORAGE_ROOT;
    DIR *d = opendir(path);
    if (!d) {
        devcon_printf("cannot open %s\n", path);
        return 1;
    }
    struct dirent *e;
    while ((e = readdir(d))) {
        char full[300];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        bool ok = stat(full, &st) == 0;
        devcon_printf("  %s%-32s %ld\n", e->d_type == DT_DIR ? "d " : "  ", e->d_name, ok ? (long)st.st_size : -1L);
    }
    closedir(d);
    return 0;
}

static int cmd_cat(int argc, char **argv)
{
    if (argc < 2) return 1;
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        devcon_printf("cannot open %s\n", argv[1]);
        return 1;
    }
    char buf[128];
    size_t n, total = 0;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && total < 16384) {
        devcon_write(buf, n);
        total += n;
    }
    fclose(f);
    devcon_printf("\n");
    return 0;
}

static int cmd_config(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "reload") == 0) {
        // Ricaricare mentre un'app gira libererebbe stringhe che l'app sta
        // usando: si ricarica tra un'app e l'altra, dal gestore. Dal
        // launcher basta farlo ripartire.
        const char *cur = appmgr_current_id();
        if (!cur || strcmp(cur, "launcher") != 0) {
            devcon_printf("exit the app first (config.lua is in use)\n");
            return 1;
        }
        main_request_config_reload();
        appmgr_request_exit();
        devcon_printf("reloading: errors are shown on screen and by 'state'\n");
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "get") == 0) {
        const char *v = config_str(argv[2], NULL);
        devcon_printf("%s\n", v ? v : "(unset)");
        return v ? 0 : 1;
    }
    devcon_printf("usage: config reload | config get <key>\n");
    return 1;
}

static int cmd_log(int argc, char **argv)
{
    if (argc < 3) {
        devcon_printf("usage: log <tag|*> <none|error|warn|info|debug|verbose>\n");
        return 1;
    }
    static const char *const lv[] = {"none", "error", "warn", "info", "debug", "verbose"};
    for (int i = 0; i < 6; i++) {
        if (strcmp(argv[2], lv[i]) == 0) {
            esp_log_level_set(argv[1], (esp_log_level_t)i);
            return 0;
        }
    }
    return 1;
}

// Cambia profilo USB senza passare da un'app (diagnosi). L'app manager lo
// riporta a quello dell'app al prossimo cambio app.
static int cmd_usb(int argc, char **argv)
{
    static const struct {
        const char *name;
        usb_profile_t p;
    } profiles[] = {
        {"none", USB_PROFILE_NONE},
        {"hid", USB_PROFILE_HID},
        {"hiddisk", USB_PROFILE_HID_DISK},
        {"msc", USB_PROFILE_MSC},
    };
    for (size_t i = 0; argc > 1 && i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        if (strcmp(argv[1], profiles[i].name) != 0) continue;
        esp_err_t e = app_usb_apply(profiles[i].p);
        devcon_printf("%s\n", esp_err_to_name(e));
        return e == ESP_OK ? 0 : 1;
    }
    devcon_printf("usage: usb none|hid|hiddisk|msc\n");
    return 1;
}

static int cmd_reboot(int argc, char **argv)
{
    esp_restart();
    return 0;
}

void console_cmds_register(void)
{
    devcon_register("state", "current app, USB, SD, heap", cmd_state);
    devcon_register("apps", "list apps", cmd_apps);
    devcon_register("launch", "<id> [arg]: start an app", cmd_launch);
    devcon_register("exit", "exit current app (like 3s hold)", cmd_exit);
    devcon_register("btn", "click|double|long|hold: simulate button", cmd_btn);
    devcon_register("shot", "dump screen (base64 RGB565)", cmd_shot);
    devcon_register("ls", "[path]: list directory", cmd_ls);
    devcon_register("cat", "<path>: print file (max 16 KB)", cmd_cat);
    devcon_register("config", "reload | get <key>", cmd_config);
    devcon_register("log", "<tag|*> <level>: set log level", cmd_log);
    devcon_register("usb", "none|hid|hiddisk|msc: switch USB profile", cmd_usb);
    devcon_register("reboot", "restart the device", cmd_reboot);
}
