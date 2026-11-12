#include "storage.h"

#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "ff.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "storage";

static sdmmc_card_t s_card;
static bool s_card_ok;
static FATFS *s_fs;
static BYTE s_pdrv = 0xFF;
static storage_state_t s_state = STORAGE_ABSENT;
static uint32_t s_lend_count;
static volatile bool s_shared;
// Serializes writes by the system tasks (syslog) with the card ownership
// hand-overs: without it, a log line could open a file while storage_lend()
// was unmounting the FAT, leaving lost allocated clusters.
static SemaphoreHandle_t s_wlock;

const char *storage_state_name(storage_state_t s)
{
    switch (s) {
    case STORAGE_MOUNTED: return "mounted";
    case STORAGE_UNSUPPORTED: return "not FAT";
    case STORAGE_LENT: return "on USB";
    default: return "no card";
    }
}

storage_state_t storage_state(void)
{
    return s_state;
}

static esp_err_t card_init(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 4;
    slot.clk = BOARD_SD_CLK;
    slot.cmd = BOARD_SD_CMD;
    slot.d0 = BOARD_SD_D0;
    slot.d1 = BOARD_SD_D1;
    slot.d2 = BOARD_SD_D2;
    slot.d3 = BOARD_SD_D3;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) return err;
    err = sdmmc_card_init(&host, &s_card);
    if (err != ESP_OK) {
        // Some cards cannot cope with high speed: retry at standard speed
        host.max_freq_khz = SDMMC_FREQ_DEFAULT;
        err = sdmmc_card_init(&host, &s_card);
    }
    return err;
}

// Tells "no filesystem" apart from "filesystem we cannot read":
// partition type 0x07 in the MBR means exFAT/NTFS, the exFAT boot sector
// has the signature "EXFAT   " at offset 3.
static bool looks_like_exfat(void)
{
    uint8_t *buf = heap_caps_malloc(512, MALLOC_CAP_DMA);
    if (!buf) return false;
    bool r = false;
    if (sdmmc_read_sectors(&s_card, buf, 0, 1) == ESP_OK) {
        r = memcmp(buf + 3, "EXFAT   ", 8) == 0 || buf[446 + 4] == 0x07;
    }
    free(buf);
    return r;
}

static esp_err_t mount(void)
{
    if (ff_diskio_get_drive(&s_pdrv) != ESP_OK) return ESP_FAIL;
    ff_diskio_register_sdmmc(s_pdrv, &s_card);
    char drv[3] = {(char)('0' + s_pdrv), ':', 0};
    esp_vfs_fat_conf_t conf = {.base_path = STORAGE_ROOT, .fat_drive = drv, .max_files = 8};
    esp_err_t err = esp_vfs_fat_register(&conf, &s_fs);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    FRESULT fr = f_mount(s_fs, drv, 1);
    if (fr != FR_OK) {
        ESP_LOGW(TAG, "f_mount: %d", fr);
        esp_vfs_fat_unregister_path(STORAGE_ROOT);
        err = fr == FR_NO_FILESYSTEM ? ESP_ERR_NOT_SUPPORTED : ESP_FAIL;
        goto fail;
    }
    s_state = STORAGE_MOUNTED;
    // The marker lets the injector scripts recognize this SD among the
    // PC's drives, whatever the volume label.
    FILE *m = fopen(STORAGE_MARKER, "r");
    if (m) {
        fclose(m);
    } else if ((m = fopen(STORAGE_MARKER, "w"))) {
        fputs("Grillo SD marker: used by scripts to find this disk. Do not delete.\n", m);
        fclose(m);
    }
    return ESP_OK;
fail:
    ff_diskio_unregister(s_pdrv);
    s_pdrv = 0xFF;
    return err;
}

static void unmount(void)
{
    if (s_pdrv == 0xFF) return;
    char drv[3] = {(char)('0' + s_pdrv), ':', 0};
    f_mount(NULL, drv, 0);
    ff_diskio_unregister(s_pdrv);
    esp_vfs_fat_unregister_path(STORAGE_ROOT);
    s_pdrv = 0xFF;
}

esp_err_t storage_init(void)
{
    s_wlock = xSemaphoreCreateMutex();
    esp_err_t err = card_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no SD (%s)", esp_err_to_name(err));
        s_state = STORAGE_ABSENT;
        return err;
    }
    s_card_ok = true;
    sdmmc_card_print_info(stdout, &s_card);
    err = mount();
    if (err != ESP_OK) {
        s_state = STORAGE_UNSUPPORTED;
        ESP_LOGW(TAG, "SD not mountable%s", looks_like_exfat() ? ": exFAT, must be formatted FAT32" : "");
    }
    return err;
}

sdmmc_card_t *storage_lend(void)
{
    if (!s_card_ok || s_state == STORAGE_LENT) return NULL;
    xSemaphoreTake(s_wlock, portMAX_DELAY);
    unmount();
    s_state = STORAGE_LENT;
    s_lend_count++;
    xSemaphoreGive(s_wlock);
    return &s_card;
}

esp_err_t storage_reclaim(void)
{
    if (s_state != STORAGE_LENT) return ESP_ERR_INVALID_STATE;
    esp_err_t err = mount();
    if (err != ESP_OK) s_state = STORAGE_UNSUPPORTED;
    return err;
}

bool storage_info(uint64_t *total, uint64_t *free_b)
{
    if (s_state != STORAGE_MOUNTED) return false;
    return esp_vfs_fat_info(STORAGE_ROOT, total, free_b) == ESP_OK;
}

uint32_t storage_lend_count(void)
{
    return s_lend_count;
}

sdmmc_card_t *storage_share_ro(void)
{
    if (s_state != STORAGE_MOUNTED) return NULL;
    xSemaphoreTake(s_wlock, portMAX_DELAY);
    s_shared = true;
    xSemaphoreGive(s_wlock);
    return &s_card;
}

void storage_unshare(void)
{
    s_shared = false;
}

bool storage_is_shared(void)
{
    return s_shared;
}

bool storage_begin_write(void)
{
    if (!s_wlock) return false;
    xSemaphoreTake(s_wlock, portMAX_DELAY);
    if (s_state == STORAGE_MOUNTED && !s_shared) return true;
    xSemaphoreGive(s_wlock);
    return false;
}

void storage_end_write(void)
{
    xSemaphoreGive(s_wlock);
}
