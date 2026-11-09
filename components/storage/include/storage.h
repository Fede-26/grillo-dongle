#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"

#define STORAGE_ROOT "/sd"

typedef enum {
    STORAGE_ABSENT,      // nessuna scheda o inizializzazione fallita
    STORAGE_MOUNTED,     // FAT montata su STORAGE_ROOT
    STORAGE_UNSUPPORTED, // scheda presente ma filesystem non FAT (es. exFAT)
    STORAGE_LENT,        // scheda prestata all'host USB (MSC): VFS smontata
} storage_state_t;

// Inizializza il bus SDMMC e la scheda, poi prova a montare la FAT.
esp_err_t storage_init(void);
storage_state_t storage_state(void);
const char *storage_state_name(storage_state_t s);

// Il possesso della scheda è esclusivo: o il firmware (VFS) o l'host USB.
// storage_lend() smonta la VFS e restituisce la scheda grezza per l'MSC;
// storage_reclaim() la riprende e rimonta la FAT (anche se nel frattempo
// è stata riformattata dal PC).
sdmmc_card_t *storage_lend(void);
esp_err_t storage_reclaim(void);

bool storage_info(uint64_t *total_bytes, uint64_t *free_bytes);
