/**
 * @file    button_driver.h
 * @brief   Button driver with hardware debounce and long-press detection
 * @details Uses 3-sample debounce algorithm scanning every 10ms.
 *          Publishes events to event_bus instead of setting global flags.
 *
 * Debounce algorithm:
 *   Sample GPIO every 10ms into 3 registers (Reg0, Reg1, Reg2).
 *   Button is considered stable when all 3 readings are identical.
 *   Effective debounce time = 3 × 10ms = 30ms.
 *
 * Long press detection:
 *   After stable pressed state, decrement a counter each 10ms.
 *   When counter reaches 0, trigger long press event.
 *   Auto-repeat optional.
 *
 * Buttons (3 buttons):
 *   BTN_0 (GPIO32) - Hold/continuous pump
 *   BTN_1 (GPIO33) - Volume preset 1
 *   BTN_2 (GPIO34) - Volume preset 2 (input-only, external pull-up)
 */

#ifndef BUTTON_DRIVER_H
#define BUTTON_DRIVER_H

#include <Arduino.h>

/* ============================================================================
 *                         BUTTON INDICES
 * ============================================================================ */

#define BTN_IDX_HOLD        0   // BTN_0: Hold to pump continuously
#define BTN_IDX_PRESET_1    1   // BTN_1: Volume preset 1
#define BTN_IDX_PRESET_2    2   // BTN_2: Volume preset 2

/* ============================================================================
 *                         BUTTON STATES
 * ============================================================================ */

#define BTN_ACTIVE_LEVEL    LOW     // Active LOW (pull-up buttons)
#define BTN_INACTIVE_LEVEL  HIGH

/* ============================================================================
 *                         CONFIGURATION
 * ============================================================================ */

#define BTN_SCAN_PERIOD_MS      10      // Debounce scan interval
#define BTN_LONG_PRESS_COUNT    200     // 200 × 10ms = 2000ms long press
#define BTN_AUTO_REPEAT_COUNT   50      // 50 × 10ms = 500ms auto-repeat

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize button driver (configures GPIO, resets state)
 */
void button_driver_init(void);

/**
 * @brief Process button scanning (call every 10ms from task or timer)
 *        Performs debounce, edge detection, and publishes events to event_bus.
 *
 * Events published:
 *   EVT_BUTTON_PRESSED      → param1 = button_index
 *   EVT_BUTTON_LONG_PRESSED → param1 = button_index
 *   EVT_BUTTON_RELEASED     → param1 = button_index
 */
void button_driver_scan(void);

/**
 * @brief Check if a specific button is currently pressed (real-time read)
 * @param index Button index (0–4)
 * @return true if pressed
 */
bool button_driver_is_pressed(uint8_t index);

/**
 * @brief Get the debounced state of a button (from last scan)
 * @param index Button index (0–4)
 * @return true if currently held
 */
bool button_driver_is_held(uint8_t index);

#endif // BUTTON_DRIVER_H
