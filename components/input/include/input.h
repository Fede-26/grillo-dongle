#pragma once

#include <stdint.h>
#include "gesture.h"

// Callback chiamata dal task del pulsante per ogni gesto riconosciuto.
// `progress` vale solo per GESTURE_HOLD_PROGRESS.
typedef void (*input_handler_t)(gesture_t g, uint8_t progress);

void input_init(input_handler_t handler);
// Stato istantaneo del pulsante (per es. "tieni premuto all'avvio").
bool input_is_pressed(void);
