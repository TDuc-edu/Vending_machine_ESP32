/**
 * @file    sensor_driver.h
 * @brief   Liquid level sensor driver for NPN sensors with level shifter
 * @details Monitors inlet (before pump) and outlet (after pump) water presence
 *          using NPN sensors interfaced via 2N2222A transistor level shifter.
 *
 * Hardware setup:
 *   - NPN sensor (24V) → 10k → Base of 2N2222A
 *   - Collector → ESP32 GPIO (with 10k pull-up to 3.3V)
 *   - Emitter → GND
 *
 * IMPORTANT: Transistor acts as INVERTER!
 *   - Sensor active (water): NPN OUT=LOW → Base LOW → Transistor OFF → Collector HIGH
 *   - Sensor inactive (no water): NPN OUT=HIGH → Base HIGH → Transistor ON → Collector LOW
 *
 * Logic at ESP32 GPIO:
 *   - GPIO HIGH (1) = Water detected (inverted by transistor)
 *   - GPIO LOW (0)  = No water (inverted by transistor)
 *
 * Debounce:
 *   - 2-sample shift register (50ms scan interval)
 *   - State change confirmed when both samples match
 */

#ifndef SENSOR_DRIVER_H
#define SENSOR_DRIVER_H

#include <Arduino.h>

/* ============================================================================
 *                         ENUMS & STRUCTS
 * ============================================================================ */

typedef enum {
    SENSOR_INLET  = 0,      // Before pump
    SENSOR_OUTLET = 1       // After pump
} sensor_id_t;

typedef enum {
    SENSOR_NO_WATER = 0,    // GPIO HIGH
    SENSOR_HAS_WATER = 1    // GPIO LOW (active)
} sensor_state_t;

typedef struct {
    sensor_state_t inlet_state;         // Current inlet sensor state
    sensor_state_t outlet_state;        // Current outlet sensor state
    bool inlet_stable;                  // Inlet state is debounced and stable
    bool outlet_stable;                 // Outlet state is debounced and stable
    uint32_t inlet_last_change_ms;      // Timestamp of last inlet state change
    uint32_t outlet_last_change_ms;     // Timestamp of last outlet state change
} sensor_status_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize liquid level sensors
 * @return true if successful
 */
bool sensor_driver_init(void);

/**
 * @brief Deinitialize sensor driver
 */
void sensor_driver_deinit(void);

/**
 * @brief Scan sensors and perform debounce (call every 50ms)
 * @details Implements 2-sample shift register debounce.
 *          Publishes events on state changes.
 */
void sensor_driver_scan(void);

/**
 * @brief Get current sensor state (debounced)
 * @param sensor_id Which sensor to query
 * @return sensor_state_t (SENSOR_HAS_WATER or SENSOR_NO_WATER)
 */
sensor_state_t sensor_driver_get_state(sensor_id_t sensor_id);

/**
 * @brief Check if both sensors detect water
 * @return true if both inlet and outlet sensors detect water
 */
bool sensor_driver_both_have_water(void);

/**
 * @brief Get sensor status structure
 * @return Pointer to internal sensor status (read-only)
 */
const sensor_status_t* sensor_driver_get_status(void);

/**
 * @brief Check if pump tube is likely broken/disconnected
 * @details Logic: If inlet has water but outlet does not have water
 *          for a prolonged period, tube may be broken.
 * @return true if fault condition detected
 */
bool sensor_driver_check_tube_fault(void);

#endif // SENSOR_DRIVER_H
