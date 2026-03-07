/**
 * @file    mqtt_service.cpp
 * @brief   MQTT service implementation using PubSubClient
 */

#include "mqtt_service.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include "../system/event_bus.h"
#include "../system/system_state.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static WiFiClient   wifi_client;
static PubSubClient mqtt_client(wifi_client);

static bool init_done          = false;
static bool was_connected      = false;
static uint32_t last_retry_ms  = 0;
static uint32_t last_telemetry = 0;

static char broker_addr[64]    = {0};
static uint16_t broker_port    = 1883;

/* ============================================================================
 *                         MQTT CALLBACK
 * ============================================================================ */

static void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
    // Null-terminate payload
    char msg[256];
    uint32_t copy_len = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
    memcpy(msg, payload, copy_len);
    msg[copy_len] = '\0';

    Serial.printf("[MQTT] Received [%s]: %s\n", topic, msg);

    // Command: dispense
    if (strcmp(topic, MQTT_TOPIC_CMD_DISPENSE) == 0) {
        uint32_t volume = (uint32_t)atoi(msg);
        if (volume > 0 && volume <= 5000) {
            event_bus_publish(EVT_MQTT_CMD_DISPENSE, volume, 0);
            Serial.printf("[MQTT] CMD: Dispense %lu ml\n", volume);
        }
    }

    // Command: stop
    else if (strcmp(topic, MQTT_TOPIC_CMD_STOP) == 0) {
        event_bus_publish(EVT_MQTT_CMD_STOP, 0, 0);
        Serial.println("[MQTT] CMD: Emergency Stop");
    }

    // Command: set speed
    else if (strcmp(topic, MQTT_TOPIC_CMD_SPEED) == 0) {
        uint32_t speed = (uint32_t)atoi(msg);
        if (speed > 0 && speed <= 100) {
            event_bus_publish(EVT_PUMP_SET_SPEED, speed, 0);
        }
    }

    // Generic MQTT message event
    event_bus_publish(EVT_MQTT_MESSAGE, 0, 0);
}

/* ============================================================================
 *                         PRIVATE HELPERS
 * ============================================================================ */

static bool mqtt_connect(void)
{
    Serial.printf("[MQTT] Connecting to %s:%d...\n", broker_addr, broker_port);

    if (mqtt_client.connect(MQTT_CLIENT_ID)) {
        Serial.println("[MQTT] Connected!");

        // Subscribe to command topics
        mqtt_client.subscribe(MQTT_TOPIC_CMD_DISPENSE);
        mqtt_client.subscribe(MQTT_TOPIC_CMD_STOP);
        mqtt_client.subscribe(MQTT_TOPIC_CMD_SPEED);

        Serial.println("[MQTT] Subscribed to command topics");
        return true;
    }

    Serial.printf("[MQTT] Connection failed, rc=%d\n", mqtt_client.state());
    return false;
}

/* ============================================================================
 *                         INITIALIZATION
 * ============================================================================ */

bool mqtt_service_init(const char* host, uint16_t port)
{
    if (host == NULL) return false;

    strncpy(broker_addr, host, sizeof(broker_addr) - 1);
    broker_port = port;

    mqtt_client.setServer(broker_addr, broker_port);
    mqtt_client.setCallback(mqtt_callback);
    mqtt_client.setKeepAlive(MQTT_KEEPALIVE_SEC);
    mqtt_client.setBufferSize(512);

    init_done     = true;
    was_connected = false;
    last_retry_ms = 0;

    Serial.printf("[MQTT] Initialized (broker: %s:%d)\n", broker_addr, broker_port);
    return true;
}

/* ============================================================================
 *                         UPDATE (call periodically)
 * ============================================================================ */

void mqtt_service_update(void)
{
    if (!init_done) return;

    // MQTT requires WiFi
    if (WiFi.status() != WL_CONNECTED) return;

    bool connected = mqtt_client.connected();

    // Handle connect/disconnect transitions
    if (connected && !was_connected) {
        was_connected = true;
        event_bus_publish(EVT_MQTT_CONNECTED, 0, 0);
    }
    if (!connected && was_connected) {
        was_connected = false;
        event_bus_publish(EVT_MQTT_DISCONNECTED, 0, 0);
    }

    // Auto-reconnect
    if (!connected) {
        uint32_t now = millis();
        if (now - last_retry_ms >= MQTT_RECONNECT_DELAY_MS) {
            last_retry_ms = now;
            mqtt_connect();
        }
        return;
    }

    // Process incoming messages
    mqtt_client.loop();

    // Periodic telemetry
    uint32_t now = millis();
    if (now - last_telemetry >= MQTT_TELEMETRY_INTERVAL) {
        last_telemetry = now;
        mqtt_service_publish_telemetry();
    }
}

/* ============================================================================
 *                         STATUS
 * ============================================================================ */

bool mqtt_service_is_connected(void)
{
    return mqtt_client.connected();
}

/* ============================================================================
 *                         PUBLISH
 * ============================================================================ */

bool mqtt_service_publish(const char* topic, const char* payload)
{
    if (!mqtt_client.connected()) return false;
    return mqtt_client.publish(topic, payload);
}

void mqtt_service_publish_status(void)
{
    system_status_t status;
    system_state_get_status(&status);

    char json[256];
    snprintf(json, sizeof(json),
        "{\"state\":\"%s\",\"uptime\":%lu,\"pump\":%s,\"speed\":%d,"
        "\"dispensed_ml\":%lu,\"count\":%lu,\"wifi_rssi\":%d}",
        system_state_name(status.state),
        status.uptime_sec,
        status.pump_running ? "true" : "false",
        status.pump_speed_percent,
        status.total_dispensed_ml,
        status.total_dispense_count,
        status.wifi_rssi
    );

    mqtt_service_publish(MQTT_TOPIC_STATUS, json);
}

void mqtt_service_publish_telemetry(void)
{
    system_status_t status;
    system_state_get_status(&status);

    char json[256];
    snprintf(json, sizeof(json),
        "{\"rpm\":%lu,\"pulses\":%lu,\"flow_target_ul\":%lu,"
        "\"flow_dispensed_ul\":%lu,\"progress\":%d}",
        status.pump_rpm,
        status.pump_pulse_count,
        status.flow_target_ul,
        status.flow_dispensed_ul,
        status.flow_progress
    );

    mqtt_service_publish(MQTT_TOPIC_TELEMETRY, json);
}
