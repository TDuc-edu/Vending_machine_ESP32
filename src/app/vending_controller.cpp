/**
 * @file    vending_controller.cpp
 * @brief   Vending controller — event-driven state machine
 *
 * Architecture:
 *   1) Reads events from event_bus
 *   2) Routes events to state machine transitions
 *   3) Controls pump_driver based on recipe_engine presets
 *   4) Updates system_state for telemetry
 *
 * Flow control (volume mode):
 *   - On START: pump_driver_reset_pulses(), pump_driver_start()
 *   - Each update: read pulse count → calculate volume → check target
 *   - On COMPLETE: pump_driver_soft_stop(), publish EVT_FLOW_COMPLETE
 *
 * Integer math for flow (from migration guide):
 *   volume_ul = pulses × 4375 / 86  (with rounding)
 *   pulses    = volume_ul × 86 / 4375
 */

#include "vending_controller.h"
#include "recipe_engine.h"
#include "../drivers/pump_driver.h"
#include "../drivers/button_driver.h"
#include "../drivers/led_driver.h"
#include "../drivers/lcd_driver.h"
#include "../drivers/sensor_driver.h"
#include "../system/event_bus.h"
#include "../system/system_state.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static vend_state_t current_state = VEND_STATE_IDLE;
static uint32_t state_enter_ms   = 0;

// Flow tracking for volume mode
static uint32_t target_pulses    = 0;
static uint32_t target_volume_ul = 0;
static uint32_t start_pulses     = 0;
static uint8_t  dispense_speed   = 0;

// Error tracking
static uint32_t no_fg_count      = 0;
static uint32_t last_stall_check_ms = 0;
#define NO_FG_ERROR_THRESHOLD    50  // 50 × 10ms = 500ms without FG signal
#define STALL_CHECK_INTERVAL_MS  20  // Check motor stall every 20ms

// Error codes for RS485 reporting
typedef enum {
    ERR_CODE_NONE = 0,
    ERR_CODE_EMPTY_TANK,
    ERR_CODE_LEAKAGE,
    ERR_CODE_MOTOR_STALL,
    ERR_CODE_SENSOR_CONFLICT
} error_code_t;
static error_code_t last_error_code = ERR_CODE_NONE;

static const char* STATE_NAMES[] = {
    "IDLE", "HOLD_PUMP", "DISPENSING", "COMPLETE", "PRIMING", "ERROR", "LOCKDOWN"
};

/* ============================================================================
 *                         PRIVATE HELPERS
 * ============================================================================ */

static void transition_to(vend_state_t new_state)
{
    if (new_state == current_state) return;

    uint32_t duration = millis() - state_enter_ms;
    Serial.printf("[VEND] %s -> %s (after %lums)\n",
                  STATE_NAMES[current_state], STATE_NAMES[new_state], duration);

    current_state  = new_state;
    state_enter_ms = millis();

    // Update system state
    switch (new_state) {
        case VEND_STATE_IDLE:
            system_state_set(SYS_STATE_READY);
            led_driver_set_pattern(LED_BLINK_SLOW);
            lcd_driver_show_ready();
            sensor_driver_enable_fault_monitoring(false);
            sensor_driver_set_priming_mode(false);
            last_error_code = ERR_CODE_NONE;
            break;
        case VEND_STATE_HOLD_PUMP:
            // HOLD mode = Manual priming - NO fault monitoring
            // User controls pump manually, will release when done
            // This allows priming empty tubes without sensor errors
            system_state_set(SYS_STATE_HOLD_PUMP);
            led_driver_set_pattern(LED_ON);
            sensor_driver_enable_fault_monitoring(false);  // Disabled for manual control
            lcd_driver_update_info("HOLD: Priming...");
            break;
        case VEND_STATE_DISPENSING:
            system_state_set(SYS_STATE_DISPENSING);
            led_driver_set_pattern(LED_BLINK_FAST);
            sensor_driver_enable_fault_monitoring(true);
            break;
        case VEND_STATE_PRIMING:
            system_state_set(SYS_STATE_DISPENSING);
            led_driver_set_pattern(LED_BLINK_FAST);
            sensor_driver_enable_fault_monitoring(true);
            sensor_driver_set_priming_mode(true);
            lcd_driver_update_info("Priming...");
            break;
        case VEND_STATE_ERROR:
            system_state_set(SYS_STATE_ERROR);
            led_driver_set_pattern(LED_BLINK_ERROR);
            sensor_driver_enable_fault_monitoring(false);
            // LCD error message set by caller
            break;
        case VEND_STATE_LOCKDOWN:
            system_state_set(SYS_STATE_ERROR);
            led_driver_set_pattern(LED_BLINK_ERROR);
            sensor_driver_enable_fault_monitoring(false);
            lcd_driver_show_error("LOCKDOWN!");
            Serial.println("[VEND] *** SYSTEM LOCKDOWN - TECHNICIAN REQUIRED ***");
            break;
        default:
            break;
    }
}

static void start_volume_dispense(uint32_t volume_ml, uint8_t speed_percent)
{
    target_volume_ul = volume_ml * 1000UL;
    target_pulses    = pump_driver_ul_to_pulses(target_volume_ul);
    dispense_speed   = speed_percent;
    no_fg_count      = 0;

    pump_driver_reset_pulses();
    start_pulses = pump_driver_get_pulse_count();

    pump_driver_set_direction(PUMP_DIR_CW);
    pump_driver_start(speed_percent);

    system_state_update_flow(target_volume_ul, 0, 0);

    Serial.printf("[VEND] Dispensing: %lu ml (%lu ul, %lu pulses) @ %d%%\n",
                  volume_ml, target_volume_ul, target_pulses, speed_percent);

    // Update LCD
    lcd_driver_show_dispensing(volume_ml, 0, 0, speed_percent);

    transition_to(VEND_STATE_DISPENSING);
}

static void start_hold_pump(uint8_t speed_percent)
{
    pump_driver_reset_pulses();
    pump_driver_set_direction(PUMP_DIR_CW);
    pump_driver_start(speed_percent);

    Serial.printf("[VEND] HOLD mode @ %d%%\n", speed_percent);
    lcd_driver_show_hold_mode(speed_percent, 0);
    transition_to(VEND_STATE_HOLD_PUMP);
}

static void stop_dispense(void)
{
    // Calculate what was actually dispensed
    uint32_t pulses = pump_driver_get_pulse_count() - start_pulses;
    uint32_t volume_ul = pump_driver_pulses_to_ul(pulses);
    uint32_t volume_ml = (volume_ul + 500) / 1000;  // Round to ml

    pump_driver_soft_stop(20, 10);

    system_state_add_dispense(volume_ml);
    system_state_update_flow(target_volume_ul, volume_ul, 100);

    Serial.printf("[VEND] Dispensed: %lu.%03lu ml (%lu pulses)\n",
                  volume_ul / 1000, volume_ul % 1000, pulses);

    // Show completion on LCD
    lcd_driver_show_complete(volume_ml);

    event_bus_publish(EVT_FLOW_COMPLETE, volume_ml, 0);
}

/* ============================================================================
 *                         EVENT HANDLERS
 * ============================================================================ */

static void handle_button_pressed(uint32_t btn_index)
{
    if (current_state != VEND_STATE_IDLE) return;

    const recipe_t* recipe = recipe_engine_get(btn_index);
    if (recipe == NULL) return;

    Serial.printf("[VEND] Button %lu pressed → recipe: %s\n", btn_index, recipe->name);

    if (recipe->mode == RECIPE_MODE_VOLUME) {
        start_volume_dispense(recipe->volume_ml, recipe->speed_percent);
    }
    // Hold mode is triggered by long press, not single press
}

static void handle_button_long_pressed(uint32_t btn_index)
{
    if (current_state != VEND_STATE_IDLE) return;

    const recipe_t* recipe = recipe_engine_get(btn_index);
    if (recipe == NULL) return;

    if (recipe->mode == RECIPE_MODE_HOLD) {
        start_hold_pump(recipe->speed_percent);
    }
}

static void handle_button_released(uint32_t btn_index)
{
    // Release BTN_0 stops hold-pump mode or priming mode
    if ((current_state == VEND_STATE_HOLD_PUMP || current_state == VEND_STATE_PRIMING) && 
        btn_index == BTN_IDX_HOLD) {
        pump_driver_soft_stop(20, 20);
        Serial.println("[VEND] Hold released, stopping pump");
        sensor_driver_set_priming_mode(false);
        transition_to(VEND_STATE_IDLE);
    }
}

static void handle_mqtt_dispense(uint32_t volume_ml)
{
    if (current_state != VEND_STATE_IDLE) {
        Serial.println("[VEND] Cannot dispense: system busy");
        return;
    }

    uint8_t speed = 70;  // Default MQTT dispense speed
    start_volume_dispense(volume_ml, speed);
}

/* ============================================================================
 *                         FLOW MONITORING (called during DISPENSING)
 * ============================================================================ */

static void update_flow_monitoring(void)
{
    uint32_t current_pulses = pump_driver_get_pulse_count();
    uint32_t dispensed_pulses = 0;

    if (current_pulses >= start_pulses) {
        dispensed_pulses = current_pulses - start_pulses;
    } else {
        dispensed_pulses = current_pulses;  // Overflow protection (unlikely)
    }

    // Calculate volume using integer math (no float accumulation error)
    uint32_t dispensed_ul = pump_driver_pulses_to_ul(dispensed_pulses);

    // Calculate progress
    uint8_t progress = 0;
    if (target_pulses > 0) {
        progress = (uint8_t)((dispensed_pulses * 100) / target_pulses);
        if (progress > 100) progress = 100;
    }

    // Update system state for telemetry
    system_state_update_flow(target_volume_ul, dispensed_ul, progress);

    // Check if target reached
    if (dispensed_pulses >= target_pulses) {
        stop_dispense();
        transition_to(VEND_STATE_COMPLETE);
        return;
    }

    // Check for FG signal error (pump running but no pulses)
    if (pump_driver_is_running() && !pump_driver_is_fg_active()) {
        no_fg_count++;
        if (no_fg_count >= NO_FG_ERROR_THRESHOLD) {
            Serial.println("[VEND] ERROR: No FG signal! Stopping pump.");
            pump_driver_stop();
            event_bus_publish(EVT_FLOW_ERROR, 0, 0);
            transition_to(VEND_STATE_ERROR);
            no_fg_count = 0;
            return;
        }
    } else {
        no_fg_count = 0;
    }

    // Periodic progress log and LCD update
    static uint32_t last_log = 0;
    if (millis() - last_log >= 250) {  // Update every 250ms
        last_log = millis();
        
        // Update LCD display
        uint32_t target_ml = target_volume_ul / 1000;
        uint32_t current_ml = dispensed_ul / 1000;
        lcd_driver_show_dispensing(target_ml, current_ml, progress, dispense_speed);
        
        // Update sensor status on LCD
        bool inlet_ok = (sensor_driver_get_state(SENSOR_INLET) == SENSOR_HAS_WATER);
        bool outlet_ok = (sensor_driver_get_state(SENSOR_OUTLET) == SENSOR_HAS_WATER);
        lcd_driver_update_sensors(inlet_ok, outlet_ok);
        
        // Log to serial every 500ms
        static uint32_t last_serial_log = 0;
        if (millis() - last_serial_log >= 500) {
            last_serial_log = millis();
            Serial.printf("[%3d%%] %lu.%03lu / %lu.%03lu ml\\n",
                          progress,
                          dispensed_ul / 1000, dispensed_ul % 1000,
                          target_volume_ul / 1000, target_volume_ul % 1000);
        }
    }
}

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

void vending_controller_init(void)
{
    current_state  = VEND_STATE_IDLE;
    state_enter_ms = millis();
    target_pulses  = 0;
    target_volume_ul = 0;
    start_pulses   = 0;
    no_fg_count    = 0;

    led_driver_set_pattern(LED_BLINK_SLOW);
    Serial.println("[VEND] Controller initialized — IDLE");
}

void vending_controller_update(void)
{
    // ── 1. Process events from event bus ──
    app_event_t evt;
    while (event_bus_receive(&evt, 0)) {
        switch (evt.event_id) {
            // Button events
            case EVT_BUTTON_PRESSED:
                handle_button_pressed(evt.param1);
                break;

            case EVT_BUTTON_LONG_PRESSED:
                handle_button_long_pressed(evt.param1);
                break;

            case EVT_BUTTON_RELEASED:
                handle_button_released(evt.param1);
                break;

            // MQTT remote commands (commented out — services disabled)
            // case EVT_MQTT_CMD_DISPENSE:
            //     handle_mqtt_dispense(evt.param1);
            //     break;

            // Sensor events
            case EVT_SENSOR_TUBE_FAULT:
                // Legacy event - now handled by ERR_LEAKAGE
                break;

            // === COMPREHENSIVE ERROR HANDLING ===
            // Note: HOLD_PUMP is excluded - it's manual priming mode
            case EVT_ERR_EMPTY_TANK:
                // CRITICAL: Tank empty - both sensors dry
                Serial.println("[VEND] *** ERR_EMPTY_TANK: Stopping pump ***");
                lcd_driver_show_error("Tank Empty!");
                last_error_code = ERR_CODE_EMPTY_TANK;
                if (current_state == VEND_STATE_DISPENSING || 
                    current_state == VEND_STATE_PRIMING) {
                    // Don't stop HOLD_PUMP - it's manual priming mode
                    pump_driver_stop();
                    event_bus_publish(EVT_FLOW_ERROR, ERR_CODE_EMPTY_TANK, 0);
                    transition_to(VEND_STATE_ERROR);
                }
                break;

            case EVT_ERR_LEAKAGE:
                // CRITICAL: Tube broken/disconnected - LOCKDOWN required
                Serial.println("[VEND] *** ERR_LEAKAGE: LOCKDOWN ***");
                lcd_driver_show_error("LEAK DETECTED!");
                last_error_code = ERR_CODE_LEAKAGE;
                if (current_state == VEND_STATE_DISPENSING || 
                    current_state == VEND_STATE_PRIMING) {
                    // Don't stop HOLD_PUMP - it's manual priming mode
                    pump_driver_stop();
                    event_bus_publish(EVT_FLOW_ERROR, ERR_CODE_LEAKAGE, 0);
                    transition_to(VEND_STATE_LOCKDOWN);  // Requires technician reset
                }
                break;

            case EVT_ERR_MOTOR_STALL:
                // CRITICAL: Motor jammed or encoder broken - LOCKDOWN
                Serial.println("[VEND] *** ERR_MOTOR_STALL: LOCKDOWN ***");
                lcd_driver_show_error("Motor Stall!");
                last_error_code = ERR_CODE_MOTOR_STALL;
                if (current_state == VEND_STATE_DISPENSING || 
                    current_state == VEND_STATE_PRIMING) {
                    // Don't stop HOLD_PUMP - it's manual priming mode
                    pump_driver_stop();
                    event_bus_publish(EVT_FLOW_ERROR, ERR_CODE_MOTOR_STALL, 0);
                    transition_to(VEND_STATE_LOCKDOWN);  // Requires technician reset
                }
                break;

            case EVT_ERR_SENSOR_CONFLICT:
                // Logic error: inlet dry but outlet wet (impossible)
                Serial.println("[VEND] *** ERR_SENSOR_CONFLICT: Sensor logic error ***");
                lcd_driver_show_error("Sensor Error!");
                last_error_code = ERR_CODE_SENSOR_CONFLICT;
                // Prevent new cycles but don't stop current operation
                if (current_state == VEND_STATE_IDLE) {
                    transition_to(VEND_STATE_ERROR);
                }
                break;

            case EVT_WARN_AIR_BUBBLES:
                // Warning: air in line - suggest priming
                Serial.println("[VEND] WARNING: Air bubbles detected");
                lcd_driver_update_info("Air in line!");
                if (current_state == VEND_STATE_DISPENSING) {
                    // Pause current operation, switch to priming
                    pump_driver_stop();
                    lcd_driver_show_error("Priming Required");
                    transition_to(VEND_STATE_PRIMING);
                }
                break;

            case EVT_SENSOR_INLET_NO_WATER:
                // Warning: no water at inlet
                Serial.println("[VEND] WARNING: No water at inlet");
                lcd_driver_update_info("WARN: No inlet water");
                break;

            case EVT_SENSOR_INLET_WATER_OK:
            case EVT_SENSOR_OUTLET_WATER_OK:
            case EVT_SENSOR_OUTLET_NO_WATER:
                // Update sensor display
                {
                    bool inlet_ok = (sensor_driver_get_state(SENSOR_INLET) == SENSOR_HAS_WATER);
                    bool outlet_ok = (sensor_driver_get_state(SENSOR_OUTLET) == SENSOR_HAS_WATER);
                    lcd_driver_update_sensors(inlet_ok, outlet_ok);
                }
                break;

            // case EVT_MQTT_CMD_STOP:
            case EVT_SYSTEM_EMERGENCY_STOP:
                vending_controller_emergency_stop();
                break;

            case EVT_PUMP_SET_SPEED:
                if (current_state == VEND_STATE_HOLD_PUMP ||
                    current_state == VEND_STATE_DISPENSING) {
                    pump_driver_set_speed((uint8_t)evt.param1);
                }
                break;

            default:
                break;
        }
    }

    // ── 2. State-specific processing ──
    switch (current_state) {
        case VEND_STATE_IDLE:
            // Periodically update sensor status on LCD
            {
                static uint32_t last_idle_lcd = 0;
                if (millis() - last_idle_lcd >= 1000) {
                    last_idle_lcd = millis();
                    bool inlet_ok = (sensor_driver_get_state(SENSOR_INLET) == SENSOR_HAS_WATER);
                    bool outlet_ok = (sensor_driver_get_state(SENSOR_OUTLET) == SENSOR_HAS_WATER);
                    lcd_driver_update_sensors(inlet_ok, outlet_ok);
                }
            }
            break;

        case VEND_STATE_HOLD_PUMP:
            // HOLD = Manual priming mode - no fault monitoring, no stall detection
            // User is manually controlling and watching the pump
            pump_driver_update_rpm();
            system_state_update_pump(true, pump_driver_get_speed(),
                                     pump_driver_get_pump_head_rpm(),
                                     pump_driver_get_pulse_count());
            
            // Update LCD with hold mode status (no error checks)
            {
                static uint32_t last_hold_lcd = 0;
                if (millis() - last_hold_lcd >= 200) {
                    last_hold_lcd = millis();
                    lcd_driver_show_hold_mode(pump_driver_get_speed(), 
                                               pump_driver_get_pulse_count());
                    
                    // Show sensor status (info only, no errors)
                    bool inlet_ok = (sensor_driver_get_state(SENSOR_INLET) == SENSOR_HAS_WATER);
                    bool outlet_ok = (sensor_driver_get_state(SENSOR_OUTLET) == SENSOR_HAS_WATER);
                    lcd_driver_update_sensors(inlet_ok, outlet_ok);
                }
            }
            break;

        case VEND_STATE_DISPENSING:
            update_flow_monitoring();
            pump_driver_update_rpm();
            system_state_update_pump(true, pump_driver_get_speed(),
                                     pump_driver_get_pump_head_rpm(),
                                     pump_driver_get_pulse_count());
            
            // Motor stall detection
            if (millis() - last_stall_check_ms >= STALL_CHECK_INTERVAL_MS) {
                last_stall_check_ms = millis();
                if (pump_driver_check_motor_stall()) {
                    event_bus_publish(EVT_ERR_MOTOR_STALL, 0, 0);
                }
            }
            break;

        case VEND_STATE_PRIMING:
            // Priming mode: wait for user to confirm water flow
            pump_driver_update_rpm();
            
            // Check if both sensors now show water (priming complete)
            if (sensor_driver_both_have_water()) {
                Serial.println("[VEND] Priming complete - water detected");
                lcd_driver_update_info("Priming OK!");
                sensor_driver_set_priming_mode(false);
                transition_to(VEND_STATE_IDLE);
            }
            
            // Allow user to manually release hold to exit priming
            // (handled by button release event)
            break;

        case VEND_STATE_LOCKDOWN:
            // LOCKDOWN: Reject all commands until technician reset
            // Only way out is vending_controller_reset_lockdown() or power cycle
            {
                static uint32_t last_lockdown_blink = 0;
                if (millis() - last_lockdown_blink >= 2000) {
                    last_lockdown_blink = millis();
                    Serial.println("[VEND] LOCKDOWN ACTIVE - Call technician");
                }
            }
            break;

        case VEND_STATE_COMPLETE:
            // Brief pause then return to IDLE
            if (millis() - state_enter_ms >= 500) {
                Serial.println("[VEND] Ready for next dispense\n");
                transition_to(VEND_STATE_IDLE);
            }
            break;

        case VEND_STATE_ERROR:
            // Wait for reset (any button press) or MQTT command
            if (millis() - state_enter_ms >= 5000) {
                Serial.println("[VEND] Auto-recovering from error...");
                transition_to(VEND_STATE_IDLE);
            }
            break;

        default:
            break;
    }

    // ── 3. Update LED pattern ──
    led_driver_update();
}

void vending_controller_emergency_stop(void)
{
    Serial.println("[VEND] *** EMERGENCY STOP ***");
    pump_driver_stop();
    system_state_set(SYS_STATE_EMERGENCY_STOP);
    led_driver_set_pattern(LED_BLINK_ERROR);

    transition_to(VEND_STATE_IDLE);
    event_bus_publish(EVT_SYSTEM_EMERGENCY_STOP, 0, 0);
}

vend_state_t vending_controller_get_state(void)
{
    return current_state;
}

const char* vending_controller_get_state_name(void)
{
    if (current_state < VEND_STATE_COUNT) {
        return STATE_NAMES[current_state];
    }
    return "UNKNOWN";
}

bool vending_controller_dispense_ml(uint32_t volume_ml, uint8_t speed_percent)
{
    if (current_state != VEND_STATE_IDLE) return false;
    if (volume_ml == 0 || volume_ml > 5000) return false;
    if (speed_percent == 0 || speed_percent > 100) return false;

    start_volume_dispense(volume_ml, speed_percent);
    return true;
}

/**
 * @brief Reset lockdown state (technician function)
 * @details Only call after technician has verified and fixed the issue
 * @return true if successfully reset from lockdown
 */
bool vending_controller_reset_lockdown(void)
{
    if (current_state != VEND_STATE_LOCKDOWN) return false;
    
    Serial.println("[VEND] *** LOCKDOWN RESET BY TECHNICIAN ***");
    last_error_code = ERR_CODE_NONE;
    transition_to(VEND_STATE_IDLE);
    return true;
}

/**
 * @brief Get last error code (for RS485/MQTT reporting)
 * @return error_code_t last error code
 */
uint8_t vending_controller_get_last_error(void)
{
    return (uint8_t)last_error_code;
}

/**
 * @brief Start priming mode manually
 * @param speed_percent Pump speed for priming
 * @return true if priming started
 */
bool vending_controller_start_priming(uint8_t speed_percent)
{
    if (current_state != VEND_STATE_IDLE && current_state != VEND_STATE_ERROR) {
        return false;
    }
    
    pump_driver_reset_pulses();
    pump_driver_set_direction(PUMP_DIR_CW);
    pump_driver_start(speed_percent);
    
    Serial.printf("[VEND] Priming mode started @ %d%%\n", speed_percent);
    transition_to(VEND_STATE_PRIMING);
    return true;
}
