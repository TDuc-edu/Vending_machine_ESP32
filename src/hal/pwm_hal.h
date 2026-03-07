/**
 * @file    pwm_hal.h
 * @brief   PWM Hardware Abstraction Layer using ESP32 LEDC peripheral
 * @details Uses ESP32 LEDC for 25kHz PWM generation.
 *          Active LOW pump: duty is inverted so 0% = OFF (GPIO HIGH), 100% = FULL (GPIO LOW).
 *
 * ESP32 LEDC has 16 channels (8 high-speed, 8 low-speed).
 * Resolution: 12-bit (0–4095) at 25kHz.
 *
 * Frequency calculation:
 *   max_resolution = log2(APB_CLK / freq) = log2(80MHz / 25kHz) = log2(3200) ≈ 11.6
 *   We use 12-bit which is fine for 25kHz.
 */

#ifndef PWM_HAL_H
#define PWM_HAL_H

#include <Arduino.h>

/* ============================================================================
 *                         PWM CONFIGURATION
 * ============================================================================ */

#define PWM_FREQUENCY       25000   // 25 kHz — optimal for pump motor
#define PWM_RESOLUTION_BITS 10      // 10-bit resolution (12-bit không đạt được với 25kHz)
#define PWM_MAX_DUTY        1023    // (2^10 - 1)

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize PWM on a specific GPIO pin
 * @param pin GPIO pin number
 * @return true if successful
 */
bool pwm_hal_init(uint8_t pin);

/**
 * @brief Deinitialize PWM on a pin
 * @param pin GPIO pin number
 */
void pwm_hal_deinit(uint8_t pin);

/**
 * @brief Set PWM duty cycle (raw 0–4095)
 * @param pin GPIO pin previously initialized
 * @param duty Raw duty value (0 = 0%, 4095 = 100%)
 */
void pwm_hal_set_duty_raw(uint8_t pin, uint32_t duty);

/**
 * @brief Set PWM duty cycle by percent (0–100)
 * @param pin GPIO pin
 * @param percent 0–100
 */
void pwm_hal_set_duty_percent(uint8_t pin, uint8_t percent);

/**
 * @brief Set PWM duty cycle for active-LOW device (inverted)
 *        0% → GPIO stays HIGH (pump OFF)
 *        100% → GPIO stays LOW (pump FULL ON)
 * @param pin GPIO pin
 * @param percent Power level 0–100
 */
void pwm_hal_set_duty_active_low(uint8_t pin, uint8_t percent);

/**
 * @brief Get current raw duty value on a pin
 * @param pin GPIO pin
 * @return Raw duty value 0–4095
 */
uint32_t pwm_hal_get_duty_raw(uint8_t pin);

/**
 * @brief Convert percent to raw duty value
 * @param percent 0–100
 * @return Raw duty 0–4095
 */
uint32_t pwm_hal_percent_to_raw(uint8_t percent);

#endif // PWM_HAL_H
