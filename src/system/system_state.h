/**
 * @file    system_state.h
 * @brief   Global system state manager
 * @details Thread-safe system state with FreeRTOS mutex protection.
 *          Single source of truth for the vending machine state.
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/* ============================================================================
 *                         SYSTEM STATES
 * ============================================================================ */

typedef enum {
    SYS_STATE_BOOT = 0,
    SYS_STATE_INIT,
    SYS_STATE_READY,            // Idle, waiting for input
    SYS_STATE_DISPENSING,       // Pump running (volume mode)
    SYS_STATE_HOLD_PUMP,        // Pump running (hold button mode)
    SYS_STATE_ERROR,
    SYS_STATE_EMERGENCY_STOP,
    SYS_STATE_COUNT
} system_state_t;

/* ============================================================================
 *                         PUMP SPECIFICATIONS (from migration guide)
 * ============================================================================ */

// Leirong 24V pump parameters
#define PUMP_ENCODER_PPR        12      // Pulses per motor revolution
#define PUMP_GEAR_RATIO         8       // Gear ratio 1:8
#define PUMP_FG_PER_REV         (PUMP_ENCODER_PPR * PUMP_GEAR_RATIO) // 96 pulses/pump head rev

// Flow calculation: 1 pump head revolution = 2100/430 ml ≈ 4.8837 ml
// 1 FG pulse = 2100/(430×96) ml = 4375/86 µl ≈ 50.87 µl
#define FLOW_UL_NUMERATOR       4375UL  // Reduced fraction numerator
#define FLOW_UL_DENOMINATOR     86UL    // Reduced fraction denominator

// Button configuration
#define BUTTON_COUNT            3
#define BUTTON_SCAN_MS          10      // Debounce scan period
#define BUTTON_LONG_PRESS_MS    2000    // 2 seconds for long press
#define BUTTON_AUTO_REPEAT_MS   500     // Auto-repeat interval

// FG signal timeout
#define FG_TIMEOUT_MS           500
#define RPM_SAMPLE_PERIOD_MS    1000

/* ============================================================================
 *                         SYSTEM STATUS STRUCTURE
 * ============================================================================ */

typedef struct {
    // System
    system_state_t state;
    uint32_t uptime_sec;
    uint32_t boot_count;

    // Pump
    bool     pump_running;
    uint8_t  pump_speed_percent;
    uint32_t pump_rpm;
    uint32_t pump_pulse_count;

    // Flow
    uint32_t flow_target_ul;
    uint32_t flow_dispensed_ul;
    uint8_t  flow_progress;

    // Network
    bool     wifi_connected;
    bool     mqtt_connected;
    int8_t   wifi_rssi;

    // Telemetry
    uint32_t total_dispensed_ml;
    uint32_t total_dispense_count;
    float    cpu_temp;
} system_status_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize system state manager (creates mutex)
 */
void system_state_init(void);

/**
 * @brief Get current system state (thread-safe)
 * @return Current system state enum
 */
system_state_t system_state_get(void);

/**
 * @brief Set system state (thread-safe)
 * @param new_state New state to transition to
 */
void system_state_set(system_state_t new_state);

/**
 * @brief Get full system status snapshot (thread-safe deep copy)
 * @param status Pointer to status struct to fill
 */
void system_state_get_status(system_status_t *status);

/**
 * @brief Update a specific field in system status (thread-safe)
 *        Uses a callback pattern for atomic updates
 */
void system_state_update_pump(bool running, uint8_t speed, uint32_t rpm, uint32_t pulses);
void system_state_update_flow(uint32_t target_ul, uint32_t dispensed_ul, uint8_t progress);
void system_state_update_network(bool wifi, bool mqtt, int8_t rssi);
void system_state_add_dispense(uint32_t volume_ml);

/**
 * @brief Get human-readable state name
 * @param state System state enum
 * @return State name string
 */
const char* system_state_name(system_state_t state);

#endif // SYSTEM_STATE_H
