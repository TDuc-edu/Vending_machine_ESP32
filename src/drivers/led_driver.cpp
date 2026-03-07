/**
 * @file    led_driver.cpp
 * @brief   LED driver implementation with pattern support
 */

#include "led_driver.h"
#include "../hal/gpio_hal.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static led_pattern_t current_pattern = LED_OFF;
static uint32_t last_toggle = 0;
static bool led_state = false;

// Blink intervals in ms
static const uint32_t blink_intervals[] = {
    0,      // OFF
    0,      // ON
    1000,   // SLOW  (1 Hz)
    200,    // FAST  (5 Hz)
    100     // ERROR (10 Hz)
};

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

void led_driver_init(void)
{
    gpio_hal_set_mode(PIN_LED_STATUS, GPIO_HAL_OUTPUT);
    gpio_hal_write(PIN_LED_STATUS, GPIO_HAL_LOW);
    current_pattern = LED_OFF;
    led_state = false;
    Serial.println("[LED] Initialized");
}

void led_driver_set_pattern(led_pattern_t pattern)
{
    current_pattern = pattern;

    switch (pattern) {
        case LED_OFF:
            gpio_hal_write(PIN_LED_STATUS, GPIO_HAL_LOW);
            led_state = false;
            break;
        case LED_ON:
            gpio_hal_write(PIN_LED_STATUS, GPIO_HAL_HIGH);
            led_state = true;
            break;
        default:
            // Blinking patterns handled in update()
            break;
    }
}

void led_driver_update(void)
{
    if (current_pattern < LED_BLINK_SLOW) return;  // No need to update static states

    uint32_t interval = blink_intervals[current_pattern];
    if (interval == 0) return;

    uint32_t now = millis();
    if (now - last_toggle >= interval) {
        last_toggle = now;
        led_state = !led_state;
        gpio_hal_write(PIN_LED_STATUS, led_state ? GPIO_HAL_HIGH : GPIO_HAL_LOW);
    }
}
