#pragma once

#include <stdbool.h>
#include <stdint.h>

// Gesture recognizer for a single button. Pure logic, with no
// ESP-IDF dependencies, so it is testable on the PC (test/host).

#define GESTURE_LONG_MS        600  // from here the release is "long" (confirm)
// From here the exit bar appears. Up to here the release still counts as
// "long": 1.5 s leaves time for whoever releases calmly after the signal.
#define GESTURE_HOLD_START_MS  1500
#define GESTURE_HOLD_MS        3000 // held up to here = exit the app
#define GESTURE_DOUBLE_MS      250  // window for the second click

typedef enum {
    GESTURE_NONE,
    GESTURE_CLICK,
    GESTURE_DOUBLE,
    GESTURE_LONG,          // release between LONG_MS and HOLD_START_MS
    GESTURE_LONG_ARMED,    // LONG threshold passed while still pressed (feedback)
    GESTURE_HOLD_PROGRESS, // between HOLD_START and HOLD: value 0..100 in *progress
    GESTURE_HOLD_CANCEL,   // released during the ring: no action
    GESTURE_HOLD_EXIT,
} gesture_t;

typedef struct {
    int state;
    uint32_t t0;
    bool second;
    bool armed;
    uint8_t last_progress;
} gesture_ctx_t;

void gesture_init(gesture_ctx_t *g);
// To be called periodically (e.g. every 10 ms) with the already "debounced" state.
gesture_t gesture_feed(gesture_ctx_t *g, bool pressed, uint32_t now_ms, uint8_t *progress);
