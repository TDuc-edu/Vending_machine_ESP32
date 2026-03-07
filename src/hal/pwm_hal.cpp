/**
 * @file    pwm_hal.cpp
 * @brief   PWM HAL implementation using ESP32 LEDC (Arduino 3.x / ESP-IDF 5.x API)
 *
 * Notes on 25kHz PWM:
 *   - APB_CLK = 80 MHz
 *   - 25kHz with 12-bit resolution: counter max = 80M / 25k = 3200
 *     12-bit max = 4096, so 3200 fits within 12-bit.
 *   - Actual effective range is 0–3200, but ledcWrite handles clamping.
 *
 * Active LOW pump logic:
 *   pump_power = 0%   → duty = 4095 (GPIO HIGH, pump OFF)
 *   pump_power = 100% → duty = 0    (GPIO LOW,  pump FULL ON)
 *   Formula: raw_duty = PWM_MAX_DUTY - (percent * PWM_MAX_DUTY / 100)
 */

#include "pwm_hal.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static uint32_t current_duty[40] = {0};  // Track duty per pin (ESP32 max GPIO = 39)

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

// Channel tracking: map pin → LEDC channel
static uint8_t pin_to_channel[40] = {0};
static uint8_t next_channel = 0;

bool pwm_hal_init(uint8_t pin)
{
    if (next_channel >= 16) {
        Serial.printf("[PWM_HAL] No free LEDC channels for GPIO%d\n", pin);
        return false;
    }

    uint8_t ch = next_channel++;
    pin_to_channel[pin] = ch;

    // Arduino ESP32 2.x API: ledcSetup + ledcAttachPin
    ledcSetup(ch, PWM_FREQUENCY, PWM_RESOLUTION_BITS);
    ledcAttachPin(pin, ch);

    // Start with duty = 0
    ledcWrite(ch, 0);
    current_duty[pin] = 0;

    Serial.printf("[PWM_HAL] GPIO%d → ch%d: %d Hz, %d-bit\n",
                  pin, ch, PWM_FREQUENCY, PWM_RESOLUTION_BITS);
    return true;
}

void pwm_hal_deinit(uint8_t pin)
{
    uint8_t ch = pin_to_channel[pin];
    ledcWrite(ch, 0);
    ledcDetachPin(pin);
    current_duty[pin] = 0;
}

void pwm_hal_set_duty_raw(uint8_t pin, uint32_t duty)
{
    if (duty > PWM_MAX_DUTY) {
        duty = PWM_MAX_DUTY;
    }
    uint8_t ch = pin_to_channel[pin];
    ledcWrite(ch, duty);
    current_duty[pin] = duty;
}

void pwm_hal_set_duty_percent(uint8_t pin, uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t raw = pwm_hal_percent_to_raw(percent);
    pwm_hal_set_duty_raw(pin, raw);
}

void pwm_hal_set_duty_active_low(uint8_t pin, uint8_t percent)
{
    if (percent > 100) percent = 100;

    // Active LOW inversion:
    //   percent=0   → raw=4095 (HIGH = pump OFF)
    //   percent=100 → raw=0    (LOW  = pump FULL ON)
    uint32_t raw = PWM_MAX_DUTY - pwm_hal_percent_to_raw(percent);
    pwm_hal_set_duty_raw(pin, raw);
}

uint32_t pwm_hal_get_duty_raw(uint8_t pin)
{
    if (pin < 40) {
        return current_duty[pin];
    }
    return 0;
}

uint32_t pwm_hal_percent_to_raw(uint8_t percent)
{
    if (percent >= 100) return PWM_MAX_DUTY;
    if (percent == 0)   return 0;

    // Use 32-bit multiplication to avoid overflow
    return (uint32_t)percent * PWM_MAX_DUTY / 100;
}
