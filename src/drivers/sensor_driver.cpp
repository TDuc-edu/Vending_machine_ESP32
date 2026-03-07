/**
 * @file    sensor_driver.cpp
 * @brief   Liquid level sensor driver implementation
 *
 * Debounce algorithm:
 *   - 2-sample shift register per sensor
 *   - State considered stable when reg[0] == reg[1]
 *   - Scan interval: 50ms (sufficient for liquid sensors)
 *
 * Event publishing:
 *   - EVT_SENSOR_INLET_WATER_OK / EVT_SENSOR_INLET_NO_WATER
 *   - EVT_SENSOR_OUTLET_WATER_OK / EVT_SENSOR_OUTLET_NO_WATER
 *   - EVT_SENSOR_TUBE_FAULT (inlet has water, outlet missing water >500ms)
 */

#include "sensor_driver.h"
#include "../hal/gpio_hal.h"
#include "../system/event_bus.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static sensor_status_t sensor_status;
static bool initialized = false;

// Debounce shift registers (2 samples)
static uint8_t inlet_debounce_reg  = 0x03;  // Init HIGH (no water)
static uint8_t outlet_debounce_reg = 0x03;  // Init HIGH (no water)

// Previous stable states (for edge detection)
static sensor_state_t prev_inlet_state  = SENSOR_NO_WATER;
static sensor_state_t prev_outlet_state = SENSOR_NO_WATER;

// Tube fault detection
#define TUBE_FAULT_TIMEOUT_MS  500      // 500ms of sensor mismatch = fault
static uint32_t tube_fault_start_ms = 0;
static bool tube_fault_active = false;

/* ============================================================================
 *                         PRIVATE HELPERS
 * ============================================================================ */

/**
 * @brief Read raw sensor GPIO and return state
 * @param sensor_id SENSOR_INLET or SENSOR_OUTLET
 * @return SENSOR_HAS_WATER (HIGH) or SENSOR_NO_WATER (LOW)
 */
static sensor_state_t read_sensor_raw(sensor_id_t sensor_id)
{
    uint8_t pin = (sensor_id == SENSOR_INLET) ? PIN_SENSOR_INLET : PIN_SENSOR_OUTLET;
    gpio_hal_level_t level = gpio_hal_read(pin);
    
    // Transistor inverts logic: GPIO HIGH = water detected, GPIO LOW = no water
    return (level == GPIO_HAL_HIGH) ? SENSOR_HAS_WATER : SENSOR_NO_WATER;
}

/**
 * @brief Debounce a sensor using 2-sample shift register
 * @param reg Pointer to debounce register
 * @param raw_state Current raw reading (0 or 1)
 * @return Debounced state (stable when both samples match)
 */
static sensor_state_t debounce_sensor(uint8_t* reg, sensor_state_t raw_state)
{
    // Shift register: reg[1] ← reg[0] ← raw_state
    *reg = (*reg << 1) | (raw_state & 0x01);
    *reg &= 0x03;  // Keep only 2 bits
    
    // Both samples must match for stable state
    if ((*reg & 0x03) == 0x00) return SENSOR_NO_WATER;   // Both 0
    if ((*reg & 0x03) == 0x03) return SENSOR_HAS_WATER;  // Both 1
    
    // Unstable: return previous stable state
    return (sensor_state_t)((*reg >> 1) & 0x01);
}

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

bool sensor_driver_init(void)
{
    // GPIO pins already initialized in gpio_hal_init()
    
    // Reset status
    memset(&sensor_status, 0, sizeof(sensor_status_t));
    sensor_status.inlet_state = SENSOR_NO_WATER;
    sensor_status.outlet_state = SENSOR_NO_WATER;
    sensor_status.inlet_stable = false;
    sensor_status.outlet_stable = false;
    
    // Initialize debounce registers
    inlet_debounce_reg = 0x03;
    outlet_debounce_reg = 0x03;
    
    prev_inlet_state = SENSOR_NO_WATER;
    prev_outlet_state = SENSOR_NO_WATER;
    
    tube_fault_start_ms = 0;
    tube_fault_active = false;
    
    initialized = true;
    Serial.println("[SENSOR] Initialized (GPIO21=INLET, GPIO22=OUTLET)");
    return true;
}

void sensor_driver_deinit(void)
{
    initialized = false;
}

void sensor_driver_scan(void)
{
    if (!initialized) return;
    
    uint32_t now = millis();
    
    // === INLET SENSOR (before pump) ===
    sensor_state_t raw_inlet = read_sensor_raw(SENSOR_INLET);
    sensor_state_t debounced_inlet = debounce_sensor(&inlet_debounce_reg, raw_inlet);
    
    sensor_status.inlet_stable = ((inlet_debounce_reg & 0x03) == 0x00) || 
                                  ((inlet_debounce_reg & 0x03) == 0x03);
    
    // Detect state change
    if (debounced_inlet != prev_inlet_state && sensor_status.inlet_stable) {
        sensor_status.inlet_state = debounced_inlet;
        sensor_status.inlet_last_change_ms = now;
        prev_inlet_state = debounced_inlet;
        
        // Publish event
        if (debounced_inlet == SENSOR_HAS_WATER) {
            event_bus_publish(EVT_SENSOR_INLET_WATER_OK, 0, 0);
            Serial.println("[SENSOR] INLET: Water OK");
        } else {
            event_bus_publish(EVT_SENSOR_INLET_NO_WATER, 0, 0);
            Serial.println("[SENSOR] INLET: No Water");
        }
    }
    
    // === OUTLET SENSOR (after pump) ===
    sensor_state_t raw_outlet = read_sensor_raw(SENSOR_OUTLET);
    sensor_state_t debounced_outlet = debounce_sensor(&outlet_debounce_reg, raw_outlet);
    
    sensor_status.outlet_stable = ((outlet_debounce_reg & 0x03) == 0x00) || 
                                   ((outlet_debounce_reg & 0x03) == 0x03);
    
    // Detect state change
    if (debounced_outlet != prev_outlet_state && sensor_status.outlet_stable) {
        sensor_status.outlet_state = debounced_outlet;
        sensor_status.outlet_last_change_ms = now;
        prev_outlet_state = debounced_outlet;
        
        // Publish event
        if (debounced_outlet == SENSOR_HAS_WATER) {
            event_bus_publish(EVT_SENSOR_OUTLET_WATER_OK, 0, 0);
            Serial.println("[SENSOR] OUTLET: Water OK");
        } else {
            event_bus_publish(EVT_SENSOR_OUTLET_NO_WATER, 0, 0);
            Serial.println("[SENSOR] OUTLET: No Water");
        }
    }
    
    // === TUBE FAULT DETECTION ===
    // Fault condition: Inlet has water BUT outlet has no water for >500ms
    if (sensor_status.inlet_state == SENSOR_HAS_WATER && 
        sensor_status.outlet_state == SENSOR_NO_WATER) {
        
        if (tube_fault_start_ms == 0) {
            tube_fault_start_ms = now;
        } else if ((now - tube_fault_start_ms) > TUBE_FAULT_TIMEOUT_MS && !tube_fault_active) {
            // Fault confirmed
            tube_fault_active = true;
            event_bus_publish(EVT_SENSOR_TUBE_FAULT, 0, 0);
            Serial.println("[SENSOR] *** TUBE FAULT DETECTED ***");
        }
    } else {
        // No fault condition: reset timer
        tube_fault_start_ms = 0;
        if (tube_fault_active) {
            tube_fault_active = false;
            Serial.println("[SENSOR] Tube fault cleared");
        }
    }
}

sensor_state_t sensor_driver_get_state(sensor_id_t sensor_id)
{
    if (!initialized) return SENSOR_NO_WATER;
    
    return (sensor_id == SENSOR_INLET) ? sensor_status.inlet_state : sensor_status.outlet_state;
}

bool sensor_driver_both_have_water(void)
{
    if (!initialized) return false;
    
    return (sensor_status.inlet_state == SENSOR_HAS_WATER && 
            sensor_status.outlet_state == SENSOR_HAS_WATER);
}

const sensor_status_t* sensor_driver_get_status(void)
{
    return &sensor_status;
}

bool sensor_driver_check_tube_fault(void)
{
    return tube_fault_active;
}
