/**
 * @file    pump_driver.h
 * @brief   Pump driver for Leirong 24V peristaltic pump (Active LOW PWM)
 * @details Controls pump via PWM HAL with FG signal feedback for closed-loop
 *          flow control. Integrates with event bus for state notifications.
 *
 * Pump specs (Leirong 24V):
 *   - Encoder PPR: 12 pulses/motor revolution
 *   - Gear ratio: 1:8
 *   - FG pulses per pump head revolution: 96 (12 × 8)
 *   - Flow per revolution: 2100/430 ml ≈ 4.8837 ml
 *   - Flow per pulse: 4375/86 µl ≈ 50.87 µl
 *   - PWM frequency: 25 kHz
 *   - Active LOW: GPIO LOW = pump ON, GPIO HIGH = pump OFF
 *
 * Flow calculation (integer math to avoid float errors):
 *   volume_ul = pulses × 4375 / 86
 *   pulses    = volume_ul × 86 / 4375
 */

#ifndef PUMP_DRIVER_H
#define PUMP_DRIVER_H

#include <Arduino.h>
#include "../system/system_state.h"

/* ============================================================================
 *                         ENUMS & STRUCTS
 * ============================================================================ */

typedef enum {
    PUMP_DIR_CW  = 0,   // Clockwise (forward)
    PUMP_DIR_CCW = 1    // Counter-clockwise (reverse)
} pump_direction_t;

typedef enum {
    PUMP_STOPPED = 0,
    PUMP_RUNNING,
    PUMP_SOFT_STARTING,
    PUMP_SOFT_STOPPING,
    PUMP_ERROR
} pump_state_t;

typedef struct {
    uint32_t pulse_count;           // Total FG pulses counted
    uint32_t motor_rpm;             // Motor shaft RPM
    uint32_t pump_head_rpm;         // Pump head RPM (motor_rpm / gear_ratio)
    uint32_t pulse_period_us;       // Period between FG pulses (µs)
    float    frequency_hz;          // FG signal frequency (Hz)
} pump_fg_data_t;

typedef struct {
    pump_state_t     state;
    pump_direction_t direction;
    uint8_t          speed_percent;  // 0–100
    pump_fg_data_t   fg;
} pump_status_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

// Lifecycle
bool pump_driver_init(void);
void pump_driver_deinit(void);

// Basic control
bool pump_driver_start(uint8_t speed_percent);
bool pump_driver_stop(void);
bool pump_driver_set_speed(uint8_t percent);
bool pump_driver_set_direction(pump_direction_t dir);

// Advanced control
bool pump_driver_run(uint8_t percent, pump_direction_t dir);
bool pump_driver_soft_start(uint8_t target_percent, uint8_t step, uint32_t step_delay_ms);
bool pump_driver_soft_stop(uint8_t step, uint32_t step_delay_ms);

// Status
pump_status_t pump_driver_get_status(void);
bool pump_driver_is_running(void);
uint8_t pump_driver_get_speed(void);

// FG signal / encoder
void pump_driver_update_rpm(void);           // Call every RPM_SAMPLE_PERIOD_MS
pump_fg_data_t pump_driver_get_fg_data(void);
uint32_t pump_driver_get_pulse_count(void);
uint32_t pump_driver_get_pump_head_rpm(void);
bool pump_driver_is_fg_active(void);

// Motor stall detection
bool pump_driver_check_motor_stall(void);    // PWM on but no FG pulses
uint32_t pump_driver_get_time_since_last_pulse(void);

// Reset
void pump_driver_reset_pulses(void);
void pump_driver_reset_fg(void);

// Integer flow helpers (avoids float accumulation errors)
uint32_t pump_driver_pulses_to_ul(uint32_t pulses);
uint32_t pump_driver_ul_to_pulses(uint32_t microliters);

#endif // PUMP_DRIVER_H
