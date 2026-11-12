#include "input.h"

#include "board.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POLL_MS 10

static input_handler_t s_handler;

bool input_is_pressed(void)
{
    return gpio_get_level(BOARD_BUTTON) == BOARD_BUTTON_ACTIVE;
}

static void input_task(void *arg)
{
    gesture_ctx_t g;
    gesture_init(&g);
    bool stable = input_is_pressed();
    int same = 0;
    for (;;) {
        // Debounce: the state changes only after 3 equal readings (30 ms).
        bool raw = input_is_pressed();
        if (raw != stable) {
            if (++same >= 3) {
                stable = raw;
                same = 0;
            }
        } else {
            same = 0;
        }
        uint8_t progress = 0;
        gesture_t ev = gesture_feed(&g, stable, (uint32_t)(esp_timer_get_time() / 1000), &progress);
        if (ev != GESTURE_NONE && s_handler) s_handler(ev, progress);
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void input_init(input_handler_t handler)
{
    s_handler = handler;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOARD_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    xTaskCreate(input_task, "input", 3072, NULL, 5, NULL);
}
