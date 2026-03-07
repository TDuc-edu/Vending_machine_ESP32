/**
 * @file    main.cpp
 * @brief   Vending Machine ESP32 — Main entry point
 * @details FreeRTOS dual-core architecture:
 *          Core 0 (PRO CPU): WiFi, MQTT, telemetry — network stack
 *          Core 1 (APP CPU): Button, pump, vending logic — real-time control
 *
 * Boot sequence:
 *   1. Serial init
 *   2. HAL init (GPIO, PWM)
 *   3. System init (event_bus, system_state)
 *   4. Driver init (pump, button, LED)
 *   5. Service init (WiFi, MQTT)
 *   6. App init (recipe_engine, vending_controller)
 *   7. Create FreeRTOS tasks
 *   8. System ready
 *
 * Watchdog: Task WDT enabled for industrial reliability.
 */

#include <Arduino.h>
#include <esp_task_wdt.h>

// HAL Layer
#include "hal/gpio_hal.h"
#include "hal/pwm_hal.h"

// System Layer
#include "system/event_bus.h"
#include "system/system_state.h"

// Drivers
#include "drivers/pump_driver.h"
#include "drivers/button_driver.h"
#include "drivers/led_driver.h"
#include "drivers/sensor_driver.h"
#include "drivers/lcd_driver.h"

// Services (commented out — will implement later)
// #include "services/wifi_service.h"
// #include "services/mqtt_service.h"

// Application
#include "app/recipe_engine.h"
#include "app/vending_controller.h"

/* ============================================================================
 *                         CONFIGURATION
 * ============================================================================ */

// WiFi credentials (commented out — will implement later)
// #define WIFI_SSID       "VendingMachine_AP"
// #define WIFI_PASSWORD   "12345678"

// MQTT broker (commented out — will implement later)
// #define MQTT_HOST       "192.168.1.100"
// #define MQTT_PORT       1883

// Task watchdog timeout
#define WDT_TIMEOUT_SEC 10

// Task stack sizes
#define STACK_BUTTON    2048
#define STACK_SENSOR    2048
#define STACK_VENDING   4096
#define STACK_PUMP_MON  2048
// #define STACK_WIFI      4096
// #define STACK_MQTT      4096

/* ============================================================================
 *                         TASK HANDLES
 * ============================================================================ */

static TaskHandle_t h_button_task   = NULL;
static TaskHandle_t h_sensor_task   = NULL;
static TaskHandle_t h_vending_task  = NULL;
static TaskHandle_t h_pump_mon_task = NULL;
// static TaskHandle_t h_wifi_task     = NULL;
// static TaskHandle_t h_mqtt_task     = NULL;

/* ============================================================================
 *                 CORE 1 TASKS — Real-time Control
 * ============================================================================ */

/**
 * @brief Button scanning task — runs every 10ms on Core 1
 *        Performs 3-sample debounce & publishes events to event_bus.
 */
static void button_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        button_driver_scan();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SCAN_MS));
    }
}

/**
 * @brief Sensor scanning task — runs every 50ms on Core 1
 *        Monitors inlet/outlet liquid sensors & publishes events.
 */
static void sensor_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        sensor_driver_scan();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Vending logic task — event-driven state machine on Core 1
 *        Consumes events, drives pump, monitors flow.
 */
static void vending_logic_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        vending_controller_update();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Pump RPM monitor task — periodic RPM calculation on Core 1
 */
static void pump_monitor_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        pump_driver_update_rpm();

        // Update system state with pump telemetry
        pump_status_t ps = pump_driver_get_status();
        system_state_update_pump(
            ps.state == PUMP_RUNNING,
            ps.speed_percent,
            ps.fg.pump_head_rpm,
            ps.fg.pulse_count
        );

        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(RPM_SAMPLE_PERIOD_MS));
    }
}

/* ============================================================================
 *                 CORE 0 TASKS — Network / Communication
 *                 (COMMENTED OUT — will implement later)
 * ============================================================================ */

/*
 * @brief WiFi management task on Core 0
 *        Handles connection, reconnection, and publishes events.
 */
/*
static void wifi_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        wifi_service_update();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
*/

/*
 * @brief MQTT task on Core 0
 *        Processes messages, publishes telemetry.
 */
/*
static void mqtt_task(void *param)
{
    (void)param;
    esp_task_wdt_add(NULL);

    for (;;) {
        mqtt_service_update();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
*/

/* ============================================================================
 *                         BOOT SEQUENCE
 * ============================================================================ */

void setup()
{
    // ── 1. Serial ──
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(500);

    Serial.println();
    Serial.println("========================================");
    Serial.println("   VENDING MACHINE — ESP32-WROOM-32    ");
    Serial.println("   Layered Architecture + FreeRTOS     ");
    Serial.println("========================================");
    
    // Print reset reason
    esp_reset_reason_t reset_reason = esp_reset_reason();
    Serial.printf("[BOOT] Reset reason: %d (", reset_reason);
    switch (reset_reason) {
        case ESP_RST_UNKNOWN:   Serial.print("UNKNOWN"); break;
        case ESP_RST_POWERON:   Serial.print("POWER_ON"); break;
        case ESP_RST_EXT:       Serial.print("EXTERNAL_PIN"); break;
        case ESP_RST_SW:        Serial.print("SOFTWARE"); break;
        case ESP_RST_PANIC:     Serial.print("PANIC/EXCEPTION"); break;
        case ESP_RST_INT_WDT:   Serial.print("INTERRUPT_WDT"); break;
        case ESP_RST_TASK_WDT:  Serial.print("TASK_WDT"); break;
        case ESP_RST_WDT:       Serial.print("OTHER_WDT"); break;
        case ESP_RST_DEEPSLEEP: Serial.print("DEEP_SLEEP"); break;
        case ESP_RST_BROWNOUT:  Serial.print("BROWNOUT"); break;
        case ESP_RST_SDIO:      Serial.print("SDIO"); break;
        default:                Serial.print("UNKNOWN"); break;
    }
    Serial.println(")");
    Serial.println();

    // ── 2. HAL Layer ──
    Serial.println("[BOOT] Initializing HAL...");
    gpio_hal_init();
    Serial.println("[BOOT] GPIO HAL: OK");

    // ── 3. System Layer ──
    Serial.println("[BOOT] Initializing System...");
    system_state_init();
    system_state_set(SYS_STATE_INIT);

    if (!event_bus_init()) {
        Serial.println("[BOOT] FATAL: Event bus init failed!");
        while (1) { delay(1000); }
    }
    Serial.println("[BOOT] Event bus: OK");

    // ── 4. Drivers ──
    Serial.println("[BOOT] Initializing Drivers...");

    if (!pump_driver_init()) {
        Serial.println("[BOOT] FATAL: Pump driver init failed!");
        while (1) { delay(1000); }
    }
    Serial.println("[BOOT] Pump driver: OK (25kHz PWM, Active LOW)");

    button_driver_init();
    Serial.println("[BOOT] Button driver: OK (3 buttons, debounce)");

    led_driver_init();
    Serial.println("[BOOT] LED driver: OK");

    if (!sensor_driver_init()) {
        Serial.println("[BOOT] FATAL: Sensor driver init failed!");
        while (1) { delay(1000); }
    }
    Serial.println("[BOOT] Sensor driver: OK (GPIO35=INLET, GPIO39=OUTLET)");

    if (!lcd_driver_init()) {
        Serial.println("[BOOT] WARNING: LCD not found! Continuing without display.");
    } else {
        Serial.println("[BOOT] LCD driver: OK (I2C 20x4)");
        lcd_driver_show_startup();
    }

    // ── 5. Services (commented out — will implement later) ──
    Serial.println("[BOOT] Initializing Services...");
    // wifi_service_init(WIFI_SSID, WIFI_PASSWORD);
    // Serial.println("[BOOT] WiFi service: OK");

    // mqtt_service_init(MQTT_HOST, MQTT_PORT);
    // Serial.println("[BOOT] MQTT service: OK");
    Serial.println("[BOOT] Services: SKIPPED (WiFi/MQTT disabled)");

    // ── 6. Application ──
    Serial.println("[BOOT] Initializing Application...");
    recipe_engine_init();
    vending_controller_init();
    Serial.println("[BOOT] Vending controller: OK");

    // ── 7. Task Watchdog ──
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);  // timeout, panic on trigger
    Serial.printf("[BOOT] Watchdog: %d sec timeout\n", WDT_TIMEOUT_SEC);

    // ── 8. Create FreeRTOS Tasks ──
    Serial.println("[BOOT] Creating tasks...");

    // Core 1 (APP CPU) — Real-time control
    xTaskCreatePinnedToCore(button_task,       "button",   STACK_BUTTON,   NULL, 2, &h_button_task,   1);
    xTaskCreatePinnedToCore(sensor_task,       "sensor",   STACK_SENSOR,   NULL, 2, &h_sensor_task,   1);
    xTaskCreatePinnedToCore(vending_logic_task, "vending",  STACK_VENDING,  NULL, 3, &h_vending_task,  1);
    xTaskCreatePinnedToCore(pump_monitor_task,  "pump_mon", STACK_PUMP_MON, NULL, 1, &h_pump_mon_task, 1);

    // Core 0 (PRO CPU) — Network (commented out — will implement later)
    // xTaskCreatePinnedToCore(wifi_task,          "wifi",     STACK_WIFI,     NULL, 2, &h_wifi_task,     0);
    // xTaskCreatePinnedToCore(mqtt_task,          "mqtt",     STACK_MQTT,     NULL, 2, &h_mqtt_task,     0);

    Serial.println("[BOOT] Tasks created:");
    Serial.println("  Core 1: button(P2), sensor(P2), vending(P3), pump_mon(P1)");
    Serial.println("  Core 0: (WiFi/MQTT disabled)");

    // ── 9. System Ready ──
    system_state_set(SYS_STATE_READY);
    event_bus_publish(EVT_SYSTEM_READY, 0, 0);

    Serial.println();
    Serial.println("[BOOT] ====== SYSTEM READY ======");
    Serial.println("[BOOT] Press a button to dispense...\n");

    // Show ready screen on LCD after boot delay
    delay(1000);
    lcd_driver_show_ready();
}

void loop()
{
    // Main loop is not used — all logic runs in FreeRTOS tasks.
    // Keep Arduino loop alive with a long delay.
    vTaskDelay(pdMS_TO_TICKS(10000));
}