// Table of the apps shown in the launcher, in carousel order.
// To add an app: include its header and add it here (and to the
// REQUIRES of main/CMakeLists.txt).

#include "app_list.h"
#include "injector.h"
#include "netscan.h"
#include "usbdisk.h"
#include "webfiles.h"
#include "webhid.h"
#include "webinjector.h"

const app_desc_t *const g_apps[] = {
    &injector_app,
    &webinjector_app,
    &webhid_app,
    &webfiles_app,
    &netscan_app,
    &usbdisk_app,
};

const size_t g_app_count = sizeof(g_apps) / sizeof(g_apps[0]);
