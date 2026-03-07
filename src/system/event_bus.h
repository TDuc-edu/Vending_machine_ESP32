/**
 * @file    event_bus.h
 * @brief   FreeRTOS-based Event Bus for inter-module communication
 * @details Modules communicate through events, NOT direct function calls.
 *          This ensures loose coupling and clean architecture.
 *
 * Event flow:
 *   Publisher → event_bus_publish() → xQueueSend → event_queue
 *   Consumer  → event_bus_receive() → xQueueReceive → process event
 *
 * Event categories:
 *   0x01xx - Button events
 *   0x02xx - Pump events
 *   0x03xx - Flow events
 *   0x04xx - WiFi events
 *   0x05xx - MQTT events
 *   0x06xx - System events
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/* ============================================================================
 *                         EVENT DEFINITIONS
 * ============================================================================ */

// Button events (0x01xx)
#define EVT_BUTTON_PRESSED          0x0100
#define EVT_BUTTON_LONG_PRESSED     0x0101
#define EVT_BUTTON_RELEASED         0x0102

// Pump events (0x02xx)
#define EVT_PUMP_START              0x0200
#define EVT_PUMP_STOP               0x0201
#define EVT_PUMP_SET_SPEED          0x0202
#define EVT_PUMP_ERROR              0x0203
#define EVT_PUMP_DONE               0x0204

// Flow events (0x03xx)
#define EVT_FLOW_START              0x0300
#define EVT_FLOW_COMPLETE           0x0301
#define EVT_FLOW_UPDATE             0x0302
#define EVT_FLOW_ERROR              0x0303

// WiFi events (0x04xx)
#define EVT_WIFI_CONNECTED          0x0400
#define EVT_WIFI_DISCONNECTED       0x0401
#define EVT_WIFI_RECONNECTING       0x0402

// MQTT events (0x05xx)
#define EVT_MQTT_CONNECTED          0x0500
#define EVT_MQTT_DISCONNECTED       0x0501
#define EVT_MQTT_MESSAGE            0x0502
#define EVT_MQTT_CMD_DISPENSE       0x0503
#define EVT_MQTT_CMD_STOP           0x0504

// System events (0x06xx)
#define EVT_SYSTEM_READY            0x0600
#define EVT_SYSTEM_ERROR            0x0601
#define EVT_SYSTEM_EMERGENCY_STOP   0x0602
#define EVT_SYSTEM_WATCHDOG         0x0603

// Sensor events (0x07xx)
#define EVT_SENSOR_INLET_WATER_OK   0x0700
#define EVT_SENSOR_INLET_NO_WATER   0x0701
#define EVT_SENSOR_OUTLET_WATER_OK  0x0702
#define EVT_SENSOR_OUTLET_NO_WATER  0x0703
#define EVT_SENSOR_TUBE_FAULT       0x0704

/* ============================================================================
 *                         EVENT STRUCTURE
 * ============================================================================ */

typedef struct {
    uint16_t event_id;      // Event identifier (EVT_xxx)
    uint32_t param1;        // Generic parameter 1 (e.g. button_index, speed_percent)
    uint32_t param2;        // Generic parameter 2 (e.g. volume_ml, duration_ms)
    uint32_t timestamp;     // millis() when event was created
} app_event_t;

/* ============================================================================
 *                         CONFIGURATION
 * ============================================================================ */

#define EVENT_QUEUE_SIZE    32
#define EVENT_TIMEOUT_MS    10

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize the event bus (create FreeRTOS queue)
 * @return true if successful
 */
bool event_bus_init(void);

/**
 * @brief Publish an event to the event bus
 * @param event_id Event identifier
 * @param param1 First parameter
 * @param param2 Second parameter
 * @return true if event was queued successfully
 */
bool event_bus_publish(uint16_t event_id, uint32_t param1, uint32_t param2);

/**
 * @brief Publish event from ISR context (safe for interrupts)
 * @param event_id Event identifier
 * @param param1 First parameter
 * @param param2 Second parameter
 * @return true if event was queued
 */
bool event_bus_publish_from_isr(uint16_t event_id, uint32_t param1, uint32_t param2);

/**
 * @brief Receive an event from the event bus (blocking with timeout)
 * @param event Pointer to event structure to fill
 * @param timeout_ms Timeout in milliseconds (0 = no wait, portMAX_DELAY = forever)
 * @return true if an event was received
 */
bool event_bus_receive(app_event_t *event, uint32_t timeout_ms);

/**
 * @brief Check if events are pending in the queue
 * @return Number of events waiting
 */
uint32_t event_bus_pending(void);

/**
 * @brief Flush all pending events
 */
void event_bus_flush(void);

/**
 * @brief Get the FreeRTOS queue handle (for advanced usage)
 * @return Queue handle
 */
QueueHandle_t event_bus_get_queue(void);

/**
 * @brief Get event name string for debugging
 * @param event_id Event identifier
 * @return Human-readable event name
 */
const char* event_bus_get_name(uint16_t event_id);

#endif // EVENT_BUS_H
