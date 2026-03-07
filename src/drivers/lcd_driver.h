/**
 * @file    lcd_driver.h
 * @brief   LCD2004A (20x4) display driver via I2C
 * @details Uses PCF8574 I2C expander backpack at address 0x27 (or 0x3F).
 *          Provides text display functions for vending machine status.
 *
 * Display layout (20 columns x 4 rows):
 *   Row 0: Status / Mode
 *   Row 1: Volume info
 *   Row 2: Sensor status
 *   Row 3: System info / Errors
 *
 * I2C pins:
 *   SDA: GPIO21
 *   SCL: GPIO22
 */

#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <Arduino.h>

/* ============================================================================
 *                         LCD CONFIGURATION
 * ============================================================================ */

#define LCD_COLS            20
#define LCD_ROWS            4
#define LCD_I2C_ADDR_1      0x27    // Common PCF8574 address
#define LCD_I2C_ADDR_2      0x3F    // Alternative PCF8574A address

/* ============================================================================
 *                         DISPLAY MODES
 * ============================================================================ */

typedef enum {
    LCD_MODE_STARTUP,       // Showing boot message
    LCD_MODE_READY,         // Ready to dispense
    LCD_MODE_DISPENSING,    // Currently dispensing
    LCD_MODE_COMPLETE,      // Dispense complete
    LCD_MODE_ERROR,         // Error state
    LCD_MODE_HOLD_PUMP      // Hold pump mode active
} lcd_display_mode_t;

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize LCD display
 * @return true if LCD detected and initialized
 */
bool lcd_driver_init(void);

/**
 * @brief Clear entire display
 */
void lcd_driver_clear(void);

/**
 * @brief Set cursor position
 * @param col Column (0-19)
 * @param row Row (0-3)
 */
void lcd_driver_set_cursor(uint8_t col, uint8_t row);

/**
 * @brief Print text at current cursor position
 * @param text Text to display
 */
void lcd_driver_print(const char* text);

/**
 * @brief Print text at specific position
 * @param col Column (0-19)
 * @param row Row (0-3)
 * @param text Text to display
 */
void lcd_driver_print_at(uint8_t col, uint8_t row, const char* text);

/**
 * @brief Clear a specific row
 * @param row Row to clear (0-3)
 */
void lcd_driver_clear_row(uint8_t row);

/**
 * @brief Control backlight
 * @param on true = backlight on, false = off
 */
void lcd_driver_backlight(bool on);

/* ============================================================================
 *                     HIGH-LEVEL DISPLAY FUNCTIONS
 * ============================================================================ */

/**
 * @brief Show startup/boot screen
 */
void lcd_driver_show_startup(void);

/**
 * @brief Show ready state with button options
 */
void lcd_driver_show_ready(void);

/**
 * @brief Update dispensing progress
 * @param target_ml Target volume in ml
 * @param current_ml Current dispensed volume in ml
 * @param progress Percentage (0-100)
 * @param speed_percent Pump speed percent
 */
void lcd_driver_show_dispensing(uint32_t target_ml, uint32_t current_ml, 
                                 uint8_t progress, uint8_t speed_percent);

/**
 * @brief Show dispense complete
 * @param dispensed_ml Final dispensed volume
 */
void lcd_driver_show_complete(uint32_t dispensed_ml);

/**
 * @brief Show error message
 * @param error_msg Error description
 */
void lcd_driver_show_error(const char* error_msg);

/**
 * @brief Show hold pump mode active
 * @param speed_percent Current pump speed
 * @param pulses Current pulse count
 */
void lcd_driver_show_hold_mode(uint8_t speed_percent, uint32_t pulses);

/**
 * @brief Update sensor status display
 * @param inlet_ok Inlet sensor has water
 * @param outlet_ok Outlet sensor has water
 */
void lcd_driver_update_sensors(bool inlet_ok, bool outlet_ok);

/**
 * @brief Update system info line (row 3)
 * @param info Info text
 */
void lcd_driver_update_info(const char* info);

/**
 * @brief Get current display mode
 * @return Current lcd_display_mode_t
 */
lcd_display_mode_t lcd_driver_get_mode(void);

#endif // LCD_DRIVER_H
