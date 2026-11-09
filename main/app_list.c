// Tabella delle app mostrate nel launcher, nell'ordine del carosello.
// Per aggiungere un'app: includi il suo header e aggiungila qui (e al
// REQUIRES di main/CMakeLists.txt).

#include "app_list.h"
#include "injector.h"
#include "usbdisk.h"
#include "webhid.h"

const app_desc_t *const g_apps[] = {
    &injector_app,
    &webhid_app,
    &usbdisk_app,
};

const size_t g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);
