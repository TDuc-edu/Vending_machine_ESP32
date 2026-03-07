/**
 * @file    mqtt_service.h
 * @brief   MQTT service for remote control and telemetry
 * @details Runs on Core 0. Subscribes to command topics, publishes telemetry.
 *          Uses PubSubClient library.
 *
 * Topics:
 *   Subscribe: vending/{device_id}/cmd/dispense   → {volume_ml, speed}
 *   Subscribe: vending/{device_id}/cmd/stop        → emergency stop
 *   Publish:   vending/{device_id}/status           → system status JSON
 *   Publish:   vending/{device_id}/telemetry        → flow/pump data
 */

#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <Arduino.h>

/* ============================================================================
 *                         CONFIGURATION
 * ============================================================================ */

#define MQTT_BROKER_HOST        "192.168.1.100"
#define MQTT_BROKER_PORT        1883
#define MQTT_CLIENT_ID          "vending_esp32_01"
#define MQTT_KEEPALIVE_SEC      30
#define MQTT_RECONNECT_DELAY_MS 5000
#define MQTT_TELEMETRY_INTERVAL 5000    // Publish status every 5 seconds

// Topics
#define MQTT_TOPIC_STATUS       "vending/esp32_01/status"
#define MQTT_TOPIC_TELEMETRY    "vending/esp32_01/telemetry"
#define MQTT_TOPIC_CMD_DISPENSE "vending/esp32_01/cmd/dispense"
#define MQTT_TOPIC_CMD_STOP     "vending/esp32_01/cmd/stop"
#define MQTT_TOPIC_CMD_SPEED    "vending/esp32_01/cmd/speed"

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize MQTT client
 * @param broker_host MQTT broker hostname/IP
 * @param broker_port MQTT broker port
 * @return true if started
 */
bool mqtt_service_init(const char* broker_host, uint16_t broker_port);

/**
 * @brief Process MQTT (call periodically — handles reconnect, incoming msgs)
 */
void mqtt_service_update(void);

/**
 * @brief Check if connected to MQTT broker
 * @return true if connected
 */
bool mqtt_service_is_connected(void);

/**
 * @brief Publish a message to a topic
 * @param topic MQTT topic
 * @param payload Message payload
 * @return true if published
 */
bool mqtt_service_publish(const char* topic, const char* payload);

/**
 * @brief Publish system status as JSON
 */
void mqtt_service_publish_status(void);

/**
 * @brief Publish telemetry data
 */
void mqtt_service_publish_telemetry(void);

#endif // MQTT_SERVICE_H
