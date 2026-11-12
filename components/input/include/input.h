#pragma once

#include <stdint.h>
#include "gesture.h"

// Callback called by the button task for every recognized gesture.
// `progress` is meaningful only for GESTURE_HOLD_PROGRESS.
typedef void (*input_handler_t)(gesture_t g, uint8_t progress);

void input_init(input_handler_t handler);
// Instantaneous button state (e.g. "hold pressed at boot").
bool input_is_pressed(void);
