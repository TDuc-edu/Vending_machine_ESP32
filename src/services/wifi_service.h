/**
 * @file    wifi_service.h
 * @brief   WiFi service with auto-reconnect for ESP32
 * @details Runs on Core 0 (PRO CPU) for network stack.
 *          Publishes EVT_WIFI_CONNECTED / EVT_WIFI_DISCONNECTED to event bus.
 */

#ifndef WIFI_SERVICE_H
#define WIFI_SERVICE_H

#include <Arduino.h>

/* ============================================================================
 *                         CONFIGURATION
 * ============================================================================ */

#define WIFI_SSID_MAX_LEN       32
#define WIFI_PASS_MAX_LEN       64
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_RECONNECT_DELAY_MS 5000
#define WIFI_MAX_RETRY          0       // 0 = infinite retries

/* ============================================================================
 *                         FUNCTION PROTOTYPES
 * ============================================================================ */

/**
 * @brief Initialize WiFi in station mode
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return true if initialization started
 */
bool wifi_service_init(const char* ssid, const char* password);

/**
 * @brief Process WiFi connection state (call periodically)
 *        Handles auto-reconnect and publishes events.
 */
void wifi_service_update(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected
 */
bool wifi_service_is_connected(void);

/**
 * @brief Get WiFi RSSI
 * @return Signal strength in dBm
 */
int8_t wifi_service_get_rssi(void);

/**
 * @brief Get local IP address as string
 * @return IP address string
 */
String wifi_service_get_ip(void);

/**
 * @brief Disconnect WiFi
 */
void wifi_service_disconnect(void);

#endif // WIFI_SERVICE_H
