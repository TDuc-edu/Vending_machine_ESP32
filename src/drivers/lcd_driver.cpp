/**
 * @file    lcd_driver.cpp
 * @brief   LCD2004A driver implementation via I2C
 *
 * Uses LiquidCrystal_I2C library for PCF8574-based LCD backpack.
 * Auto-detects I2C address (0x27 or 0x3F).
 */

#include "lcd_driver.h"
#include "../hal/gpio_hal.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static LiquidCrystal_I2C* lcd = nullptr;
static bool initialized = false;
static lcd_display_mode_t current_mode = LCD_MODE_STARTUP;
static uint8_t detected_addr = 0;

/* ============================================================================
 *                         I2C SCAN HELPER
 * ============================================================================ */

static uint8_t scan_i2c_lcd(void)
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    // Try common LCD addresses
    uint8_t addresses[] = {LCD_I2C_ADDR_1, LCD_I2C_ADDR_2};
    
    for (int i = 0; i < 2; i++) {
        Wire.beginTransmission(addresses[i]);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[LCD] Found I2C device at 0x%02X\n", addresses[i]);
            return addresses[i];
        }
    }
    
    // Full scan if not found
    Serial.println("[LCD] Scanning all I2C addresses...");
    for (uint8_t addr = 0x20; addr < 0x40; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[LCD] Found I2C device at 0x%02X\n", addr);
            return addr;
        }
    }
    
    return 0;
}

/* ============================================================================
 *                         PUBLIC FUNCTIONS - BASIC
 * ============================================================================ */

bool lcd_driver_init(void)
{
    // Scan for LCD
    detected_addr = scan_i2c_lcd();
    
    if (detected_addr == 0) {
        Serial.println("[LCD] ERROR: No LCD found on I2C bus!");
        return false;
    }
    
    // Create LCD instance
    lcd = new LiquidCrystal_I2C(detected_addr, LCD_COLS, LCD_ROWS);
    
    if (lcd == nullptr) {
        Serial.println("[LCD] ERROR: Failed to allocate LCD object!");
        return false;
    }
    
    // Initialize LCD
    lcd->init();
    lcd->backlight();
    lcd->clear();
    
    initialized = true;
    current_mode = LCD_MODE_STARTUP;
    
    Serial.printf("[LCD] Initialized (I2C addr=0x%02X, %dx%d)\n", 
                  detected_addr, LCD_COLS, LCD_ROWS);
    
    return true;
}

void lcd_driver_clear(void)
{
    if (!initialized || !lcd) return;
    lcd->clear();
}

void lcd_driver_set_cursor(uint8_t col, uint8_t row)
{
    if (!initialized || !lcd) return;
    if (col >= LCD_COLS || row >= LCD_ROWS) return;
    lcd->setCursor(col, row);
}

void lcd_driver_print(const char* text)
{
    if (!initialized || !lcd || !text) return;
    lcd->print(text);
}

void lcd_driver_print_at(uint8_t col, uint8_t row, const char* text)
{
    if (!initialized || !lcd || !text) return;
    if (col >= LCD_COLS || row >= LCD_ROWS) return;
    
    lcd->setCursor(col, row);
    lcd->print(text);
}

void lcd_driver_clear_row(uint8_t row)
{
    if (!initialized || !lcd) return;
    if (row >= LCD_ROWS) return;
    
    lcd->setCursor(0, row);
    lcd->print("                    ");  // 20 spaces
    lcd->setCursor(0, row);
}

void lcd_driver_backlight(bool on)
{
    if (!initialized || !lcd) return;
    if (on) {
        lcd->backlight();
    } else {
        lcd->noBacklight();
    }
}

/* ============================================================================
 *                     HIGH-LEVEL DISPLAY FUNCTIONS
 * ============================================================================ */

void lcd_driver_show_startup(void)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_STARTUP;
    lcd->clear();
    
    //                  "01234567890123456789"
    lcd_driver_print_at(0, 0, "== VENDING MACHINE ==");
    lcd_driver_print_at(0, 1, "  ESP32-WROOM-32    ");
    lcd_driver_print_at(0, 2, "  Initializing...   ");
    lcd_driver_print_at(0, 3, "  v1.0 FreeRTOS     ");
}

void lcd_driver_show_ready(void)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_READY;
    lcd->clear();
    
    //                  "01234567890123456789"
    lcd_driver_print_at(0, 0, "=== READY TO POUR ===");
    lcd_driver_print_at(0, 1, "BTN0:Hold BTN1:100ml");
    lcd_driver_print_at(0, 2, "BTN2:50ml           ");
    lcd_driver_print_at(0, 3, "IN:-- OUT:--        ");
}

void lcd_driver_show_dispensing(uint32_t target_ml, uint32_t current_ml, 
                                 uint8_t progress, uint8_t speed_percent)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_DISPENSING;
    
    char buf[22];
    
    // Row 0: Mode
    lcd_driver_print_at(0, 0, ">>> DISPENSING <<<  ");
    
    // Row 1: Progress bar
    lcd_driver_clear_row(1);
    snprintf(buf, sizeof(buf), "[");
    lcd->print(buf);
    
    // Progress bar (16 chars)
    int filled = (progress * 16) / 100;
    for (int i = 0; i < 16; i++) {
        lcd->print(i < filled ? "#" : "-");
    }
    snprintf(buf, sizeof(buf), "]%2d%%", progress);
    lcd->print(buf);
    
    // Row 2: Volume
    snprintf(buf, sizeof(buf), "%3lu/%3lu ml @%2d%%    ", 
             current_ml, target_ml, speed_percent);
    lcd_driver_print_at(0, 2, buf);
}

void lcd_driver_show_complete(uint32_t dispensed_ml)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_COMPLETE;
    
    char buf[22];
    
    lcd_driver_print_at(0, 0, "*** COMPLETE! ***   ");
    
    lcd_driver_clear_row(1);
    snprintf(buf, sizeof(buf), "Dispensed: %lu ml   ", dispensed_ml);
    lcd_driver_print_at(0, 1, buf);
    
    lcd_driver_print_at(0, 2, "                    ");
    lcd_driver_print_at(0, 3, "Ready for next...   ");
}

void lcd_driver_show_error(const char* error_msg)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_ERROR;
    
    lcd_driver_print_at(0, 0, "!!! ERROR !!!       ");
    
    lcd_driver_clear_row(1);
    if (error_msg) {
        char buf[22];
        snprintf(buf, sizeof(buf), "%-20s", error_msg);
        lcd_driver_print_at(0, 1, buf);
    }
    
    lcd_driver_print_at(0, 2, "Check system!       ");
    lcd_driver_print_at(0, 3, "Auto-reset in 5s... ");
}

void lcd_driver_show_hold_mode(uint8_t speed_percent, uint32_t pulses)
{
    if (!initialized || !lcd) return;
    
    current_mode = LCD_MODE_HOLD_PUMP;
    
    char buf[22];
    
    lcd_driver_print_at(0, 0, ">>> HOLD MODE <<<   ");
    
    snprintf(buf, sizeof(buf), "Speed: %3d%%         ", speed_percent);
    lcd_driver_print_at(0, 1, buf);
    
    // Calculate approximate volume from pulses
    // volume_ul = pulses * 4375 / 86
    uint32_t volume_ul = (pulses * 4375UL) / 86UL;
    uint32_t volume_ml = volume_ul / 1000;
    uint32_t volume_frac = (volume_ul % 1000) / 10;
    
    snprintf(buf, sizeof(buf), "Vol: %3lu.%02lu ml      ", volume_ml, volume_frac);
    lcd_driver_print_at(0, 2, buf);
}

void lcd_driver_update_sensors(bool inlet_ok, bool outlet_ok)
{
    if (!initialized || !lcd) return;
    
    char buf[22];
    snprintf(buf, sizeof(buf), "IN:%s OUT:%s        ",
             inlet_ok ? "OK" : "--",
             outlet_ok ? "OK" : "--");
    lcd_driver_print_at(0, 3, buf);
}

void lcd_driver_update_info(const char* info)
{
    if (!initialized || !lcd || !info) return;
    
    lcd_driver_clear_row(3);
    char buf[22];
    snprintf(buf, sizeof(buf), "%-20s", info);
    lcd_driver_print_at(0, 3, buf);
}

lcd_display_mode_t lcd_driver_get_mode(void)
{
    return current_mode;
}
