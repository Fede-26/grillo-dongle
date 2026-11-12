#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#define STORAGE_ROOT "/sd"

typedef enum {
    STORAGE_ABSENT,      // no card or initialization failed
    STORAGE_MOUNTED,     // FAT mounted on STORAGE_ROOT
    STORAGE_UNSUPPORTED, // card present but filesystem not FAT (e.g. exFAT)
    STORAGE_LENT,        // card lent to the USB host (MSC): VFS unmounted
} storage_state_t;

// Initializes the SDMMC bus and the card, then tries to mount the FAT.
esp_err_t storage_init(void);
storage_state_t storage_state(void);
const char *storage_state_name(storage_state_t s);

// Card ownership is exclusive: either the firmware (VFS) or the USB host.
// storage_lend() unmounts the VFS and returns the raw card for the MSC;
// storage_reclaim() takes it back and remounts the FAT (even if it has
// been reformatted by the PC in the meantime).
sdmmc_card_t *storage_lend(void);
esp_err_t storage_reclaim(void);

// How many times the card has been lent: lets you tell whether the PC
// may have modified its contents in the meantime.
uint32_t storage_lend_count(void);

// Read-only sharing with the host (USB profile HID_DISK): the FAT
// stays mounted, but while it is shared the firmware must not write to it
// (the host would not see the changes and its cache would become inconsistent).
sdmmc_card_t *storage_share_ro(void);
void storage_unshare(void);
bool storage_is_shared(void);

// Writing from a task running in parallel with the apps (e.g. syslog): true if
// the card is mounted and not shared, and in that case it holds the lock until
// storage_end_write(), so lending or sharing wait for the write to finish.
// With false the lock is not taken and nothing must be written.
bool storage_begin_write(void);
void storage_end_write(void);

// Marker file the scripts use to find the SD on the PC.
#define STORAGE_MARKER STORAGE_ROOT "/.grillo"

bool storage_info(uint64_t *total_bytes, uint64_t *free_bytes);
