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

// Quante volte la scheda è stata prestata: permette di capire se nel
// frattempo il PC può averne modificato il contenuto.
uint32_t storage_lend_count(void);

// Condivisione in sola lettura con l'host (profilo USB HID_DISK): la FAT
// resta montata, ma finché è condivisa il firmware non deve scriverci
// (l'host non vedrebbe le modifiche e la sua cache diventerebbe incoerente).
sdmmc_card_t *storage_share_ro(void);
void storage_unshare(void);
bool storage_is_shared(void);

// Scrittura da un task che gira in parallelo alle app (es. syslog): true se
// la scheda è montata e non condivisa, e in quel caso tiene il lock fino a
// storage_end_write(), così il prestito o la condivisione aspettano la fine
// della scrittura. Con false il lock non è preso e non si deve scrivere.
bool storage_begin_write(void);
void storage_end_write(void);

// File marcatore che gli script usano per trovare la SD sul PC.
#define STORAGE_MARKER STORAGE_ROOT "/.grillo"

bool storage_info(uint64_t *total_bytes, uint64_t *free_bytes);
