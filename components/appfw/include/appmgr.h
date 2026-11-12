#pragma once

#include "app.h"
#include "gesture.h"

// App manager: runs one app at a time, the launcher when no other is in the
// foreground.

// Starts the manager (never returns). `apps` does not include the launcher.
void appmgr_run(const app_desc_t *launcher, const app_desc_t *const *apps, size_t count);

size_t appmgr_count(void);
const app_desc_t *appmgr_get(size_t index);
const app_desc_t *appmgr_find(const char *id);

// Requests the launch of `app` as soon as the current app terminates
// (typically called by the launcher just before returning from run()).
void appmgr_launch(const app_desc_t *app, const char *arg);

// Reason why the app cannot start now (e.g. "no SD card"), NULL if ok.
const char *appmgr_unavailable(const app_desc_t *app);

// Cleanup function called every time an app exits. Services (network,
// etc.) register it to release what the app left on.
void appmgr_add_cleanup(void (*fn)(void));
// true (once) if an app or the console called app_config_changed().
bool appmgr_take_config_changed(void);

// --- Used by the debug console ---

// Id of the foreground app ("launcher" if none), NULL during switches.
const char *appmgr_current_id(void);
// Like holding for 3 s (also applies to the launcher, which then returns).
void appmgr_request_exit(void);
// Simulates a button gesture.
void appmgr_inject(gesture_t g, uint8_t progress);
