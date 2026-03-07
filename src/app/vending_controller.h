/**
 * @file    vending_controller.h
 * @brief   Main vending machine controller (Application Layer)
 * @details Implements the vending state machine, consumes events from event_bus,
 *          coordinates pump_driver and recipe_engine.
 *
 * State Machine:
 *   IDLE → DISPENSING (volume mode) or HOLD_PUMP (hold button mode)
 *   DISPENSING → IDLE (complete) or ERROR (FG timeout)
 *   HOLD_PUMP → IDLE (button released)
 *   ERROR → IDLE (reset)
 *
 * This module runs on Core 1 (APP CPU) for real-time performance.
 */

#ifndef VENDING_CONTROLLER_H
#define VENDING_CONTROLLER_H

#include <Arduino.h>

/* ============================================================================
 *                         VENDING STATES
 * ============================================================================ */

typedef enum {
    VEND_STATE_IDLE = 0,
    VEND_STATE_HOLD_PUMP,       // BTN_0 held → continuous pump
    VEND_STATE_DISPENSING,      // Volume-based dispensing
    VEND_STATE_COMPLETE,        // Dispense done (brief pause before IDLE)
    VEND_STATE_ERROR,
    VEND_STATE_COUNT
} vend_state_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize vending controller
 */
void vending_controller_init(void);

/**
 * @brief Process one event-loop iteration of the vending controller
 *        Reads events from event_bus and drives the state machine.
 *        Call this from the vending_logic_task on Core 1.
 */
void vending_controller_update(void);

/**
 * @brief Emergency stop — immediately stops pump and resets to IDLE
 */
void vending_controller_emergency_stop(void);

/**
 * @brief Get current vending state
 */
vend_state_t vending_controller_get_state(void);

/**
 * @brief Get human-readable state name
 */
const char* vending_controller_get_state_name(void);

/**
 * @brief Start dispensing a specific volume (triggered by MQTT command)
 * @param volume_ml Volume in ml
 * @param speed_percent Pump speed
 * @return true if started
 */
bool vending_controller_dispense_ml(uint32_t volume_ml, uint8_t speed_percent);

#endif // VENDING_CONTROLLER_H
