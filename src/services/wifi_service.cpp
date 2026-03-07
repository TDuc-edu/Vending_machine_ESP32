/**
 * @file    wifi_service.cpp
 * @brief   WiFi service implementation with auto-reconnect
 */

#include "wifi_service.h"
#include <WiFi.h>
#include "../system/event_bus.h"
#include "../system/system_state.h"

/* ============================================================================
 *                         PRIVATE DATA
 * ============================================================================ */

static char wifi_ssid[WIFI_SSID_MAX_LEN + 1] = {0};
static char wifi_pass[WIFI_PASS_MAX_LEN + 1] = {0};

static bool was_connected     = false;
static bool init_done         = false;
static uint32_t last_retry_ms = 0;
static uint32_t retry_count   = 0;

/* ============================================================================
 *                         INITIALIZATION
 * ============================================================================ */

bool wifi_service_init(const char* ssid, const char* password)
{
    if (ssid == NULL) return false;

    strncpy(wifi_ssid, ssid, WIFI_SSID_MAX_LEN);
    if (password != NULL) {
        strncpy(wifi_pass, password, WIFI_PASS_MAX_LEN);
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(wifi_ssid, wifi_pass);

    init_done     = true;
    was_connected = false;
    last_retry_ms = millis();
    retry_count   = 0;

    Serial.printf("[WIFI] Connecting to '%s'...\n", wifi_ssid);
    return true;
}

/* ============================================================================
 *                         UPDATE (call periodically)
 * ============================================================================ */

void wifi_service_update(void)
{
    if (!init_done) return;

    bool connected = (WiFi.status() == WL_CONNECTED);

    // Just connected
    if (connected && !was_connected) {
        was_connected = true;
        retry_count   = 0;

        Serial.printf("[WIFI] Connected! IP: %s, RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.RSSI());

        event_bus_publish(EVT_WIFI_CONNECTED, 0, 0);
        system_state_update_network(true, false, WiFi.RSSI());
    }

    // Just disconnected
    if (!connected && was_connected) {
        was_connected = false;
        Serial.println("[WIFI] Disconnected!");

        event_bus_publish(EVT_WIFI_DISCONNECTED, 0, 0);
        system_state_update_network(false, false, 0);
    }

    // Auto reconnect
    if (!connected) {
        uint32_t now = millis();
        if (now - last_retry_ms >= WIFI_RECONNECT_DELAY_MS) {
            last_retry_ms = now;
            retry_count++;

            if (WIFI_MAX_RETRY == 0 || retry_count <= WIFI_MAX_RETRY) {
                Serial.printf("[WIFI] Reconnecting... (attempt %lu)\n", retry_count);
                WiFi.disconnect();
                WiFi.begin(wifi_ssid, wifi_pass);
                event_bus_publish(EVT_WIFI_RECONNECTING, retry_count, 0);
            }
        }
    }

    // Update RSSI periodically when connected
    if (connected) {
        static uint32_t last_rssi = 0;
        if (millis() - last_rssi >= 10000) {
            last_rssi = millis();
            system_state_update_network(true,
                                        system_state_get() != SYS_STATE_ERROR,
                                        WiFi.RSSI());
        }
    }
}

/* ============================================================================
 *                         STATUS
 * ============================================================================ */

bool wifi_service_is_connected(void)
{
    return (WiFi.status() == WL_CONNECTED);
}

int8_t wifi_service_get_rssi(void)
{
    if (WiFi.status() == WL_CONNECTED) {
        return (int8_t)WiFi.RSSI();
    }
    return 0;
}

String wifi_service_get_ip(void)
{
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.localIP().toString();
    }
    return "0.0.0.0";
}

void wifi_service_disconnect(void)
{
    WiFi.disconnect();
    was_connected = false;
    Serial.println("[WIFI] Disconnected by request");
}
