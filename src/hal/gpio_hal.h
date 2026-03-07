/**
 * @file    gpio_hal.h
 * @brief   GPIO Hardware Abstraction Layer
 * @details Provides hardware-independent GPIO interface for ESP32-WROOM-32.
 *          All hardware-specific GPIO operations are abstracted here.
 *
 * Pin Assignment:
 *   PWM Motor    : GPIO25 (LEDC output)
 *   Motor DIR    : GPIO26
 *   FG Signal    : GPIO27 (Pulse counter input)
 *   BTN_0 (Hold) : GPIO32 (internal pull-up)
 *   BTN_1        : GPIO33 (internal pull-up)
 *   BTN_2        : GPIO34 (input only, external pull-up required)
 *   Sensor Inlet : GPIO35 (input only, liquid sensor before pump)
 *   Sensor Outlet: GPIO39 (input only, liquid sensor after pump)
 *   LCD I2C SDA  : GPIO21
 *   LCD I2C SCL  : GPIO22
 *   LED_STATUS   : GPIO2  (built-in LED)
 */

#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <Arduino.h>

/* ============================================================================
 *                         GPIO PIN DEFINITIONS
 * ============================================================================ */

// Motor / Pump control
#define PIN_PWM_MOTOR       25
#define PIN_MOTOR_DIR       26
#define PIN_FG_SIGNAL       27

// Liquid level sensors (NPN via 2N2222A level shifter)
// GPIO35, GPIO39 are input-only pins
#define PIN_SENSOR_INLET    35      // Sensor before pump (water inlet)
#define PIN_SENSOR_OUTLET   39      // Sensor after pump (water outlet)

// I2C for LCD2004A display
#define PIN_I2C_SDA         21
#define PIN_I2C_SCL         22

// Buttons (Active LOW with pull-up) - 3 buttons only
#define PIN_BTN_0           32      // Hold button (has internal pull-up)
#define PIN_BTN_1           33      // Volume button 1 (has internal pull-up)
#define PIN_BTN_2           34      // Volume button 2 (input only, need external pull-up)

// LED
#define PIN_LED_STATUS      2       // Built-in LED

// Button count
#define GPIO_BUTTON_COUNT   3

/* ============================================================================
 *                         GPIO MODES
 * ============================================================================ */

typedef enum {
    GPIO_HAL_INPUT = 0,
    GPIO_HAL_INPUT_PULLUP,
    GPIO_HAL_OUTPUT,
    GPIO_HAL_OUTPUT_OD
} gpio_hal_mode_t;

typedef enum {
    GPIO_HAL_LOW = 0,
    GPIO_HAL_HIGH = 1
} gpio_hal_level_t;

typedef enum {
    GPIO_HAL_EDGE_RISING = 0,
    GPIO_HAL_EDGE_FALLING,
    GPIO_HAL_EDGE_BOTH
} gpio_hal_edge_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize all GPIO pins used in the system
 */
void gpio_hal_init(void);

/**
 * @brief Configure a single GPIO pin mode
 * @param pin GPIO pin number
 * @param mode Pin mode (input, output, etc.)
 */
void gpio_hal_set_mode(uint8_t pin, gpio_hal_mode_t mode);

/**
 * @brief Write a digital level to a GPIO pin
 * @param pin GPIO pin number
 * @param level HIGH or LOW
 */
void gpio_hal_write(uint8_t pin, gpio_hal_level_t level);

/**
 * @brief Read a digital level from a GPIO pin
 * @param pin GPIO pin number
 * @return GPIO_HAL_HIGH or GPIO_HAL_LOW
 */
gpio_hal_level_t gpio_hal_read(uint8_t pin);

/**
 * @brief Toggle a GPIO pin output
 * @param pin GPIO pin number
 */
void gpio_hal_toggle(uint8_t pin);

/**
 * @brief Attach interrupt to a GPIO pin
 * @param pin GPIO pin number
 * @param isr ISR function pointer
 * @param edge Edge type (rising, falling, both)
 */
void gpio_hal_attach_interrupt(uint8_t pin, void (*isr)(void), gpio_hal_edge_t edge);

/**
 * @brief Detach interrupt from a GPIO pin
 * @param pin GPIO pin number
 */
void gpio_hal_detach_interrupt(uint8_t pin);

/**
 * @brief Get the array of button GPIO pin numbers
 * @return Pointer to button pin array
 */
const uint8_t* gpio_hal_get_button_pins(void);

#endif // GPIO_HAL_H
