/**
 * @file    event_bus.cpp
 * @brief   Event Bus implementation using FreeRTOS Queue
 */

#include "event_bus.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static QueueHandle_t event_queue = NULL;
static bool is_initialized = false;

/* ============================================================================
 *                         PUBLIC FUNCTIONS
 * ============================================================================ */

bool event_bus_init(void)
{
    if (is_initialized) return true;

    event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(app_event_t));
    if (event_queue == NULL) {
        Serial.println("[EVT_BUS] Failed to create event queue!");
        return false;
    }

    is_initialized = true;
    Serial.println("[EVT_BUS] Initialized (queue size: " + String(EVENT_QUEUE_SIZE) + ")");
    return true;
}

bool event_bus_publish(uint16_t event_id, uint32_t param1, uint32_t param2)
{
    if (!is_initialized || event_queue == NULL) return false;

    app_event_t evt;
    evt.event_id  = event_id;
    evt.param1    = param1;
    evt.param2    = param2;
    evt.timestamp = millis();

    BaseType_t result = xQueueSend(event_queue, &evt, pdMS_TO_TICKS(EVENT_TIMEOUT_MS));

    if (result != pdTRUE) {
        Serial.printf("[EVT_BUS] Queue full! Dropped event 0x%04X\n", event_id);
        return false;
    }

    return true;
}

bool event_bus_publish_from_isr(uint16_t event_id, uint32_t param1, uint32_t param2)
{
    if (!is_initialized || event_queue == NULL) return false;

    app_event_t evt;
    evt.event_id  = event_id;
    evt.param1    = param1;
    evt.param2    = param2;
    evt.timestamp = millis();

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t result = xQueueSendFromISR(event_queue, &evt, &xHigherPriorityTaskWoken);

    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }

    return (result == pdTRUE);
}

bool event_bus_receive(app_event_t *event, uint32_t timeout_ms)
{
    if (!is_initialized || event_queue == NULL || event == NULL) return false;

    TickType_t ticks = (timeout_ms == portMAX_DELAY)
                       ? portMAX_DELAY
                       : pdMS_TO_TICKS(timeout_ms);

    return (xQueueReceive(event_queue, event, ticks) == pdTRUE);
}

uint32_t event_bus_pending(void)
{
    if (!is_initialized || event_queue == NULL) return 0;
    return (uint32_t)uxQueueMessagesWaiting(event_queue);
}

void event_bus_flush(void)
{
    if (!is_initialized || event_queue == NULL) return;
    xQueueReset(event_queue);
}

QueueHandle_t event_bus_get_queue(void)
{
    return event_queue;
}

const char* event_bus_get_name(uint16_t event_id)
{
    switch (event_id) {
        // Button events
        case EVT_BUTTON_PRESSED:        return "BTN_PRESSED";
        case EVT_BUTTON_LONG_PRESSED:   return "BTN_LONG";
        case EVT_BUTTON_RELEASED:       return "BTN_RELEASED";

        // Pump events
        case EVT_PUMP_START:            return "PUMP_START";
        case EVT_PUMP_STOP:             return "PUMP_STOP";
        case EVT_PUMP_SET_SPEED:        return "PUMP_SET_SPEED";
        case EVT_PUMP_ERROR:            return "PUMP_ERROR";
        case EVT_PUMP_DONE:             return "PUMP_DONE";

        // Flow events
        case EVT_FLOW_START:            return "FLOW_START";
        case EVT_FLOW_COMPLETE:         return "FLOW_COMPLETE";
        case EVT_FLOW_UPDATE:           return "FLOW_UPDATE";
        case EVT_FLOW_ERROR:            return "FLOW_ERROR";

        // WiFi events
        case EVT_WIFI_CONNECTED:        return "WIFI_CONN";
        case EVT_WIFI_DISCONNECTED:     return "WIFI_DISC";

        // MQTT events
        case EVT_MQTT_CONNECTED:        return "MQTT_CONN";
        case EVT_MQTT_DISCONNECTED:     return "MQTT_DISC";
        case EVT_MQTT_MESSAGE:          return "MQTT_MSG";
        case EVT_MQTT_CMD_DISPENSE:     return "MQTT_DISPENSE";
        case EVT_MQTT_CMD_STOP:         return "MQTT_STOP";

        // System events
        case EVT_SYSTEM_READY:          return "SYS_READY";
        case EVT_SYSTEM_ERROR:          return "SYS_ERROR";
        case EVT_SYSTEM_EMERGENCY_STOP: return "EMERGENCY_STOP";

        default:                        return "UNKNOWN";
    }
}
