/**
 * @file    sensor_driver.cpp
 * @brief   Liquid level sensor driver implementation
 *
 * Debounce algorithm:
 *   - 2-sample shift register per sensor
 *   - State considered stable when reg[0] == reg[1]
 *   - Scan interval: 50ms (sufficient for liquid sensors)
 *
 * Fault Detection Matrix:
 *   - ERR_EMPTY_TANK: Both sensors dry >300ms while pumping
 *   - ERR_LEAKAGE: Inlet wet, outlet dry >500ms (or 2500ms in priming)
 *   - ERR_SENSOR_CONFLICT: Inlet dry, outlet wet (logic error)
 *   - WARN_AIR_BUBBLES: Inlet flickering (>3 transitions in 500ms)
 *
 * Event publishing:
 *   - EVT_SENSOR_INLET_WATER_OK / EVT_SENSOR_INLET_NO_WATER
 *   - EVT_SENSOR_OUTLET_WATER_OK / EVT_SENSOR_OUTLET_NO_WATER
 *   - EVT_ERR_EMPTY_TANK, EVT_ERR_LEAKAGE, EVT_ERR_SENSOR_CONFLICT
 *   - EVT_WARN_AIR_BUBBLES
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

// Fault monitoring state
static bool fault_monitoring_enabled = false;
static bool priming_mode = false;

// Timeout constants (ms)
#define EMPTY_TANK_TIMEOUT_MS       300     // Both dry → ERR_EMPTY_TANK
#define LEAKAGE_TIMEOUT_MS          500     // Inlet wet, outlet dry → ERR_LEAKAGE
#define LEAKAGE_PRIMING_TIMEOUT_MS  2500    // Extended timeout during priming
#define FLICKER_WINDOW_MS           500     // Window for flicker detection
#define FLICKER_THRESHOLD           3       // Transitions to trigger warning

// Fault detection timers
static uint32_t empty_tank_start_ms = 0;
static uint32_t leakage_start_ms = 0;
static bool empty_tank_active = false;
static bool leakage_active = false;

// Flicker detection for air bubbles
static uint8_t flicker_count = 0;
static uint32_t flicker_window_start_ms = 0;
static bool air_bubble_warning_sent = false;

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
    sensor_status.inlet_stability = SENSOR_STABLE;
    sensor_status.inlet_flicker_count = 0;
    
    // Initialize debounce registers
    inlet_debounce_reg = 0x03;
    outlet_debounce_reg = 0x03;
    
    prev_inlet_state = SENSOR_NO_WATER;
    prev_outlet_state = SENSOR_NO_WATER;
    
    // Reset fault detection
    fault_monitoring_enabled = false;
    priming_mode = false;
    empty_tank_start_ms = 0;
    leakage_start_ms = 0;
    empty_tank_active = false;
    leakage_active = false;
    flicker_count = 0;
    flicker_window_start_ms = 0;
    air_bubble_warning_sent = false;
    
    initialized = true;
    Serial.println("[SENSOR] Initialized (GPIO35=INLET, GPIO39=OUTLET)");
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
        
        // Track flicker count for air bubble detection
        if (fault_monitoring_enabled) {
            if (flicker_window_start_ms == 0) {
                flicker_window_start_ms = now;
                flicker_count = 1;
            } else if ((now - flicker_window_start_ms) < FLICKER_WINDOW_MS) {
                flicker_count++;
            } else {
                // Reset window
                flicker_window_start_ms = now;
                flicker_count = 1;
            }
            sensor_status.inlet_flicker_count = flicker_count;
        }
        
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
    
    // === FAULT DETECTION (only when monitoring enabled) ===
    if (!fault_monitoring_enabled) {
        // Reset timers when monitoring disabled
        empty_tank_start_ms = 0;
        leakage_start_ms = 0;
        return;
    }
    
    // --- AIR BUBBLE / FLICKER DETECTION ---
    if (flicker_count >= FLICKER_THRESHOLD && !air_bubble_warning_sent) {
        sensor_status.inlet_stability = SENSOR_FLICKERING;
        air_bubble_warning_sent = true;
        event_bus_publish(EVT_WARN_AIR_BUBBLES, flicker_count, 0);
        Serial.printf("[SENSOR] *** AIR BUBBLES DETECTED (%d transitions) ***\n", flicker_count);
    } else if (flicker_count < FLICKER_THRESHOLD) {
        sensor_status.inlet_stability = SENSOR_STABLE;
        // Allow re-triggering if bubbles return after clearing
        if ((now - flicker_window_start_ms) > FLICKER_WINDOW_MS) {
            air_bubble_warning_sent = false;
        }
    }
    
    // --- ERR_EMPTY_TANK: Both sensors dry ---
    if (sensor_status.inlet_state == SENSOR_NO_WATER && 
        sensor_status.outlet_state == SENSOR_NO_WATER) {
        
        if (empty_tank_start_ms == 0) {
            empty_tank_start_ms = now;
        } else if ((now - empty_tank_start_ms) > EMPTY_TANK_TIMEOUT_MS && !empty_tank_active) {
            empty_tank_active = true;
            event_bus_publish(EVT_ERR_EMPTY_TANK, 0, 0);
            Serial.println("[SENSOR] *** ERR_EMPTY_TANK: Both sensors DRY ***");
        }
    } else {
        empty_tank_start_ms = 0;
        if (empty_tank_active) {
            empty_tank_active = false;
            Serial.println("[SENSOR] Empty tank condition cleared");
        }
    }
    
    // --- ERR_LEAKAGE: Inlet wet, outlet dry (tube broken) ---
    uint32_t leakage_timeout = priming_mode ? LEAKAGE_PRIMING_TIMEOUT_MS : LEAKAGE_TIMEOUT_MS;
    
    if (sensor_status.inlet_state == SENSOR_HAS_WATER && 
        sensor_status.outlet_state == SENSOR_NO_WATER) {
        
        if (leakage_start_ms == 0) {
            leakage_start_ms = now;
        } else if ((now - leakage_start_ms) > leakage_timeout && !leakage_active) {
            leakage_active = true;
            event_bus_publish(EVT_ERR_LEAKAGE, 0, 0);
            Serial.println("[SENSOR] *** ERR_LEAKAGE: Tube broken/disconnected ***");
        }
    } else {
        leakage_start_ms = 0;
        if (leakage_active) {
            leakage_active = false;
            Serial.println("[SENSOR] Leakage condition cleared");
        }
    }
    
    // --- ERR_SENSOR_CONFLICT: Inlet dry, outlet wet (impossible state) ---
    // This is checked instantly, no timeout needed
    static bool sensor_conflict_sent = false;
    if (sensor_status.inlet_state == SENSOR_NO_WATER && 
        sensor_status.outlet_state == SENSOR_HAS_WATER) {
        // Only publish once per occurrence
        if (!sensor_conflict_sent) {
            sensor_conflict_sent = true;
            event_bus_publish(EVT_ERR_SENSOR_CONFLICT, 0, 0);
            Serial.println("[SENSOR] *** ERR_SENSOR_CONFLICT: Logic error ***");
        }
    } else {
        // Allow re-triggering if condition clears and returns
        sensor_conflict_sent = false;
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
    // Legacy function - now returns leakage_active
    return leakage_active;
}

void sensor_driver_enable_fault_monitoring(bool enable)
{
    fault_monitoring_enabled = enable;
    if (enable) {
        // Reset all fault timers when monitoring starts
        empty_tank_start_ms = 0;
        leakage_start_ms = 0;
        flicker_count = 0;
        flicker_window_start_ms = 0;
        air_bubble_warning_sent = false;
        Serial.println("[SENSOR] Fault monitoring ENABLED");
    } else {
        // Clear active faults when monitoring disabled
        empty_tank_active = false;
        leakage_active = false;
        Serial.println("[SENSOR] Fault monitoring DISABLED");
    }
}

bool sensor_driver_is_inlet_flickering(void)
{
    return (sensor_status.inlet_stability == SENSOR_FLICKERING);
}

bool sensor_driver_check_sensor_conflict(void)
{
    return (sensor_status.inlet_state == SENSOR_NO_WATER && 
            sensor_status.outlet_state == SENSOR_HAS_WATER);
}

bool sensor_driver_both_dry(void)
{
    return (sensor_status.inlet_state == SENSOR_NO_WATER && 
            sensor_status.outlet_state == SENSOR_NO_WATER);
}

void sensor_driver_set_priming_mode(bool enable)
{
    priming_mode = enable;
    if (enable) {
        // Reset leakage timer to give priming full timeout
        leakage_start_ms = 0;
        leakage_active = false;
        Serial.println("[SENSOR] Priming mode ENABLED (extended timeout)");
    } else {
        Serial.println("[SENSOR] Priming mode DISABLED");
    }
}
