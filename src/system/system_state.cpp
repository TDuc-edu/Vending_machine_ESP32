/**
 * @file    system_state.cpp
 * @brief   System state manager implementation with mutex protection
 */

#include "system_state.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static system_status_t sys_status;
static SemaphoreHandle_t state_mutex = NULL;
static bool is_initialized = false;

static const char* STATE_NAMES[] = {
    "BOOT", "INIT", "READY", "DISPENSING",
    "HOLD_PUMP", "ERROR", "EMERGENCY_STOP"
};

/* ============================================================================
 *                         PRIVATE HELPERS
 * ============================================================================ */

static inline bool take_mutex(uint32_t timeout_ms = 100)
{
    if (state_mutex == NULL) return false;
    return (xSemaphoreTake(state_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

static inline void give_mutex(void)
{
    if (state_mutex != NULL) {
        xSemaphoreGive(state_mutex);
    }
}

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

void system_state_init(void)
{
    if (is_initialized) return;

    state_mutex = xSemaphoreCreateMutex();
    if (state_mutex == NULL) {
        Serial.println("[SYS_STATE] Failed to create mutex!");
        return;
    }

    // Initialize all fields to zero/defaults
    memset(&sys_status, 0, sizeof(system_status_t));
    sys_status.state = SYS_STATE_BOOT;

    is_initialized = true;
    Serial.println("[SYS_STATE] Initialized");
}

system_state_t system_state_get(void)
{
    system_state_t state = SYS_STATE_ERROR;
    if (take_mutex()) {
        state = sys_status.state;
        give_mutex();
    }
    return state;
}

void system_state_set(system_state_t new_state)
{
    if (new_state >= SYS_STATE_COUNT) return;
    if (take_mutex()) {
        system_state_t old_state = sys_status.state;
        sys_status.state = new_state;
        give_mutex();

        if (old_state != new_state) {
            Serial.printf("[SYS_STATE] %s -> %s\n",
                          STATE_NAMES[old_state], STATE_NAMES[new_state]);
        }
    }
}

void system_state_get_status(system_status_t *status)
{
    if (status == NULL) return;
    if (take_mutex()) {
        sys_status.uptime_sec = millis() / 1000;
        memcpy(status, &sys_status, sizeof(system_status_t));
        give_mutex();
    }
}

void system_state_update_pump(bool running, uint8_t speed, uint32_t rpm, uint32_t pulses)
{
    if (take_mutex()) {
        sys_status.pump_running       = running;
        sys_status.pump_speed_percent = speed;
        sys_status.pump_rpm           = rpm;
        sys_status.pump_pulse_count   = pulses;
        give_mutex();
    }
}

void system_state_update_flow(uint32_t target_ul, uint32_t dispensed_ul, uint8_t progress)
{
    if (take_mutex()) {
        sys_status.flow_target_ul    = target_ul;
        sys_status.flow_dispensed_ul = dispensed_ul;
        sys_status.flow_progress     = progress;
        give_mutex();
    }
}

void system_state_update_network(bool wifi, bool mqtt, int8_t rssi)
{
    if (take_mutex()) {
        sys_status.wifi_connected = wifi;
        sys_status.mqtt_connected = mqtt;
        sys_status.wifi_rssi      = rssi;
        give_mutex();
    }
}

void system_state_add_dispense(uint32_t volume_ml)
{
    if (take_mutex()) {
        sys_status.total_dispensed_ml += volume_ml;
        sys_status.total_dispense_count++;
        give_mutex();
    }
}

const char* system_state_name(system_state_t state)
{
    if (state < SYS_STATE_COUNT) {
        return STATE_NAMES[state];
    }
    return "UNKNOWN";
}
