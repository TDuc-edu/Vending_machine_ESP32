/**
 * @file    pump_driver.cpp
 * @brief   Pump driver implementation for Leirong 24V pump on ESP32
 *
 * Active LOW PWM control:
 *   pwm_hal_set_duty_active_low() handles inversion internally.
 *   0% power → GPIO HIGH (pump OFF)
 *   100% power → GPIO LOW (pump FULL ON)
 *
 * FG signal counting with sensor validation:
 *   Uses GPIO interrupt on rising edge to count encoder pulses.
 *   Flow volume is only counted when BOTH inlet and outlet sensors detect water.
 *   This prevents counting during tube fault conditions (broken/disconnected tube).
 *   ISR is IRAM_ATTR for reliable timing.
 *
 * Flow calculation (integer arithmetic — no float accumulation):
 *   microliters = pulses × 4375 / 86
 *   pulses      = microliters × 86 / 4375
 *   With rounding: (num + den/2) / den
 */

#include "pump_driver.h"
#include "../hal/gpio_hal.h"
#include "../hal/pwm_hal.h"
#include "../system/event_bus.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static pump_status_t pump;
static bool initialized = false;

// FG signal tracking (volatile for ISR access)
static volatile int32_t  total_pulses        = 0;
static volatile int32_t  rpm_pulse_counter   = 0;
static volatile uint32_t last_pulse_time_us  = 0;
static volatile uint32_t pulse_period_us     = 0;
static volatile uint32_t last_pulse_tick_ms  = 0;

// Sensor tracking (non-volatile, only counted pulses are affected)
static volatile int32_t  total_pulses_no_sensor_check = 0;  // Raw encoder pulses

/* ============================================================================
 *                         ISR HANDLER
 * ============================================================================ */

/**
 * @brief FG signal ISR - counts pulses only when both sensors detect water
 * @details Reads sensor GPIOs directly (fast, ISR-safe).
 *          Only increments flow counters if both sensors are active (HIGH).
 *          Raw pulse count is always incremented for diagnostics.
 *          NOTE: Transistor inverts logic - GPIO HIGH = water present
 */
static void IRAM_ATTR fg_signal_isr(void)
{
    uint32_t now_us = micros();

    if (last_pulse_time_us > 0) {
        pulse_period_us = now_us - last_pulse_time_us;
    }
    last_pulse_time_us = now_us;
    last_pulse_tick_ms = millis();

    // Always count raw pulses (for diagnostics)
    total_pulses_no_sensor_check++;

    // Read sensor states directly
    // Transistor inverts: GPIO HIGH = water detected, GPIO LOW = no water
    int inlet_gpio = digitalRead(PIN_SENSOR_INLET);
    int outlet_gpio = digitalRead(PIN_SENSOR_OUTLET);

    // Only count flow pulses if BOTH sensors detect water (both HIGH)
    if (inlet_gpio == HIGH && outlet_gpio == HIGH) {
        total_pulses++;
        rpm_pulse_counter++;
    }
}

/* ============================================================================
 *                         PRIVATE HELPERS
 * ============================================================================ */

/**
 * @brief Integer division with rounding: (num + den/2) / den
 *        Reduces systematic error in flow calculation.
 */
static uint32_t div_round(uint32_t numerator, uint32_t denominator)
{
    if (denominator == 0) return 0;
    return (numerator + (denominator / 2)) / denominator;
}

/* ============================================================================
 *                         LIFECYCLE
 * ============================================================================ */

bool pump_driver_init(void)
{
    // Initialize PWM on motor pin (25 kHz, 12-bit, active LOW)
    if (!pwm_hal_init(PIN_PWM_MOTOR)) {
        Serial.println("[PUMP] PWM init failed!");
        return false;
    }
    // Start with pump OFF (active LOW: duty=4095 → GPIO HIGH)
    pwm_hal_set_duty_active_low(PIN_PWM_MOTOR, 0);

    // Direction pin
    gpio_hal_set_mode(PIN_MOTOR_DIR, GPIO_HAL_OUTPUT);
    gpio_hal_write(PIN_MOTOR_DIR, GPIO_HAL_HIGH);  // CW default

    gpio_hal_set_mode(PIN_FG_SIGNAL, GPIO_HAL_INPUT); //pull up 10k - 3v3
    gpio_hal_attach_interrupt(PIN_FG_SIGNAL, fg_signal_isr, GPIO_HAL_EDGE_RISING);

    // Initialize status
    memset(&pump, 0, sizeof(pump_status_t));
    pump.state     = PUMP_STOPPED;
    pump.direction = PUMP_DIR_CW;

    // Reset counters
    total_pulses       = 0;
    rpm_pulse_counter  = 0;
    last_pulse_time_us = 0;
    pulse_period_us    = 0;
    last_pulse_tick_ms = 0;

    initialized = true;
    Serial.println("[PUMP] Initialized (25kHz PWM, Active LOW)");
    return true;
}

void pump_driver_deinit(void)
{
    if (!initialized) return;
    pump_driver_stop();
    gpio_hal_detach_interrupt(PIN_FG_SIGNAL);
    pwm_hal_deinit(PIN_PWM_MOTOR);
    initialized = false;
}

/* ============================================================================
 *                         BASIC CONTROL
 * ============================================================================ */

bool pump_driver_start(uint8_t speed_percent)
{
    if (!initialized) return false;
    if (speed_percent == 0 || speed_percent > 100) return false;

    pwm_hal_set_duty_active_low(PIN_PWM_MOTOR, speed_percent);
    pump.speed_percent = speed_percent;
    pump.state = PUMP_RUNNING;

    event_bus_publish(EVT_PUMP_START, speed_percent, 0);
    Serial.printf("[PUMP] Started at %d%%\n", speed_percent);
    return true;
}

bool pump_driver_stop(void)
{
    if (!initialized) return false;

    pwm_hal_set_duty_active_low(PIN_PWM_MOTOR, 0);  // OFF
    pump.speed_percent = 0;
    pump.state = PUMP_STOPPED;

    event_bus_publish(EVT_PUMP_STOP, 0, 0);
    return true;
}

bool pump_driver_set_speed(uint8_t percent)
{
    if (!initialized) return false;
    if (percent > 100) percent = 100;

    pwm_hal_set_duty_active_low(PIN_PWM_MOTOR, percent);
    pump.speed_percent = percent;

    if (percent == 0) {
        pump.state = PUMP_STOPPED;
    } else if (pump.state != PUMP_RUNNING) {
        pump.state = PUMP_RUNNING;
    }

    return true;
}

bool pump_driver_set_direction(pump_direction_t dir)
{
    if (!initialized) return false;

    // Must stop before changing direction (safety)
    uint8_t prev_speed = pump.speed_percent;
    if (prev_speed > 0) {
        pump_driver_set_speed(0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    gpio_hal_write(PIN_MOTOR_DIR, (dir == PUMP_DIR_CW) ? GPIO_HAL_HIGH : GPIO_HAL_LOW);
    pump.direction = dir;

    // Restore speed after direction change
    if (prev_speed > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        pump_driver_set_speed(prev_speed);
    }

    return true;
}

/* ============================================================================
 *                         ADVANCED CONTROL
 * ============================================================================ */

bool pump_driver_run(uint8_t percent, pump_direction_t dir)
{
    if (!pump_driver_set_direction(dir)) return false;
    return pump_driver_start(percent);
}

bool pump_driver_soft_start(uint8_t target_percent, uint8_t step, uint32_t step_delay_ms)
{
    if (!initialized) return false;

    pump.state = PUMP_SOFT_STARTING;
    uint8_t current = pump.speed_percent;

    while (current < target_percent) {
        current += step;
        if (current > target_percent) current = target_percent;
        pump_driver_set_speed(current);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    pump.state = PUMP_RUNNING;
    return true;
}

bool pump_driver_soft_stop(uint8_t step, uint32_t step_delay_ms)
{
    if (!initialized) return false;

    pump.state = PUMP_SOFT_STOPPING;
    int16_t current = (int16_t)pump.speed_percent;

    while (current > 0) {
        current -= step;
        if (current < 0) current = 0;
        pump_driver_set_speed((uint8_t)current);
        vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
    }

    pump.state = PUMP_STOPPED;
    event_bus_publish(EVT_PUMP_STOP, 0, 0);
    return true;
}

/* ============================================================================
 *                         STATUS
 * ============================================================================ */

pump_status_t pump_driver_get_status(void)
{
    return pump;
}

bool pump_driver_is_running(void)
{
    return (pump.state == PUMP_RUNNING && pump.speed_percent > 0);
}

uint8_t pump_driver_get_speed(void)
{
    return pump.speed_percent;
}

/* ============================================================================
 *                         FG SIGNAL / RPM
 * ============================================================================ */

void pump_driver_update_rpm(void)
{
    // Atomic read-and-reset of RPM counter
    noInterrupts();
    uint32_t pulses   = rpm_pulse_counter;
    rpm_pulse_counter = 0;
    uint32_t period   = pulse_period_us;
    interrupts();

    if (!pump_driver_is_fg_active()) {
        pump.fg.motor_rpm     = 0;
        pump.fg.pump_head_rpm = 0;
        pump.fg.frequency_hz  = 0.0f;
        pump.fg.pulse_period_us = 0;
        return;
    }

    // RPM from pulse count over 1-second sample
    // motor_rpm = (pulses_per_second × 60) / encoder_ppr
    pump.fg.motor_rpm     = (pulses * 60) / PUMP_ENCODER_PPR;
    pump.fg.pump_head_rpm = (pulses * 60) / PUMP_FG_PER_REV;

    // Update total count
    pump.fg.pulse_count = (uint32_t)total_pulses;

    // Frequency from last pulse period
    pump.fg.pulse_period_us = period;
    if (period > 0) {
        pump.fg.frequency_hz = 1000000.0f / (float)period;
    }
}

pump_fg_data_t pump_driver_get_fg_data(void)
{
    return pump.fg;
}

uint32_t pump_driver_get_pulse_count(void)
{
    return (uint32_t)total_pulses;
}

uint32_t pump_driver_get_pump_head_rpm(void)
{
    return pump.fg.pump_head_rpm;
}

bool pump_driver_is_fg_active(void)
{
    return (millis() - last_pulse_tick_ms) < FG_TIMEOUT_MS;
}

bool pump_driver_check_motor_stall(void)
{
    // Motor stall: PWM is ON but no encoder pulses for >150ms
    // This indicates jammed motor or broken encoder
    if (!pump_driver_is_running()) return false;  // Not running, no stall
    
    uint32_t time_since_pulse = millis() - last_pulse_tick_ms;
    
    // Allow initial startup time (first 200ms after start)
    if (last_pulse_tick_ms == 0 && time_since_pulse < 200) return false;
    
    return (time_since_pulse > MOTOR_STALL_TIMEOUT_MS);
}

uint32_t pump_driver_get_time_since_last_pulse(void)
{
    return millis() - last_pulse_tick_ms;
}

/* ============================================================================
 *                         RESET
 * ============================================================================ */

void pump_driver_reset_pulses(void)
{
    noInterrupts();
    total_pulses = 0;
    pump.fg.pulse_count = 0;
    interrupts();
}

void pump_driver_reset_fg(void)
{
    noInterrupts();
    total_pulses       = 0;
    rpm_pulse_counter  = 0;
    last_pulse_time_us = 0;
    pulse_period_us    = 0;
    last_pulse_tick_ms = 0;
    interrupts();

    memset(&pump.fg, 0, sizeof(pump_fg_data_t));
}

/* ============================================================================
 *                         FLOW HELPERS (Integer arithmetic)
 * ============================================================================ */

uint32_t pump_driver_pulses_to_ul(uint32_t pulses)
{
    // microliters = pulses × 4375 / 86 (with rounding)
    return div_round(pulses * FLOW_UL_NUMERATOR, FLOW_UL_DENOMINATOR);
}

uint32_t pump_driver_ul_to_pulses(uint32_t microliters)
{
    // pulses = microliters × 86 / 4375 (with rounding)
    return div_round(microliters * FLOW_UL_DENOMINATOR, FLOW_UL_NUMERATOR);
}
