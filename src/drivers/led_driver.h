/**
 * @file    led_driver.h
 * @brief   LED driver for status indication
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <Arduino.h>

/* ============================================================================
 *                         LED PATTERNS
 * ============================================================================ */

typedef enum {
    LED_OFF = 0,
    LED_ON,
    LED_BLINK_SLOW,         // 1 Hz (ready/idle)
    LED_BLINK_FAST,         // 5 Hz (dispensing)
    LED_BLINK_ERROR         // 10 Hz (error)
} led_pattern_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

void led_driver_init(void);
void led_driver_set_pattern(led_pattern_t pattern);
void led_driver_update(void);  // Call periodically from task

#endif // LED_DRIVER_H
