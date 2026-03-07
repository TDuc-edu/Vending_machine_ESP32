/**
 * @file    button_driver.cpp
 * @brief   Button driver with 3-sample debounce + long-press + event bus
 *
 * Debounce method:
 *   Three shift registers sampled every 10ms.
 *   Requires 3 consecutive identical readings for state change.
 *   Eliminates mechanical bounce (typically 5–20ms).
 *
 * State machine per button:
 *   IDLE → PRESSED (edge detected) → HELD (long press timer) → RELEASED
 */

#include "button_driver.h"
#include "../hal/gpio_hal.h"
#include "../system/event_bus.h"
#include "../system/system_state.h"

/* ============================================================================
 *                         PRIVATE TYPES
 * ============================================================================ */

typedef struct {
    // 3-sample debounce registers
    int reg0;           // oldest sample
    int reg1;           // middle sample
    int reg2;           // newest sample
    int stable_state;   // last confirmed stable state

    // Long press tracking
    int  long_press_counter;
    bool long_press_triggered;
    bool is_held;
} button_state_t;

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static button_state_t buttons[BUTTON_COUNT];

static const uint8_t btn_pins[BUTTON_COUNT] = {
    PIN_BTN_0, PIN_BTN_1, PIN_BTN_2
};

/* ============================================================================
 *                         INITIALIZATION
 * ============================================================================ */

void button_driver_init(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        // GPIO pins are already configured by gpio_hal_init()
        // Reset debounce state
        buttons[i].reg0          = BTN_INACTIVE_LEVEL;
        buttons[i].reg1          = BTN_INACTIVE_LEVEL;
        buttons[i].reg2          = BTN_INACTIVE_LEVEL;
        buttons[i].stable_state  = BTN_INACTIVE_LEVEL;

        buttons[i].long_press_counter  = 0;
        buttons[i].long_press_triggered = false;
        buttons[i].is_held             = false;
    }

    Serial.println("[BTN] Initialized (3 buttons, 10ms debounce scan)");
}

/* ============================================================================
 *                         DEBOUNCE SCAN (call every 10ms)
 * ============================================================================ */

void button_driver_scan(void)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        // Phase 1: Shift debounce registers
        buttons[i].reg0 = buttons[i].reg1;
        buttons[i].reg1 = buttons[i].reg2;
        buttons[i].reg2 = gpio_hal_read(btn_pins[i]);

        // Phase 2: Check if 3 consecutive samples are identical (stable)
        if ((buttons[i].reg0 == buttons[i].reg1) &&
            (buttons[i].reg1 == buttons[i].reg2))
        {
            int new_level = buttons[i].reg2;

            // Phase 3: Edge detection (state changed)
            if (new_level != buttons[i].stable_state) {
                buttons[i].stable_state = new_level;

                if (new_level == BTN_ACTIVE_LEVEL) {
                    // ── PRESSED edge ──
                    buttons[i].is_held = true;
                    buttons[i].long_press_counter  = BTN_LONG_PRESS_COUNT;
                    buttons[i].long_press_triggered = false;

                    // Publish press event
                    event_bus_publish(EVT_BUTTON_PRESSED, (uint32_t)i, 0);

                } else {
                    // ── RELEASED edge ──
                    buttons[i].is_held = false;

                    // Publish release event (especially important for hold button)
                    event_bus_publish(EVT_BUTTON_RELEASED, (uint32_t)i, 0);

                    buttons[i].long_press_counter  = 0;
                    buttons[i].long_press_triggered = false;
                }
            }
            else {
                // Phase 4: State unchanged — handle long press
                if (new_level == BTN_ACTIVE_LEVEL) {
                    if (buttons[i].long_press_counter > 0) {
                        buttons[i].long_press_counter--;

                        if (buttons[i].long_press_counter == 0) {
                            // Long press threshold reached
                            if (!buttons[i].long_press_triggered) {
                                buttons[i].long_press_triggered = true;
                                event_bus_publish(EVT_BUTTON_LONG_PRESSED, (uint32_t)i, 0);
                            }
                            // Reset for auto-repeat
                            buttons[i].long_press_counter = BTN_AUTO_REPEAT_COUNT;
                        }
                    }
                }
            }
        }
        // If 3 samples are NOT identical → bounce in progress, ignore
    }
}

/* ============================================================================
 *                         STATUS
 * ============================================================================ */

bool button_driver_is_pressed(uint8_t index)
{
    if (index >= BUTTON_COUNT) return false;
    return (gpio_hal_read(btn_pins[index]) == BTN_ACTIVE_LEVEL);
}

bool button_driver_is_held(uint8_t index)
{
    if (index >= BUTTON_COUNT) return false;
    return buttons[index].is_held;
}
