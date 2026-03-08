/**
 * @file    gpio_hal.cpp
 * @brief   GPIO Hardware Abstraction Layer implementation
 */

#include "gpio_hal.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static const uint8_t button_pins[GPIO_BUTTON_COUNT] = {
    PIN_BTN_0, PIN_BTN_1, PIN_BTN_2
};

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

void gpio_hal_init(void)
{
    // Motor control pins
    pinMode(PIN_PWM_MOTOR, OUTPUT);
    pinMode(PIN_MOTOR_DIR, OUTPUT);
    digitalWrite(PIN_MOTOR_DIR, HIGH);  // Default: CW

    pinMode(PIN_FG_SIGNAL, INPUT);     //pull up 10k - 3v3

    // Liquid level sensors on GPIO35, GPIO39 (input-only, no pull-up capability)
    // External pull-up via 2N2222A transistor circuit
    pinMode(PIN_SENSOR_INLET, INPUT);
    pinMode(PIN_SENSOR_OUTLET, INPUT);

    // I2C pins for LCD (will be configured by Wire library)
    // pinMode(PIN_I2C_SDA, OUTPUT_OPEN_DRAIN);  // Handled by Wire.begin()
    // pinMode(PIN_I2C_SCL, OUTPUT_OPEN_DRAIN);

    // Button pins - only 3 buttons now
    // GPIO32, GPIO33 have internal pull-up capability
    pinMode(PIN_BTN_0, INPUT_PULLUP);
    pinMode(PIN_BTN_1, INPUT_PULLUP);
    // GPIO34 is input-only — need external pull-up resistor (10kΩ to 3.3V)
    pinMode(PIN_BTN_2, INPUT);

    // LED status
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);
}

void gpio_hal_set_mode(uint8_t pin, gpio_hal_mode_t mode)
{
    switch (mode) {
        case GPIO_HAL_INPUT:
            pinMode(pin, INPUT);
            break;
        case GPIO_HAL_INPUT_PULLUP:
            pinMode(pin, INPUT_PULLUP);
            break;
        case GPIO_HAL_OUTPUT:
            pinMode(pin, OUTPUT);
            break;
        case GPIO_HAL_OUTPUT_OD:
            pinMode(pin, OUTPUT_OPEN_DRAIN);
            break;
    }
}

void gpio_hal_write(uint8_t pin, gpio_hal_level_t level)
{
    digitalWrite(pin, (level == GPIO_HAL_HIGH) ? HIGH : LOW);
}

gpio_hal_level_t gpio_hal_read(uint8_t pin)
{
    return (digitalRead(pin) == HIGH) ? GPIO_HAL_HIGH : GPIO_HAL_LOW;
}

void gpio_hal_toggle(uint8_t pin)
{
    digitalWrite(pin, !digitalRead(pin));
}

void gpio_hal_attach_interrupt(uint8_t pin, void (*isr)(void), gpio_hal_edge_t edge)
{
    int mode;
    switch (edge) {
        case GPIO_HAL_EDGE_RISING:  mode = RISING;  break;
        case GPIO_HAL_EDGE_FALLING: mode = FALLING; break;
        case GPIO_HAL_EDGE_BOTH:    mode = CHANGE;  break;
        default:                    mode = RISING;   break;
    }
    attachInterrupt(digitalPinToInterrupt(pin), isr, mode);
}

void gpio_hal_detach_interrupt(uint8_t pin)
{
    detachInterrupt(digitalPinToInterrupt(pin));
}

const uint8_t* gpio_hal_get_button_pins(void)
{
    return button_pins;
}
