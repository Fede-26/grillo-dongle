#include "gesture.h"

enum { ST_IDLE, ST_PRESSED, ST_WAIT_DOUBLE, ST_HELD_DONE };

void gesture_init(gesture_ctx_t *g)
{
    *g = (gesture_ctx_t){.state = ST_IDLE};
}

gesture_t gesture_feed(gesture_ctx_t *g, bool pressed, uint32_t now, uint8_t *progress)
{
    switch (g->state) {
    case ST_IDLE:
        if (pressed) {
            g->state = ST_PRESSED;
            g->t0 = now;
            g->second = false;
            g->armed = false;
            g->last_progress = 0xFF;
        }
        return GESTURE_NONE;

    case ST_WAIT_DOUBLE:
        if (pressed) {
            g->state = ST_PRESSED;
            g->t0 = now;
            g->second = true;
            g->armed = false;
            g->last_progress = 0xFF;
            return GESTURE_NONE;
        }
        if (now - g->t0 >= GESTURE_DOUBLE_MS) {
            g->state = ST_IDLE;
            return GESTURE_CLICK;
        }
        return GESTURE_NONE;

    case ST_PRESSED: {
        uint32_t held = now - g->t0;
        if (!pressed) {
            if (held < GESTURE_LONG_MS) {
                if (g->second) {
                    g->state = ST_IDLE;
                    return GESTURE_DOUBLE;
                }
                // The click is emitted only once the window has expired, so it
                // can be told apart from the double click.
                g->state = ST_WAIT_DOUBLE;
                g->t0 = now;
                return GESTURE_NONE;
            }
            g->state = ST_IDLE;
            // Releasing during the ring cancels: whoever wanted to exit and
            // changed their mind must not end up with an unintended confirm.
            return held < GESTURE_HOLD_START_MS ? GESTURE_LONG : GESTURE_HOLD_CANCEL;
        }
        if (held >= GESTURE_HOLD_MS) {
            g->state = ST_HELD_DONE;
            return GESTURE_HOLD_EXIT;
        }
        if (held >= GESTURE_HOLD_START_MS) {
            uint8_t p = (held - GESTURE_HOLD_START_MS) * 100 / (GESTURE_HOLD_MS - GESTURE_HOLD_START_MS);
            if (p != g->last_progress) {
                g->last_progress = p;
                if (progress) *progress = p;
                return GESTURE_HOLD_PROGRESS;
            }
            return GESTURE_NONE;
        }
        if (held >= GESTURE_LONG_MS && !g->armed) {
            g->armed = true;
            return GESTURE_LONG_ARMED;
        }
        return GESTURE_NONE;
    }

    case ST_HELD_DONE:
        if (!pressed) g->state = ST_IDLE;
        return GESTURE_NONE;
    }
    return GESTURE_NONE;
}
