# 📋 Hướng Dẫn Triển Khai Vending Machine trên ESP-WROOM-32

## 📑 Mục Lục

1. [Phân Tích Dự Án STM32 Hiện Tại](#1-phân-tích-dự-án-stm32-hiện-tại)
2. [So Sánh STM32F103C8 vs ESP32-WROOM-32](#2-so-sánh-stm32f103c8-vs-esp32-wroom-32)
3. [Thiết Lập Môi Trường PlatformIO](#3-thiết-lập-môi-trường-platformio)
4. [Cấu Trúc Dự Án ESP32](#4-cấu-trúc-dự-án-esp32)
5. [Mapping Chân GPIO](#5-mapping-chân-gpio)
6. [Chuyển Đổi Code Chi Tiết](#6-chuyển-đổi-code-chi-tiết)
7. [Code ESP32 Hoàn Chỉnh](#7-code-esp32-hoàn-chỉnh)
8. [Hướng Dẫn Build và Flash](#8-hướng-dẫn-build-và-flash)
9. [Debug và Xử Lý Lỗi](#9-debug-và-xử-lý-lỗi)
10. [Mở Rộng Dự Án](#10-mở-rộng-dự-án)
11. [Kiến Trúc Firmware Chuẩn](#11-kiến-trúc-firmware-chuẩn)
12. [Thiết Kế Pump Driver Hardware](#12-thiết-kế-pump-driver-hardware)
13. [FreeRTOS Dual-Core Architecture](#13-freertos-dual-core-architecture)
14. [Sơ Đồ Kết Nối Phần Cứng Chi Tiết](#14-sơ-đồ-kết-nối-phần-cứng-chi-tiết)
15. [Giao Thức RS485 - Modbus RTU](#15-giao-thức-rs485---modbus-rtu)
16. [Logic Vận Hành Hệ Thống](#16-logic-vận-hành-hệ-thống)

---

## 1. Phân Tích Dự Án STM32 Hiện Tại

### 1.1. Tổng Quan Kiến Trúc

```
┌─────────────────────────────────────────────────────────────┐
│                    VENDING MACHINE FSM                       │
│                                                              │
│  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐   │
│  │   BUTTON    │───>│  VENDING_FSM │───>│     PUMP      │   │
│  │   MODULE    │    │   (5 states) │    │    DRIVER     │   │
│  └─────────────┘    └──────────────┘    └───────────────┘   │
│         │                  │                    │            │
│         │                  │                    │            │
│  ┌─────────────┐    ┌──────────────┐    ┌───────────────┐   │
│  │  5 Buttons  │    │ FLOW_CONTROL │───>│  FG Signal    │   │
│  │  (PB5-PB9)  │    │   (Volume)   │<───│  (Encoder)    │   │
│  └─────────────┘    └──────────────┘    └───────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 1.2. Các Module Chính

| Module           | File               | Chức Năng                      |
| ---------------- | ------------------ | ------------------------------ |
| **Main**         | `main.c`           | Khởi tạo, main loop            |
| **Vending FSM**  | `vending_fsm.c/h`  | Máy trạng thái điều khiển      |
| **Pump Driver**  | `pump.c/h`         | Điều khiển bơm PWM + Direction |
| **Pump Config**  | `pump_config.h`    | Cấu hình thông số bơm          |
| **Button**       | `button.c/h`       | Xử lý nút nhấn + debounce      |
| **Flow Control** | `flow_control.c/h` | Tính toán lưu lượng            |

### 1.3. Sơ Đồ FSM (Finite State Machine)

```
                         [START]
                            │
                            ▼
        ┌───────────────────────────────────────┐
        │              STATE_IDLE               │
        │          (Chờ nút nhấn)               │
        └───────────────────┬───────────────────┘
                            │
      ┌─────────┬───────────┼───────────┬─────────┐
      │         │           │           │         │
      ▼         ▼           ▼           ▼         ▼
┌──────────┐┌──────────┐┌──────────┐┌──────────┐┌──────────┐
│ STATE_1  ││ STATE_2  ││ STATE_3  ││ STATE_4  ││ STATE_5  │
│  (HOLD)  ││ (100ml)  ││ (20ml)   ││ (50ml)   ││ (100ml)  │
│  BTN_0   ││  BTN_1   ││  BTN_2   ││  BTN_3   ││  BTN_4   │
└────┬─────┘└────┬─────┘└────┬─────┘└────┬─────┘└────┬─────┘
     │           │           │           │           │
     │ Release   │ Complete  │ Complete  │ Complete  │ Complete
     └───────────┴───────────┴───────────┴───────────┘
                            │
                            ▼
                      STATE_IDLE
```

### 1.4. Thông Số Kỹ Thuật Bơm Leirong

| Thông số             | Giá trị       | Ghi chú                    |
| -------------------- | ------------- | -------------------------- |
| Điện áp              | 24V DC        | Nguồn ngoài                |
| Encoder PPR          | 12 pulses/rev | Xung trên mỗi vòng động cơ |
| Tỉ số giảm tốc       | 1:8           | Gear ratio                 |
| Xung FG/vòng đầu bơm | 96            | = 12 × 8                   |
| Lưu lượng/vòng       | 4.8837 ml     | = 2100/430 ml              |
| Lưu lượng/xung       | 0.0509 ml     | = 2100/(430×96) ml         |
| PWM Frequency        | 25 kHz        |                            |

### 1.5. Công Thức Tính Toán Lưu Lượng

```
1 vòng đầu bơm = 2100/430 ml ≈ 4.8837 ml
1 vòng đầu bơm = 96 xung FG
1 xung FG      = 2100/(430×96) ml ≈ 0.0509 ml ≈ 50.87 µl

Công thức integer (tránh float):
  microlit = pulses × 4375 / 86
  pulses   = microlit × 86 / 4375
```

---

## 2. So Sánh STM32F103C8 vs ESP32-WROOM-32

### 2.1. Bảng So Sánh Chi Tiết

| Tính năng         | STM32F103C8 (BluePill)  | ESP32-WROOM-32                |
| ----------------- | ----------------------- | ----------------------------- |
| **CPU**           | ARM Cortex-M3 @ 72MHz   | Xtensa LX6 Dual-Core @ 240MHz |
| **RAM**           | 20 KB                   | 520 KB                        |
| **Flash**         | 64/128 KB               | 4 MB                          |
| **GPIO**          | 37 pins                 | 34 pins                       |
| **PWM**           | Hardware Timer (TIM1-4) | LEDC (16 channels)            |
| **ADC**           | 2× 12-bit (10 channels) | 2× 12-bit (18 channels)       |
| **Input Capture** | Hardware Timer IC       | PCNT (Pulse Counter)          |
| **UART**          | 3                       | 3                             |
| **WiFi**          | ❌                       | ✅ 802.11 b/g/n                |
| **Bluetooth**     | ❌                       | ✅ BLE 4.2                     |
| **FPU**           | ❌                       | ✅                             |
| **FreeRTOS**      | Manual                  | Built-in                      |
| **Giá thành**     | ~$2-3                   | ~$3-5                         |

### 2.2. Ưu Điểm Khi Chuyển Sang ESP32

1. **WiFi/Bluetooth**: Có thể điều khiển từ xa qua smartphone
2. **Dual-Core**: Tách biệt task button/pump trên 2 core
3. **FPU**: Tính toán float nhanh hơn (không cần integer arithmetic)
4. **PCNT**: Hardware pulse counter, không cần interrupt
5. **Nhiều RAM/Flash**: Dễ mở rộng firmware

### 2.3. Các Thay Đổi Cần Thiết

| STM32 HAL               | ESP-IDF/Arduino                | Ghi chú          |
| ----------------------- | ------------------------------ | ---------------- |
| `HAL_GPIO_ReadPin()`    | `digitalRead()`                |                  |
| `HAL_GPIO_WritePin()`   | `digitalWrite()`               |                  |
| `HAL_Delay()`           | `delay()` / `vTaskDelay()`     |                  |
| `HAL_GetTick()`         | `millis()`                     |                  |
| `HAL_TIM_PWM_Start()`   | `ledcAttach()` + `ledcWrite()` |                  |
| `HAL_TIM_IC_Start_IT()` | `pcnt_unit_config()`           | Hardware counter |
| `printf()` → UART       | `Serial.printf()`              |                  |

---

## 3. Thiết Lập Môi Trường PlatformIO

### 3.1. Cài Đặt PlatformIO Extension

1. Mở **VSCode**
2. Vào **Extensions** (Ctrl+Shift+X)
3. Tìm kiếm "**PlatformIO IDE**"
4. Click **Install**
5. Restart VSCode

### 3.2. Tạo Dự Án Mới

**Cách 1: Qua PlatformIO Home**

1. Click icon **PlatformIO** trên sidebar (hình con kiến)
2. Chọn **Home** → **New Project**
3. Điền thông tin:
   - **Name**: `vending_machine_esp32`
   - **Board**: `Espressif ESP32 Dev Module` hoặc `esp32doit-devkit-v1`
   - **Framework**: `Arduino`
4. Click **Finish**

**Cách 2: Command Line**

```powershell
# Di chuyển đến thư mục dự án
cd D:\ABCsolution

# Tạo project mới
pio project init --board esp32doit-devkit-v1 --project-dir vending_machine_esp32

# Hoặc dùng template
pio project init --board esp32doit-devkit-v1 --project-option "framework=arduino" --project-dir vending_machine_esp32
```

### 3.3. Cấu Hình `platformio.ini`

```ini
; PlatformIO Configuration File for Vending Machine ESP32

[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino

; Serial Monitor
monitor_speed = 115200
monitor_filters = esp32_exception_decoder

; Build flags
build_flags = 
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_MODE=1
    -DPUMP_DEBUG=1

; Upload settings
upload_speed = 921600
upload_port = COM3  ; Thay đổi theo port của bạn

; Thư viện (nếu cần)
lib_deps = 
    ; Thêm thư viện nếu dùng WiFi/BLE sau này

; Các tùy chọn nâng cao
board_build.partitions = default.csv
board_build.flash_mode = dio
```

---

## 4. Cấu Trúc Dự Án ESP32

### 4.1. Cấu Trúc Thư Mục

```
vending_machine_esp32/
├── platformio.ini              # Cấu hình PlatformIO
├── include/                    # Header files
│   ├── config.h                # Cấu hình GPIO, thông số
│   ├── pump.h                  # Pump driver header
│   ├── button.h                # Button handler header
│   ├── flow_control.h          # Flow control header
│   └── vending_fsm.h           # FSM header
├── src/                        # Source files
│   ├── main.cpp                # Main application
│   ├── pump.cpp                # Pump driver
│   ├── button.cpp              # Button handler
│   ├── flow_control.cpp        # Flow control
│   └── vending_fsm.cpp         # FSM implementation
├── lib/                        # Project-specific libraries
│   └── README
├── test/                       # Unit tests
│   └── README
└── .gitignore
```

### 4.2. Tạo Cấu Trúc Tự Động

```powershell
# Di chuyển vào thư mục dự án
cd D:\ABCsolution\vending_machine_esp32

# Tạo các file header
New-Item -ItemType File -Path "include\config.h" -Force
New-Item -ItemType File -Path "include\pump.h" -Force
New-Item -ItemType File -Path "include\button.h" -Force
New-Item -ItemType File -Path "include\flow_control.h" -Force
New-Item -ItemType File -Path "include\vending_fsm.h" -Force

# Tạo các file source
New-Item -ItemType File -Path "src\main.cpp" -Force
New-Item -ItemType File -Path "src\pump.cpp" -Force
New-Item -ItemType File -Path "src\button.cpp" -Force
New-Item -ItemType File -Path "src\flow_control.cpp" -Force
New-Item -ItemType File -Path "src\vending_fsm.cpp" -Force
```

---

## 5. Mapping Chân GPIO

### 5.1. Bảng Chuyển Đổi Pin

| Chức năng          | STM32F103C8    | ESP32-WROOM-32 | Ghi chú             |
| ------------------ | -------------- | -------------- | ------------------- |
| **PWM (Motor)**    | PA8 (TIM1_CH1) | GPIO25         | LEDC Channel 0      |
| **Direction**      | PA1            | GPIO26         | Motor F/R           |
| **FG Signal**      | PA0 (TIM2_CH1) | GPIO27         | PCNT input          |
| **BTN_0 (Hold)**   | PB5            | GPIO32         | Pull-up, Long press |
| **BTN_1 (Volume)** | PB6            | GPIO33         | Pull-up             |
| **BTN_2 (Volume)** | PB7            | GPIO34         | Input only          |
| **BTN_3 (Volume)** | PB8            | GPIO35         | Input only          |
| **BTN_4 (Volume)** | PB9            | GPIO39/VN      | Input only          |
| **UART TX**        | PA9            | GPIO1/TX0      | Debug               |
| **UART RX**        | PA10           | GPIO3/RX0      | Debug               |

### 5.2. Sơ Đồ Kết Nối ESP32

```
                    ESP32-WROOM-32 DevKit
                    ┌──────────────────────┐
                    │                      │
              EN ───┤ EN            GPIO23 ├───
             3V3 ───┤ 3V3           GPIO22 ├───
              VP ───┤ GPIO36        GPIO1  ├─── TX (Debug)
              VN ───┤ GPIO39        GPIO3  ├─── RX (Debug)
          BTN_4 ───▶┤ GPIO34        GPIO21 ├───
          BTN_3 ───▶┤ GPIO35        GPIO19 ├───
          BTN_0 ───▶┤ GPIO32        GPIO18 ├───
          BTN_1 ───▶┤ GPIO33        GPIO5  ├───
       FG_SIGNAL ──▶┤ GPIO27        GPIO17 ├───
       DIRECTION ◀──┤ GPIO26        GPIO16 ├───
       PWM_MOTOR ◀──┤ GPIO25        GPIO4  ├───
                    │ GPIO0         GPIO2  ├─── (Built-in LED)
                    │                      │
             GND ───┤ GND           GND    ├─── GND
             5V  ───┤ VIN           3V3    ├───
                    └──────────────────────┘

Bơm Leirong 24V:
────────────────
  Pin 1 (Đỏ)  ──── +24V External
  Pin 2 (Đen) ──── GND Common
  Pin 3 (Lục) ◀── PWM (GPIO25 qua MOSFET/Driver)
  Pin 4 (Bạch) ◀── DIR (GPIO26)
  Pin 5 (Vàng) ──▶ FG (GPIO27)
```

### 5.3. Lưu Ý Quan Trọng Về GPIO ESP32

| GPIO      | Đặc điểm                              | Khuyến nghị                     |
| --------- | ------------------------------------- | ------------------------------- |
| GPIO34-39 | Input only, không có pull-up/down nội | Cần điện trở pull-up ngoài      |
| GPIO0, 2  | Boot mode pins                        | Tránh dùng cho input quan trọng |
| GPIO6-11  | SPI Flash (không dùng được)           | ❌ Không sử dụng                 |
| GPIO12    | Boot mode (phải LOW khi boot)         | Cẩn thận khi dùng               |

---

## 6. Chuyển Đổi Code Chi Tiết

### 6.1. Chuyển Đổi HAL Functions

#### GPIO

```cpp
// STM32 HAL
HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_5);
HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);

// ESP32 Arduino
digitalRead(32);     // GPIO32
digitalWrite(26, HIGH);  // GPIO26
```

#### PWM (Timer → LEDC)

```cpp
// STM32 HAL
TIM_HandleTypeDef htim1;
HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr_value);

// ESP32 Arduino (ESP-IDF 5.x / Arduino 3.x)
const int PWM_PIN = 25;
const int PWM_FREQ = 25000;    // 25 kHz
const int PWM_RESOLUTION = 12; // 12-bit (0-4095)
const int PWM_CHANNEL = 0;

// Khởi tạo (Arduino 3.x / ESP-IDF 5.x style)
ledcAttach(PWM_PIN, PWM_FREQ, PWM_RESOLUTION);

// Set duty cycle
ledcWrite(PWM_PIN, duty_value);  // 0-4095
```

#### Input Capture → PCNT (Pulse Counter)

```cpp
// STM32 HAL - Dùng Input Capture interrupt
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
    pulse_count++;
}

// ESP32 - Dùng PCNT (Hardware Pulse Counter)
#include "driver/pcnt.h"

#define PCNT_UNIT       PCNT_UNIT_0
#define PCNT_INPUT_PIN  27

void pcnt_init() {
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = PCNT_INPUT_PIN,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .channel = PCNT_CHANNEL_0,
        .unit = PCNT_UNIT,
        .pos_mode = PCNT_COUNT_INC,   // Count rising edge
        .neg_mode = PCNT_COUNT_DIS,   // Ignore falling edge
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = 32767,
        .counter_l_lim = -32768
    };
    
    pcnt_unit_config(&pcnt_config);
    pcnt_counter_pause(PCNT_UNIT);
    pcnt_counter_clear(PCNT_UNIT);
    pcnt_counter_resume(PCNT_UNIT);
}

int16_t get_pulse_count() {
    int16_t count;
    pcnt_get_counter_value(PCNT_UNIT, &count);
    return count;
}
```

#### Delay & Timing

```cpp
// STM32 HAL
HAL_Delay(100);
uint32_t tick = HAL_GetTick();

// ESP32 Arduino
delay(100);
unsigned long tick = millis();
```

### 6.2. Thay Đổi Kiểu Dữ Liệu

```cpp
// STM32
GPIO_TypeDef* port;
uint16_t pin;
HAL_StatusTypeDef status;

// ESP32
uint8_t gpio_pin;
int status; // hoặc esp_err_t
```

---

## 7. Code ESP32 Hoàn Chỉnh

### 7.1. `include/config.h`

```cpp
/**
 * @file    config.h
 * @brief   Cấu hình phần cứng cho Vending Machine ESP32
 */

#ifndef CONFIG_H
#define CONFIG_H

/* ============================================================================
 *                         GPIO PIN DEFINITIONS
 * ============================================================================ */

// PWM Motor Control
#define PIN_PWM_MOTOR       25      // LEDC output for motor PWM
#define PIN_MOTOR_DIR       26      // Motor direction (F/R)
#define PIN_FG_SIGNAL       27      // FG encoder input (PCNT)

// Buttons (với pull-up nội/ngoại)
#define PIN_BTN_0           32      // Hold button (has internal pull-up)
#define PIN_BTN_1           33      // Volume button 1
#define PIN_BTN_2           34      // Volume button 2 (need external pull-up)
#define PIN_BTN_3           35      // Volume button 3 (need external pull-up)
#define PIN_BTN_4           39      // Volume button 4 (need external pull-up)

// LED (built-in for debug)
#define PIN_LED_BUILTIN     2

/* ============================================================================
 *                         PWM CONFIGURATION
 * ============================================================================ */

#define PWM_FREQUENCY       25000   // 25 kHz
#define PWM_RESOLUTION      12      // 12-bit resolution (0-4095)
#define PWM_MAX_DUTY        4095    // Max duty value
#define PWM_CHANNEL         0       // LEDC channel (not used in new API)

/* ============================================================================
 *                         PUMP SPECIFICATIONS
 * ============================================================================ */

#define PUMP_ENCODER_PPR        12      // Pulses per motor revolution
#define PUMP_GEAR_RATIO         8       // Gear ratio 1:8
#define PUMP_FG_PULSES_PER_REV  (PUMP_ENCODER_PPR * PUMP_GEAR_RATIO)  // = 96

// Flow calculation (ml = 2100/430 per pump head revolution)
#define FLOW_ML_NUMERATOR       2100
#define FLOW_ML_DENOMINATOR     430
#define FLOW_PULSES_PER_REV     96

// Speed limits
#define PUMP_MIN_DUTY_PERCENT   0
#define PUMP_MAX_DUTY_PERCENT   100
#define PUMP_DIR_CHANGE_DELAY   50      // ms

/* ============================================================================
 *                         BUTTON CONFIGURATION
 * ============================================================================ */

#define NO_OF_BUTTONS           5
#define TIMER_CYCLE_MS          10      // Button scan period
#define DURATION_FOR_LONG_PRESS 200     // 200 × 10ms = 2 seconds
#define DURATION_FOR_AUTO_REPEAT 50     // 500ms auto-repeat

#define BUTTON_IS_PRESSED       LOW     // Active LOW (pull-up)
#define BUTTON_IS_RELEASED      HIGH

/* ============================================================================
 *                         BUTTON INDICES
 * ============================================================================ */

#define BTN_IDX_CONTINUOUS      0       // BTN_0: Hold để bơm liên tục
#define BTN_IDX_10ML            1       // BTN_1: Bơm 10ml/100ml
#define BTN_IDX_20ML            2       // BTN_2: Bơm 20ml
#define BTN_IDX_50ML            3       // BTN_3: Bơm 50ml
#define BTN_IDX_100ML           4       // BTN_4: Bơm 100ml

/* ============================================================================
 *                         TIMING CONFIGURATION
 * ============================================================================ */

#define FG_TIMEOUT_MS           500     // FG signal timeout
#define RPM_SAMPLE_PERIOD_MS    1000    // RPM calculation period

/* ============================================================================
 *                         DEBUG FLAGS
 * ============================================================================ */

#ifndef PUMP_DEBUG
#define PUMP_DEBUG              1       // Enable debug output
#endif

#endif // CONFIG_H
```

### 7.2. `include/pump.h`

```cpp
/**
 * @file    pump.h
 * @brief   Pump driver header for Leirong 24V pump on ESP32
 */

#ifndef PUMP_H
#define PUMP_H

#include <Arduino.h>
#include "config.h"

/* ============================================================================
 *                          ENUMS & STRUCTS
 * ============================================================================ */

typedef enum {
    PUMP_DIR_CW  = 0,   // Clockwise
    PUMP_DIR_CCW = 1    // Counter-clockwise
} Pump_Direction_t;

typedef enum {
    PUMP_STATE_STOPPED = 0,
    PUMP_STATE_RUNNING = 1,
    PUMP_STATE_ERROR   = 2
} Pump_State_t;

typedef struct {
    uint32_t pulse_count;           // Total FG pulses
    uint32_t motor_rpm;             // Motor RPM
    uint32_t pump_head_rpm;         // Pump head RPM
    float    pump_head_revolutions; // Total revolutions
    uint32_t pulse_period_us;       // Pulse period (µs)
    float    frequency_hz;          // FG frequency (Hz)
} Pump_FG_Data_t;

typedef struct {
    Pump_State_t     state;
    Pump_Direction_t direction;
    uint8_t          speed_percent;
    Pump_FG_Data_t   fg_data;
} Pump_Status_t;

/* ============================================================================
 *                          FUNCTION PROTOTYPES
 * ============================================================================ */

// Initialization
bool Pump_Init(void);
void Pump_DeInit(void);

// Basic control
bool Pump_Start(void);
bool Pump_Stop(void);
bool Pump_SetSpeed(uint8_t percent);
bool Pump_SetDirection(Pump_Direction_t direction);

// Advanced control
bool Pump_Run(uint8_t percent, Pump_Direction_t direction);
bool Pump_SoftStart(uint8_t target_percent, uint8_t step, uint32_t delay_ms);
bool Pump_SoftStop(uint8_t step, uint32_t delay_ms);

// Status
Pump_Status_t Pump_GetStatus(void);
uint8_t Pump_GetSpeed(void);
Pump_Direction_t Pump_GetDirection(void);
bool Pump_IsRunning(void);

// FG Signal handling
void Pump_FG_Update(void);          // Call periodically
Pump_FG_Data_t Pump_FG_GetData(void);
uint32_t Pump_GetPumpHeadRPM(void);
uint32_t Pump_GetMotorRPM(void);
float Pump_GetTotalRevolutions(void);
uint32_t Pump_GetPulseCount(void);
void Pump_CalculateRPM_Periodic(void);
void Pump_FG_Reset(void);
void Pump_ResetRevolutionCounter(void);
bool Pump_IsFGSignalActive(void);
uint32_t Pump_GetTimeSinceLastPulse(void);

// Integer helpers
uint32_t Pump_GetTotalRevolutions_Int(void);
uint32_t Pump_GetTotalRevolutions_Frac(void);

#endif // PUMP_H
```

### 7.3. `include/button.h`

```cpp
/**
 * @file    button.h
 * @brief   Button handler with debouncing and long-press detection
 */

#ifndef BUTTON_H
#define BUTTON_H

#include <Arduino.h>
#include "config.h"

/* ============================================================================
 *                          GLOBAL FLAGS
 * ============================================================================ */

extern volatile int button_flag[NO_OF_BUTTONS];
extern volatile int button_long_pressed_flag[NO_OF_BUTTONS];
extern volatile int button_released_flag[NO_OF_BUTTONS];

/* ============================================================================
 *                          FUNCTION PROTOTYPES
 * ============================================================================ */

void Button_Init(void);
void Button_Update(void);           // Call every 10ms
void Button_Process(void);          // Internal processing

// Flag management
void Button_ClearFlag(int index);
void Button_ClearLongPressFlag(int index);
void Button_ClearReleasedFlag(int index);
bool Button_IsPressed(int index);

#endif // BUTTON_H
```

### 7.4. `include/flow_control.h`

```cpp
/**
 * @file    flow_control.h
 * @brief   Flow control module for precise liquid dispensing
 */

#ifndef FLOW_CONTROL_H
#define FLOW_CONTROL_H

#include <Arduino.h>
#include "config.h"

/* ============================================================================
 *                          ENUMS & STRUCTS
 * ============================================================================ */

typedef enum {
    FLOW_STATE_IDLE = 0,
    FLOW_STATE_DISPENSING,
    FLOW_STATE_COMPLETE,
    FLOW_STATE_ERROR
} FlowControl_State_t;

typedef struct {
    uint32_t target_volume_ul;      // Target volume (microliters)
    uint32_t current_volume_ul;     // Current volume (microliters)
    uint32_t target_pulses;         // Target FG pulses
    uint32_t start_pulses;          // Starting pulse count
    uint32_t dispensed_pulses;      // Dispensed pulses
    FlowControl_State_t state;      // Current state
    uint8_t speed_percent;          // Pump speed
} FlowControl_Data_t;

/* ============================================================================
 *                          FUNCTION PROTOTYPES
 * ============================================================================ */

void FlowControl_Init(void);

// Target setting
bool FlowControl_SetTargetVolume_ml(uint32_t volume_ml);
bool FlowControl_SetTargetVolume_ul(uint32_t volume_ul);

// Control
bool FlowControl_StartDispense(uint8_t speed_percent);
void FlowControl_Stop(void);
void FlowControl_Reset(void);
void FlowControl_Update(void);

// Status
bool FlowControl_IsComplete(void);
bool FlowControl_IsDispensing(void);
FlowControl_State_t FlowControl_GetState(void);
FlowControl_Data_t FlowControl_GetData(void);

// Volume info
uint32_t FlowControl_GetDispensedVolume_ml(void);
uint32_t FlowControl_GetDispensedVolume_ul(void);
uint32_t FlowControl_GetRemainingVolume_ml(void);
uint8_t FlowControl_GetProgress(void);

// Utility
uint32_t FlowControl_PulsesToMicroliters(uint32_t pulses);
uint32_t FlowControl_MicrolitersToPulses(uint32_t microliters);

#endif // FLOW_CONTROL_H
```

### 7.5. `include/vending_fsm.h`

```cpp
/**
 * @file    vending_fsm.h
 * @brief   Finite State Machine for Vending Machine
 */

#ifndef VENDING_FSM_H
#define VENDING_FSM_H

#include <Arduino.h>
#include "config.h"

/* ============================================================================
 *                          ENUMS
 * ============================================================================ */

typedef enum {
    VM_STATE_IDLE = 0,
    VM_STATE_1,             // BTN_0 - Hold mode
    VM_STATE_2,             // BTN_1 - Volume mode
    VM_STATE_3,             // BTN_2 - Volume mode
    VM_STATE_4,             // BTN_3 - Volume mode
    VM_STATE_5,             // BTN_4 - Volume mode
    VM_STATE_ERROR,
    VM_STATE_COUNT
} VendingMachine_State_t;

typedef enum {
    VM_EVENT_NONE = 0,
    VM_EVENT_BTN0_HOLD,
    VM_EVENT_BTN0_RELEASE,
    VM_EVENT_BTN1_PRESS,
    VM_EVENT_BTN2_PRESS,
    VM_EVENT_BTN3_PRESS,
    VM_EVENT_BTN4_PRESS,
    VM_EVENT_COMPLETE,
    VM_EVENT_ERROR,
    VM_EVENT_RESET
} VendingMachine_Event_t;

typedef enum {
    STATE_MODE_HOLD = 0,    // Run while button held
    STATE_MODE_VOLUME       // Run until target volume
} StateMode_t;

/* ============================================================================
 *                          STRUCTS
 * ============================================================================ */

typedef struct {
    StateMode_t mode;
    uint32_t    volume_ml;
    uint8_t     speed_percent;
    const char* name;
} StateConfig_t;

typedef struct {
    StateConfig_t state_1;
    StateConfig_t state_2;
    StateConfig_t state_3;
    StateConfig_t state_4;
    StateConfig_t state_5;
} VendingMachine_Config_t;

/* ============================================================================
 *                          FUNCTION PROTOTYPES
 * ============================================================================ */

void VendingFSM_Init(void);
void VendingFSM_Update(void);

// Configuration
bool VendingFSM_ConfigState(VendingMachine_State_t state, const StateConfig_t* config);
void VendingFSM_SetSpeed(VendingMachine_State_t state, uint8_t speed_percent);
void VendingFSM_SetVolume(VendingMachine_State_t state, uint32_t volume_ml);

// Control
void VendingFSM_EmergencyStop(void);

// Status
VendingMachine_State_t VendingFSM_GetState(void);
const char* VendingFSM_GetStateName(void);

#endif // VENDING_FSM_H
```

### 7.6. `src/pump.cpp`

```cpp
/**
 * @file    pump.cpp
 * @brief   Pump driver implementation for ESP32
 */

#include "pump.h"
#include "driver/pcnt.h"

/* ============================================================================
 *                          PRIVATE VARIABLES
 * ============================================================================ */

static Pump_Status_t pump_status;
static bool is_initialized = false;

// PCNT (Pulse Counter) variables
static volatile int32_t total_pulse_count = 0;
static volatile int32_t pulse_count_for_rpm = 0;
static volatile uint32_t last_pulse_tick = 0;

// For frequency measurement
static volatile uint32_t last_pulse_time_us = 0;
static volatile uint32_t pulse_period_us = 0;

// PCNT overflow counter
static volatile int32_t pcnt_overflow_count = 0;

/* ============================================================================
 *                          PCNT ISR HANDLER
 * ============================================================================ */

static void IRAM_ATTR pcnt_intr_handler(void* arg) {
    uint32_t intr_status = PCNT.int_st.val;
    
    if (intr_status & (BIT(PCNT_UNIT_0))) {
        // Handle overflow/underflow
        if (PCNT.status_unit[PCNT_UNIT_0].h_lim_lat) {
            pcnt_overflow_count++;
        }
        PCNT.int_clr.val = BIT(PCNT_UNIT_0);
    }
}

/* ============================================================================
 *                          GPIO ISR FOR FREQUENCY
 * ============================================================================ */

static void IRAM_ATTR fg_isr_handler(void* arg) {
    uint32_t current_time = micros();
    
    if (last_pulse_time_us > 0) {
        pulse_period_us = current_time - last_pulse_time_us;
    }
    last_pulse_time_us = current_time;
    last_pulse_tick = millis();
    
    // Manual pulse counting (backup if PCNT has issues)
    total_pulse_count++;
    pulse_count_for_rpm++;
}

/* ============================================================================
 *                          INITIALIZATION
 * ============================================================================ */

bool Pump_Init(void) {
    // Initialize PWM using LEDC (ESP-IDF 5.x / Arduino 3.x style)
    if (!ledcAttach(PIN_PWM_MOTOR, PWM_FREQUENCY, PWM_RESOLUTION)) {
        Serial.println("[PUMP] PWM init failed!");
        return false;
    }
    ledcWrite(PIN_PWM_MOTOR, 0);  // Start with 0% duty
    
    // Initialize direction pin
    pinMode(PIN_MOTOR_DIR, OUTPUT);
    digitalWrite(PIN_MOTOR_DIR, HIGH);  // CW direction
    
    // Initialize FG signal pin with interrupt
    pinMode(PIN_FG_SIGNAL, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_FG_SIGNAL), fg_isr_handler, RISING);
    
    /* PCNT Configuration (Alternative hardware counter)
     * Uncomment below if you want to use PCNT instead of GPIO interrupt
     */
    /*
    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = PIN_FG_SIGNAL,
        .ctrl_gpio_num = PCNT_PIN_NOT_USED,
        .channel = PCNT_CHANNEL_0,
        .unit = PCNT_UNIT_0,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DIS,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .counter_h_lim = 10000,
        .counter_l_lim = 0
    };
    
    pcnt_unit_config(&pcnt_config);
    pcnt_event_enable(PCNT_UNIT_0, PCNT_EVT_H_LIM);
    pcnt_isr_service_install(0);
    pcnt_isr_handler_add(PCNT_UNIT_0, pcnt_intr_handler, NULL);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_intr_enable(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
    */
    
    // Initialize status
    pump_status.state = PUMP_STATE_STOPPED;
    pump_status.direction = PUMP_DIR_CW;
    pump_status.speed_percent = 0;
    
    pump_status.fg_data.pulse_count = 0;
    pump_status.fg_data.motor_rpm = 0;
    pump_status.fg_data.pump_head_rpm = 0;
    pump_status.fg_data.pump_head_revolutions = 0.0f;
    pump_status.fg_data.pulse_period_us = 0;
    pump_status.fg_data.frequency_hz = 0.0f;
    
    total_pulse_count = 0;
    pulse_count_for_rpm = 0;
    last_pulse_tick = 0;
    
    is_initialized = true;
    
    #if PUMP_DEBUG
    Serial.println("[PUMP] Initialized successfully");
    #endif
    
    return true;
}

void Pump_DeInit(void) {
    if (!is_initialized) return;
    
    Pump_Stop();
    detachInterrupt(digitalPinToInterrupt(PIN_FG_SIGNAL));
    ledcDetach(PIN_PWM_MOTOR);
    
    is_initialized = false;
}

/* ============================================================================
 *                          BASIC CONTROL
 * ============================================================================ */

bool Pump_Start(void) {
    if (!is_initialized) return false;
    
    pump_status.state = PUMP_STATE_RUNNING;
    return true;
}

bool Pump_Stop(void) {
    if (!is_initialized) return false;
    
    ledcWrite(PIN_PWM_MOTOR, 0);
    pump_status.speed_percent = 0;
    pump_status.state = PUMP_STATE_STOPPED;
    
    return true;
}

bool Pump_SetSpeed(uint8_t percent) {
    if (!is_initialized) return false;
    
    if (percent > PUMP_MAX_DUTY_PERCENT) percent = PUMP_MAX_DUTY_PERCENT;
    
    // Convert percent to duty cycle (12-bit: 0-4095)
    uint32_t duty = (percent * PWM_MAX_DUTY) / 100;
    ledcWrite(PIN_PWM_MOTOR, duty);
    
    pump_status.speed_percent = percent;
    return true;
}

bool Pump_SetDirection(Pump_Direction_t direction) {
    if (!is_initialized) return false;
    
    uint8_t current_speed = pump_status.speed_percent;
    
    // Stop first if running
    if (current_speed > 0) {
        Pump_SetSpeed(0);
        delay(PUMP_DIR_CHANGE_DELAY);
    }
    
    // Set direction
    digitalWrite(PIN_MOTOR_DIR, (direction == PUMP_DIR_CW) ? HIGH : LOW);
    pump_status.direction = direction;
    
    // Restore speed
    if (current_speed > 0) {
        delay(PUMP_DIR_CHANGE_DELAY);
        Pump_SetSpeed(current_speed);
    }
    
    return true;
}

/* ============================================================================
 *                          ADVANCED CONTROL
 * ============================================================================ */

bool Pump_Run(uint8_t percent, Pump_Direction_t direction) {
    if (!Pump_SetDirection(direction)) return false;
    
    if (pump_status.state != PUMP_STATE_RUNNING) {
        if (!Pump_Start()) return false;
    }
    
    return Pump_SetSpeed(percent);
}

bool Pump_SoftStart(uint8_t target_percent, uint8_t step, uint32_t delay_ms) {
    if (!is_initialized) return false;
    
    if (pump_status.state != PUMP_STATE_RUNNING) {
        if (!Pump_Start()) return false;
    }
    
    uint8_t current = pump_status.speed_percent;
    
    while (current < target_percent) {
        current += step;
        if (current > target_percent) current = target_percent;
        Pump_SetSpeed(current);
        delay(delay_ms);
    }
    
    return true;
}

bool Pump_SoftStop(uint8_t step, uint32_t delay_ms) {
    if (!is_initialized) return false;
    
    int16_t current = (int16_t)pump_status.speed_percent;
    
    while (current > 0) {
        current -= step;
        if (current < 0) current = 0;
        Pump_SetSpeed((uint8_t)current);
        delay(delay_ms);
    }
    
    return true;
}

/* ============================================================================
 *                          STATUS FUNCTIONS
 * ============================================================================ */

Pump_Status_t Pump_GetStatus(void) {
    return pump_status;
}

uint8_t Pump_GetSpeed(void) {
    return pump_status.speed_percent;
}

Pump_Direction_t Pump_GetDirection(void) {
    return pump_status.direction;
}

bool Pump_IsRunning(void) {
    return (pump_status.state == PUMP_STATE_RUNNING && 
            pump_status.speed_percent > 0);
}

/* ============================================================================
 *                          FG SIGNAL FUNCTIONS
 * ============================================================================ */

void Pump_CalculateRPM_Periodic(void) {
    // Critical section - disable interrupts
    noInterrupts();
    uint32_t pulses = pulse_count_for_rpm;
    pulse_count_for_rpm = 0;
    uint32_t period_us = pulse_period_us;
    interrupts();
    
    // Check if FG signal is active
    if (!Pump_IsFGSignalActive()) {
        pump_status.fg_data.motor_rpm = 0;
        pump_status.fg_data.pump_head_rpm = 0;
        pump_status.fg_data.frequency_hz = 0;
        pump_status.fg_data.pulse_period_us = 0;
        return;
    }
    
    // Calculate RPM (based on 1 second sample)
    pump_status.fg_data.motor_rpm = (pulses * 60) / PUMP_ENCODER_PPR;
    pump_status.fg_data.pump_head_rpm = (pulses * 60) / PUMP_FG_PULSES_PER_REV;
    
    // Update counts
    pump_status.fg_data.pulse_count = total_pulse_count;
    pump_status.fg_data.pump_head_revolutions = (float)total_pulse_count / 
                                                 (float)PUMP_FG_PULSES_PER_REV;
    
    // Calculate frequency
    pump_status.fg_data.pulse_period_us = period_us;
    if (period_us > 0) {
        pump_status.fg_data.frequency_hz = 1000000.0f / (float)period_us;
    }
}

Pump_FG_Data_t Pump_FG_GetData(void) {
    return pump_status.fg_data;
}

uint32_t Pump_GetPumpHeadRPM(void) {
    return pump_status.fg_data.pump_head_rpm;
}

uint32_t Pump_GetMotorRPM(void) {
    return pump_status.fg_data.motor_rpm;
}

float Pump_GetTotalRevolutions(void) {
    return (float)total_pulse_count / (float)PUMP_FG_PULSES_PER_REV;
}

uint32_t Pump_GetPulseCount(void) {
    return total_pulse_count;
}

bool Pump_IsFGSignalActive(void) {
    return (millis() - last_pulse_tick) < FG_TIMEOUT_MS;
}

uint32_t Pump_GetTimeSinceLastPulse(void) {
    return millis() - last_pulse_tick;
}

void Pump_FG_Reset(void) {
    noInterrupts();
    total_pulse_count = 0;
    pulse_count_for_rpm = 0;
    last_pulse_tick = 0;
    last_pulse_time_us = 0;
    pulse_period_us = 0;
    pcnt_overflow_count = 0;
    
    pump_status.fg_data.pulse_count = 0;
    pump_status.fg_data.motor_rpm = 0;
    pump_status.fg_data.pump_head_rpm = 0;
    pump_status.fg_data.pump_head_revolutions = 0.0f;
    pump_status.fg_data.pulse_period_us = 0;
    pump_status.fg_data.frequency_hz = 0.0f;
    interrupts();
}

void Pump_ResetRevolutionCounter(void) {
    noInterrupts();
    total_pulse_count = 0;
    pump_status.fg_data.pulse_count = 0;
    pump_status.fg_data.pump_head_revolutions = 0.0f;
    interrupts();
}

uint32_t Pump_GetTotalRevolutions_Int(void) {
    return (uint32_t)(total_pulse_count / PUMP_FG_PULSES_PER_REV);
}

uint32_t Pump_GetTotalRevolutions_Frac(void) {
    uint32_t remainder = total_pulse_count % PUMP_FG_PULSES_PER_REV;
    return (remainder * 100) / PUMP_FG_PULSES_PER_REV;
}
```

### 7.7. `src/button.cpp`

```cpp
/**
 * @file    button.cpp
 * @brief   Button handler implementation for ESP32
 */

#include "button.h"

/* ============================================================================
 *                          PRIVATE VARIABLES
 * ============================================================================ */

// Button GPIO pins array
static const uint8_t BUTTON_PINS[NO_OF_BUTTONS] = {
    PIN_BTN_0,  // GPIO32 - Hold button
    PIN_BTN_1,  // GPIO33
    PIN_BTN_2,  // GPIO34 (input only)
    PIN_BTN_3,  // GPIO35 (input only)
    PIN_BTN_4   // GPIO39 (input only)
};

// Debounce registers
static int KeyReg0[NO_OF_BUTTONS];
static int KeyReg1[NO_OF_BUTTONS];
static int KeyReg2[NO_OF_BUTTONS];
static int KeyReg3[NO_OF_BUTTONS];

// Long press handling
static int TimeOutForKeyPress[NO_OF_BUTTONS];
static int LongPressTriggered[NO_OF_BUTTONS];

// Global flags
volatile int button_flag[NO_OF_BUTTONS] = {0};
volatile int button_long_pressed_flag[NO_OF_BUTTONS] = {0};
volatile int button_released_flag[NO_OF_BUTTONS] = {0};

// Timing
static uint32_t last_process_tick = 0;

/* ============================================================================
 *                          PRIVATE FUNCTIONS
 * ============================================================================ */

static void subKeyProcess(int index) {
    button_flag[index] = 1;
}

static void subLongKeyProcess(int index) {
    button_long_pressed_flag[index] = 1;
}

static void subKeyReleaseProcess(int index) {
    button_released_flag[index] = 1;
    button_long_pressed_flag[index] = 0;
}

/* ============================================================================
 *                          PUBLIC FUNCTIONS
 * ============================================================================ */

void Button_Init(void) {
    // Initialize GPIO pins
    for (int i = 0; i < NO_OF_BUTTONS; i++) {
        // GPIO34-39 are input-only and don't have internal pull-up
        if (BUTTON_PINS[i] >= 34) {
            pinMode(BUTTON_PINS[i], INPUT);  // Need external pull-up
        } else {
            pinMode(BUTTON_PINS[i], INPUT_PULLUP);
        }
        
        // Initialize registers
        KeyReg0[i] = BUTTON_IS_RELEASED;
        KeyReg1[i] = BUTTON_IS_RELEASED;
        KeyReg2[i] = BUTTON_IS_RELEASED;
        KeyReg3[i] = BUTTON_IS_RELEASED;
        TimeOutForKeyPress[i] = 0;
        LongPressTriggered[i] = 0;
        button_flag[i] = 0;
        button_long_pressed_flag[i] = 0;
        button_released_flag[i] = 0;
    }
    
    last_process_tick = millis();
    
    #if PUMP_DEBUG
    Serial.println("[BUTTON] Initialized");
    #endif
}

void Button_Process(void) {
    for (int i = 0; i < NO_OF_BUTTONS; i++) {
        // Phase 1: Debouncing
        KeyReg0[i] = KeyReg1[i];
        KeyReg1[i] = KeyReg2[i];
        KeyReg2[i] = digitalRead(BUTTON_PINS[i]);
        
        // Check stability: 3 consecutive same readings
        if ((KeyReg0[i] == KeyReg1[i]) && (KeyReg1[i] == KeyReg2[i])) {
            
            // Phase 2: FSM for long press detection
            if (KeyReg3[i] != KeyReg2[i]) {
                KeyReg3[i] = KeyReg2[i];
                
                if (KeyReg2[i] == BUTTON_IS_PRESSED) {
                    // Just pressed
                    subKeyProcess(i);
                    TimeOutForKeyPress[i] = DURATION_FOR_LONG_PRESS;
                    LongPressTriggered[i] = 0;
                } else {
                    // Just released
                    if (LongPressTriggered[i]) {
                        subKeyReleaseProcess(i);
                    }
                    LongPressTriggered[i] = 0;
                    TimeOutForKeyPress[i] = 0;
                }
            } else {
                // State unchanged
                if (KeyReg2[i] == BUTTON_IS_PRESSED) {
                    // Button held
                    if (TimeOutForKeyPress[i] > 0) {
                        TimeOutForKeyPress[i]--;
                        
                        if (TimeOutForKeyPress[i] == 0) {
                            subLongKeyProcess(i);
                            LongPressTriggered[i] = 1;
                            TimeOutForKeyPress[i] = DURATION_FOR_AUTO_REPEAT;
                        }
                    }
                }
            }
        }
    }
}

void Button_Update(void) {
    uint32_t current_tick = millis();
    
    if (current_tick - last_process_tick >= TIMER_CYCLE_MS) {
        last_process_tick = current_tick;
        Button_Process();
    }
}

void Button_ClearFlag(int index) {
    if (index >= 0 && index < NO_OF_BUTTONS) {
        button_flag[index] = 0;
    }
}

void Button_ClearLongPressFlag(int index) {
    if (index >= 0 && index < NO_OF_BUTTONS) {
        button_long_pressed_flag[index] = 0;
    }
}

void Button_ClearReleasedFlag(int index) {
    if (index >= 0 && index < NO_OF_BUTTONS) {
        button_released_flag[index] = 0;
    }
}

bool Button_IsPressed(int index) {
    if (index >= 0 && index < NO_OF_BUTTONS) {
        return (digitalRead(BUTTON_PINS[index]) == BUTTON_IS_PRESSED);
    }
    return false;
}
```

### 7.8. `src/flow_control.cpp`

```cpp
/**
 * @file    flow_control.cpp
 * @brief   Flow control implementation for ESP32
 */

#include "flow_control.h"
#include "pump.h"

/* ============================================================================
 *                          CONSTANTS
 * ============================================================================ */

// Optimized calculation constants (avoid overflow)
// 1 pulse = 2100/(430×96) ml = 4375/86 µl (reduced fraction)
#define VOLUME_NUMERATOR_REDUCED    4375UL
#define VOLUME_DENOMINATOR_REDUCED  86UL

/* ============================================================================
 *                          PRIVATE VARIABLES
 * ============================================================================ */

static FlowControl_Data_t flow_data;
static bool is_initialized = false;

/* ============================================================================
 *                          PRIVATE FUNCTIONS
 * ============================================================================ */

static uint32_t DivideWithRounding(uint32_t numerator, uint32_t denominator) {
    if (denominator == 0) return 0;
    return (numerator + (denominator / 2)) / denominator;
}

/* ============================================================================
 *                          INITIALIZATION
 * ============================================================================ */

void FlowControl_Init(void) {
    flow_data.target_volume_ul = 0;
    flow_data.current_volume_ul = 0;
    flow_data.target_pulses = 0;
    flow_data.start_pulses = 0;
    flow_data.dispensed_pulses = 0;
    flow_data.state = FLOW_STATE_IDLE;
    flow_data.speed_percent = 0;
    
    is_initialized = true;
    
    #if PUMP_DEBUG
    Serial.println("[FLOW] Initialized");
    #endif
}

/* ============================================================================
 *                          TARGET SETTING
 * ============================================================================ */

bool FlowControl_SetTargetVolume_ml(uint32_t volume_ml) {
    return FlowControl_SetTargetVolume_ul(volume_ml * 1000);
}

bool FlowControl_SetTargetVolume_ul(uint32_t volume_ul) {
    if (!is_initialized) return false;
    if (volume_ul == 0 || volume_ul > 9999999) return false;
    if (flow_data.state == FLOW_STATE_DISPENSING) return false;
    
    flow_data.target_volume_ul = volume_ul;
    
    // Calculate required pulses
    // pulses = volume_ul × 86 / 4375
    flow_data.target_pulses = DivideWithRounding(
        volume_ul * VOLUME_DENOMINATOR_REDUCED,
        VOLUME_NUMERATOR_REDUCED
    );
    
    flow_data.current_volume_ul = 0;
    flow_data.dispensed_pulses = 0;
    flow_data.state = FLOW_STATE_IDLE;
    
    #if PUMP_DEBUG
    Serial.printf("[FLOW] Target: %lu ul (%lu pulses)\n", 
                  volume_ul, flow_data.target_pulses);
    #endif
    
    return true;
}

/* ============================================================================
 *                          CONTROL FUNCTIONS
 * ============================================================================ */

bool FlowControl_StartDispense(uint8_t speed_percent) {
    if (!is_initialized) return false;
    if (flow_data.target_pulses == 0) return false;
    if (speed_percent == 0 || speed_percent > 100) return false;
    
    // Reset counter before starting
    Pump_ResetRevolutionCounter();
    
    flow_data.start_pulses = Pump_GetPulseCount();
    flow_data.dispensed_pulses = 0;
    flow_data.current_volume_ul = 0;
    flow_data.speed_percent = speed_percent;
    flow_data.state = FLOW_STATE_DISPENSING;
    
    // Start pump
    if (!Pump_Run(speed_percent, PUMP_DIR_CW)) {
        flow_data.state = FLOW_STATE_ERROR;
        return false;
    }
    
    #if PUMP_DEBUG
    Serial.printf("[FLOW] Dispensing started at %d%%\n", speed_percent);
    #endif
    
    return true;
}

void FlowControl_Stop(void) {
    if (!is_initialized) return;
    
    Pump_SoftStop(10, 20);
    FlowControl_Update();
    
    if (flow_data.state == FLOW_STATE_DISPENSING) {
        flow_data.state = FLOW_STATE_IDLE;
    }
}

void FlowControl_Reset(void) {
    if (!is_initialized) return;
    
    Pump_Stop();
    
    flow_data.target_volume_ul = 0;
    flow_data.current_volume_ul = 0;
    flow_data.target_pulses = 0;
    flow_data.start_pulses = 0;
    flow_data.dispensed_pulses = 0;
    flow_data.state = FLOW_STATE_IDLE;
    flow_data.speed_percent = 0;
    
    Pump_FG_Reset();
}

void FlowControl_Update(void) {
    if (!is_initialized) return;
    if (flow_data.state != FLOW_STATE_DISPENSING) return;
    
    uint32_t current_pulses = Pump_GetPulseCount();
    
    // Calculate dispensed pulses
    if (current_pulses >= flow_data.start_pulses) {
        flow_data.dispensed_pulses = current_pulses - flow_data.start_pulses;
    } else {
        flow_data.dispensed_pulses = current_pulses;
    }
    
    // Calculate volume in µl
    flow_data.current_volume_ul = DivideWithRounding(
        flow_data.dispensed_pulses * VOLUME_NUMERATOR_REDUCED,
        VOLUME_DENOMINATOR_REDUCED
    );
    
    // Check if target reached
    if (flow_data.dispensed_pulses >= flow_data.target_pulses) {
        Pump_SoftStop(20, 10);
        flow_data.state = FLOW_STATE_COMPLETE;
        
        #if PUMP_DEBUG
        Serial.printf("[FLOW] Complete! Dispensed: %lu ul\n", 
                      flow_data.current_volume_ul);
        #endif
    }
    
    // Check for errors (no FG signal)
    static uint32_t no_signal_count = 0;
    if (Pump_IsRunning() && !Pump_IsFGSignalActive()) {
        no_signal_count++;
        if (no_signal_count > 50) {
            Pump_Stop();
            flow_data.state = FLOW_STATE_ERROR;
            no_signal_count = 0;
            Serial.println("[FLOW] ERROR: No FG signal!");
        }
    } else {
        no_signal_count = 0;
    }
}

/* ============================================================================
 *                          STATUS FUNCTIONS
 * ============================================================================ */

bool FlowControl_IsComplete(void) {
    return (flow_data.state == FLOW_STATE_COMPLETE);
}

bool FlowControl_IsDispensing(void) {
    return (flow_data.state == FLOW_STATE_DISPENSING);
}

FlowControl_State_t FlowControl_GetState(void) {
    return flow_data.state;
}

FlowControl_Data_t FlowControl_GetData(void) {
    return flow_data;
}

uint32_t FlowControl_GetDispensedVolume_ml(void) {
    return DivideWithRounding(flow_data.current_volume_ul, 1000);
}

uint32_t FlowControl_GetDispensedVolume_ul(void) {
    return flow_data.current_volume_ul;
}

uint32_t FlowControl_GetRemainingVolume_ml(void) {
    if (flow_data.current_volume_ul >= flow_data.target_volume_ul) {
        return 0;
    }
    uint32_t remaining_ul = flow_data.target_volume_ul - flow_data.current_volume_ul;
    return DivideWithRounding(remaining_ul, 1000);
}

uint8_t FlowControl_GetProgress(void) {
    if (flow_data.target_pulses == 0) return 0;
    uint32_t progress = (flow_data.dispensed_pulses * 100) / flow_data.target_pulses;
    if (progress > 100) progress = 100;
    return (uint8_t)progress;
}

/* ============================================================================
 *                          UTILITY FUNCTIONS
 * ============================================================================ */

uint32_t FlowControl_PulsesToMicroliters(uint32_t pulses) {
    return DivideWithRounding(
        pulses * VOLUME_NUMERATOR_REDUCED,
        VOLUME_DENOMINATOR_REDUCED
    );
}

uint32_t FlowControl_MicrolitersToPulses(uint32_t microliters) {
    return DivideWithRounding(
        microliters * VOLUME_DENOMINATOR_REDUCED,
        VOLUME_NUMERATOR_REDUCED
    );
}
```

### 7.9. `src/vending_fsm.cpp`

```cpp
/**
 * @file    vending_fsm.cpp
 * @brief   FSM implementation for ESP32 Vending Machine
 */

#include "vending_fsm.h"
#include "button.h"
#include "pump.h"
#include "flow_control.h"

/* ============================================================================
 *                          PRIVATE VARIABLES
 * ============================================================================ */

static VendingMachine_Config_t fsm_config = {
    .state_1 = {
        .mode = STATE_MODE_HOLD,
        .volume_ml = 0,
        .speed_percent = 70,
        .name = "HOLD"
    },
    .state_2 = {
        .mode = STATE_MODE_VOLUME,
        .volume_ml = 100,
        .speed_percent = 80,
        .name = "100ml"
    },
    .state_3 = {
        .mode = STATE_MODE_VOLUME,
        .volume_ml = 20,
        .speed_percent = 65,
        .name = "20ml"
    },
    .state_4 = {
        .mode = STATE_MODE_VOLUME,
        .volume_ml = 50,
        .speed_percent = 70,
        .name = "50ml"
    },
    .state_5 = {
        .mode = STATE_MODE_VOLUME,
        .volume_ml = 100,
        .speed_percent = 20,
        .name = "100ml"
    }
};

static VendingMachine_State_t current_state = VM_STATE_IDLE;
static VendingMachine_State_t previous_state = VM_STATE_IDLE;
static uint32_t state_enter_time = 0;
static uint32_t total_dispensed_ml = 0;
static uint32_t dispense_count = 0;
static bool is_initialized = false;

static const char* STATE_NAMES[] = {
    "IDLE", "STATE_1", "STATE_2", "STATE_3", 
    "STATE_4", "STATE_5", "ERROR"
};

/* ============================================================================
 *                          PRIVATE FUNCTIONS
 * ============================================================================ */

static StateConfig_t* GetStateConfig(VendingMachine_State_t state) {
    switch (state) {
        case VM_STATE_1: return &fsm_config.state_1;
        case VM_STATE_2: return &fsm_config.state_2;
        case VM_STATE_3: return &fsm_config.state_3;
        case VM_STATE_4: return &fsm_config.state_4;
        case VM_STATE_5: return &fsm_config.state_5;
        default: return NULL;
    }
}

static void FSM_TransitionTo(VendingMachine_State_t new_state) {
    if (new_state >= VM_STATE_COUNT) return;
    if (new_state == current_state) return;
    
    previous_state = current_state;
    uint32_t duration = millis() - state_enter_time;
    
    Serial.printf("[FSM] %s -> %s (after %lu ms)\n",
                  STATE_NAMES[previous_state],
                  STATE_NAMES[new_state],
                  duration);
    
    current_state = new_state;
    state_enter_time = millis();
}

static void FSM_OnEnterState(VendingMachine_State_t state) {
    StateConfig_t* config = GetStateConfig(state);
    
    switch (state) {
        case VM_STATE_IDLE:
            if (Pump_IsRunning()) {
                Pump_SoftStop(20, 20);
            }
            Serial.println("[FSM] Ready! Waiting for button...\n");
            break;
            
        case VM_STATE_1:
        case VM_STATE_2:
        case VM_STATE_3:
        case VM_STATE_4:
        case VM_STATE_5:
            if (config == NULL) break;
            
            if (config->mode == STATE_MODE_HOLD) {
                Pump_Run(config->speed_percent, PUMP_DIR_CW);
                Serial.printf("[FSM] %s @ %d%% - Release to stop\n",
                             config->name, config->speed_percent);
            } else {
                Serial.printf("[FSM] Start dispensing %s @ %d%%\n",
                             config->name, config->speed_percent);
                FlowControl_SetTargetVolume_ml(config->volume_ml);
                FlowControl_StartDispense(config->speed_percent);
            }
            break;
            
        case VM_STATE_ERROR:
            Pump_Stop();
            Serial.println("[FSM] ERROR! Pump stopped.");
            break;
            
        default:
            break;
    }
}

static void FSM_OnExitState(VendingMachine_State_t state) {
    StateConfig_t* config = GetStateConfig(state);
    
    switch (state) {
        case VM_STATE_1:
        case VM_STATE_2:
        case VM_STATE_3:
        case VM_STATE_4:
        case VM_STATE_5:
            if (config == NULL) break;
            
            if (config->mode == STATE_MODE_HOLD) {
                Pump_SoftStop(20, 20);
                Serial.println("[FSM] Stopped.");
            } else {
                uint32_t dispensed_ml = FlowControl_GetDispensedVolume_ml();
                FlowControl_Stop();
                total_dispensed_ml += dispensed_ml;
                dispense_count++;
                Serial.printf("[FSM] Done! Dispensed: %lu ml (Total: %lu ml, %lu times)\n",
                             dispensed_ml, total_dispensed_ml, dispense_count);
            }
            break;
            
        default:
            break;
    }
}

static void FSM_ProcessEvent(VendingMachine_Event_t event) {
    if (!is_initialized) return;
    
    VendingMachine_State_t next_state = current_state;
    
    switch (current_state) {
        case VM_STATE_IDLE:
            switch (event) {
                case VM_EVENT_BTN0_HOLD:  next_state = VM_STATE_1; break;
                case VM_EVENT_BTN1_PRESS: next_state = VM_STATE_2; break;
                case VM_EVENT_BTN2_PRESS: next_state = VM_STATE_3; break;
                case VM_EVENT_BTN3_PRESS: next_state = VM_STATE_4; break;
                case VM_EVENT_BTN4_PRESS: next_state = VM_STATE_5; break;
                default: break;
            }
            break;
            
        case VM_STATE_1:
            if (event == VM_EVENT_BTN0_RELEASE) {
                next_state = VM_STATE_IDLE;
            }
            break;
            
        case VM_STATE_2:
        case VM_STATE_3:
        case VM_STATE_4:
        case VM_STATE_5:
            if (event == VM_EVENT_COMPLETE) {
                next_state = VM_STATE_IDLE;
            } else if (event == VM_EVENT_ERROR) {
                next_state = VM_STATE_ERROR;
            }
            break;
            
        case VM_STATE_ERROR:
            if (event == VM_EVENT_RESET) {
                next_state = VM_STATE_IDLE;
            }
            break;
            
        default:
            break;
    }
    
    if (next_state != current_state) {
        FSM_OnExitState(current_state);
        FSM_TransitionTo(next_state);
        FSM_OnEnterState(next_state);
    }
}

static VendingMachine_Event_t FSM_ReadButtonEvent(void) {
    Button_Update();
    
    // BTN_0 - Long Press / Release
    if (button_long_pressed_flag[BTN_IDX_CONTINUOUS]) {
        Button_ClearLongPressFlag(BTN_IDX_CONTINUOUS);
        return VM_EVENT_BTN0_HOLD;
    }
    if (button_released_flag[BTN_IDX_CONTINUOUS]) {
        Button_ClearReleasedFlag(BTN_IDX_CONTINUOUS);
        return VM_EVENT_BTN0_RELEASE;
    }
    if (button_flag[BTN_IDX_CONTINUOUS]) {
        Button_ClearFlag(BTN_IDX_CONTINUOUS);
    }
    
    // BTN_1-4 - Normal Press
    if (button_flag[BTN_IDX_10ML]) {
        Button_ClearFlag(BTN_IDX_10ML);
        return VM_EVENT_BTN1_PRESS;
    }
    if (button_flag[BTN_IDX_20ML]) {
        Button_ClearFlag(BTN_IDX_20ML);
        return VM_EVENT_BTN2_PRESS;
    }
    if (button_flag[BTN_IDX_50ML]) {
        Button_ClearFlag(BTN_IDX_50ML);
        return VM_EVENT_BTN3_PRESS;
    }
    if (button_flag[BTN_IDX_100ML]) {
        Button_ClearFlag(BTN_IDX_100ML);
        return VM_EVENT_BTN4_PRESS;
    }
    
    return VM_EVENT_NONE;
}

static void FSM_ProcessCurrentState(void) {
    StateConfig_t* config = GetStateConfig(current_state);
    
    switch (current_state) {
        case VM_STATE_IDLE:
        case VM_STATE_ERROR:
            break;
            
        case VM_STATE_1:
            // Hold mode - no extra processing
            break;
            
        case VM_STATE_2:
        case VM_STATE_3:
        case VM_STATE_4:
        case VM_STATE_5:
            if (config == NULL || config->mode != STATE_MODE_VOLUME) break;
            
            FlowControl_Update();
            
            if (FlowControl_IsComplete()) {
                FSM_ProcessEvent(VM_EVENT_COMPLETE);
                break;
            }
            
            if (FlowControl_GetState() == FLOW_STATE_ERROR) {
                FSM_ProcessEvent(VM_EVENT_ERROR);
                break;
            }
            
            // Print progress periodically
            if (FlowControl_IsDispensing()) {
                static uint32_t last_print = 0;
                if (millis() - last_print >= 500) {
                    last_print = millis();
                    Serial.printf("[%3d%%] %lu.%03lu ml\n",
                                 FlowControl_GetProgress(),
                                 FlowControl_GetDispensedVolume_ml(),
                                 FlowControl_GetDispensedVolume_ul() % 1000);
                }
            }
            break;
            
        default:
            break;
    }
}

/* ============================================================================
 *                          PUBLIC FUNCTIONS
 * ============================================================================ */

void VendingFSM_Init(void) {
    current_state = VM_STATE_IDLE;
    previous_state = VM_STATE_IDLE;
    state_enter_time = millis();
    total_dispensed_ml = 0;
    dispense_count = 0;
    is_initialized = true;
    
    Serial.println("\n[FSM] Initialization complete!");
    Serial.println("[FSM] Configuration:");
    Serial.printf("  - STATE_1: %s @ %d%%\n", fsm_config.state_1.name, fsm_config.state_1.speed_percent);
    Serial.printf("  - STATE_2: %s @ %d%%\n", fsm_config.state_2.name, fsm_config.state_2.speed_percent);
    Serial.printf("  - STATE_3: %s @ %d%%\n", fsm_config.state_3.name, fsm_config.state_3.speed_percent);
    Serial.printf("  - STATE_4: %s @ %d%%\n", fsm_config.state_4.name, fsm_config.state_4.speed_percent);
    Serial.printf("  - STATE_5: %s @ %d%%\n", fsm_config.state_5.name, fsm_config.state_5.speed_percent);
    Serial.println();
}

void VendingFSM_Update(void) {
    if (!is_initialized) return;
    
    VendingMachine_Event_t event = FSM_ReadButtonEvent();
    if (event != VM_EVENT_NONE) {
        FSM_ProcessEvent(event);
    }
    
    FSM_ProcessCurrentState();
    
    // Update RPM periodically
    static uint32_t last_rpm = 0;
    if (millis() - last_rpm >= 1000) {
        last_rpm = millis();
        Pump_CalculateRPM_Periodic();
    }
}

bool VendingFSM_ConfigState(VendingMachine_State_t state, const StateConfig_t* config) {
    if (config == NULL) return false;
    StateConfig_t* target = GetStateConfig(state);
    if (target == NULL) return false;
    
    target->mode = config->mode;
    target->volume_ml = config->volume_ml;
    target->speed_percent = config->speed_percent;
    target->name = config->name;
    return true;
}

void VendingFSM_SetSpeed(VendingMachine_State_t state, uint8_t speed_percent) {
    if (speed_percent == 0 || speed_percent > 100) return;
    StateConfig_t* config = GetStateConfig(state);
    if (config != NULL) {
        config->speed_percent = speed_percent;
        Serial.printf("[FSM] %s speed: %d%%\n", config->name, speed_percent);
    }
}

void VendingFSM_SetVolume(VendingMachine_State_t state, uint32_t volume_ml) {
    StateConfig_t* config = GetStateConfig(state);
    if (config != NULL && config->mode == STATE_MODE_VOLUME) {
        config->volume_ml = volume_ml;
        Serial.printf("[FSM] %s volume: %lu ml\n", config->name, volume_ml);
    }
}

void VendingFSM_EmergencyStop(void) {
    Serial.println("[FSM] EMERGENCY STOP!");
    Pump_Stop();
    FlowControl_Stop();
    current_state = VM_STATE_IDLE;
}

VendingMachine_State_t VendingFSM_GetState(void) {
    return current_state;
}

const char* VendingFSM_GetStateName(void) {
    return STATE_NAMES[current_state];
}
```

### 7.10. `src/main.cpp`

```cpp
/**
 * @file    main.cpp
 * @brief   Main application for ESP32 Vending Machine
 * @author  Migrated from STM32F103C8
 * @date    2026
 */

#include <Arduino.h>
#include "config.h"
#include "pump.h"
#include "button.h"
#include "flow_control.h"
#include "vending_fsm.h"

/* ============================================================================
 *                          SETUP
 * ============================================================================ */

void setup() {
    // Initialize Serial for debugging
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    
    delay(1000);  // Wait for serial monitor
    
    Serial.println("\n========================================");
    Serial.println("   VENDING MACHINE - ESP32 VERSION     ");
    Serial.println("========================================");
    Serial.println();
    
    // Initialize modules
    Serial.println("[MAIN] Initializing modules...");
    
    // 1. Initialize Pump Driver
    if (!Pump_Init()) {
        Serial.println("[MAIN] ERROR: Pump initialization failed!");
        while (1) {
            digitalWrite(PIN_LED_BUILTIN, !digitalRead(PIN_LED_BUILTIN));
            delay(100);
        }
    }
    Serial.println("[MAIN] Pump driver: OK");
    
    // 2. Initialize Flow Control
    FlowControl_Init();
    Serial.println("[MAIN] Flow control: OK");
    
    // 3. Initialize Button Handler
    Button_Init();
    Serial.println("[MAIN] Button handler: OK");
    
    // 4. Initialize FSM
    VendingFSM_Init();
    Serial.println("[MAIN] Vending FSM: OK");
    
    // Setup built-in LED for status
    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);
    
    Serial.println("\n[MAIN] System ready!");
    Serial.println("[MAIN] Press a button to start...\n");
}

/* ============================================================================
 *                          MAIN LOOP
 * ============================================================================ */

void loop() {
    // Update FSM - handles all vending logic
    VendingFSM_Update();
    
    // Blink LED to show system is running
    static uint32_t last_blink = 0;
    if (millis() - last_blink >= 1000) {
        last_blink = millis();
        digitalWrite(PIN_LED_BUILTIN, !digitalRead(PIN_LED_BUILTIN));
    }
    
    // Small delay to reduce CPU load
    delay(10);
}

/* ============================================================================
 *                     OPTIONAL: FreeRTOS TASKS
 * ============================================================================ */

/*
 * Nếu muốn dùng FreeRTOS để tách biệt các task:
 * 
 * Task 1 (Core 0): Button scanning
 * Task 2 (Core 1): Flow control & FSM
 * 
 * Uncomment code below và comment hàm loop() ở trên
 */

/*
void TaskButton(void* parameter) {
    for (;;) {
        Button_Update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void TaskFSM(void* parameter) {
    for (;;) {
        VendingFSM_Update();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void setup() {
    // ... initialization code ...
    
    // Create tasks
    xTaskCreatePinnedToCore(TaskButton, "Button", 2048, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskFSM, "FSM", 4096, NULL, 2, NULL, 1);
    
    // Delete Arduino loop task
    vTaskDelete(NULL);
}

void loop() {
    // Not used when running FreeRTOS tasks
}
*/
```

---

## 8. Hướng Dẫn Build và Flash

### 8.1. Kết Nối Phần Cứng

```
ESP32 DevKit     →     Bơm Leirong 24V
─────────────────────────────────────────
GPIO25 (PWM)     →     Pin 3 (Lục) qua MOSFET/Driver
GPIO26 (DIR)     →     Pin 4 (Bạch)
GPIO27 (FG)      ←     Pin 5 (Vàng)
GND              →     Pin 2 (Đen) + GND chung
                       Pin 1 (Đỏ) ← +24V nguồn ngoài

ESP32 DevKit     →     Nút nhấn (Pull-up)
─────────────────────────────────────────
GPIO32           ←     BTN_0 (Hold)    + 10kΩ to 3.3V
GPIO33           ←     BTN_1 (Volume)  + 10kΩ to 3.3V
GPIO34           ←     BTN_2 (Volume)  + 10kΩ ngoài (bắt buộc)
GPIO35           ←     BTN_3 (Volume)  + 10kΩ ngoài (bắt buộc)
GPIO39/VN        ←     BTN_4 (Volume)  + 10kΩ ngoài (bắt buộc)
```

### 8.2. Sơ Đồ Mạch MOSFET Driver

```
                    +24V
                     │
                     │
              ┌──────┴──────┐
              │             │
              │  MOTOR      │
              │  (Pump)     │
              │             │
              └──────┬──────┘
                     │
                     D
                  ┌──┴──┐
      GPIO25 ────┤ G    │ IRF540N (MOSFET)
                  └──┬──┘
                     S
                     │
                    GND

Hoặc dùng Module L298N / TB6612:
┌─────────────────────────┐
│      L298N Module       │
│                         │
│  IN1  ← GPIO25 (PWM)    │
│  IN2  ← GPIO26 (DIR)    │
│  ENA  ← 3.3V (luôn bật) │
│                         │
│  OUT1 → Motor +         │
│  OUT2 → Motor -         │
│                         │
│  +12V/24V ← Nguồn motor │
│  GND     ← GND chung    │
│  +5V     → Có thể cấp   │
│            cho ESP32    │
└─────────────────────────┘
```

### 8.3. Build Project

**Trong VSCode with PlatformIO:**

1. Mở Command Palette (`Ctrl+Shift+P`)
2. Gõ "PlatformIO: Build" và Enter
3. Hoặc click icon ✓ (checkmark) trên thanh dưới

**Command Line:**

```powershell
cd D:\ABCsolution\vending_machine_esp32
pio run
```

### 8.4. Upload Firmware

**Trong VSCode:**

1. Kết nối ESP32 qua USB
2. Command Palette → "PlatformIO: Upload"
3. Hoặc click icon → (arrow) trên thanh dưới

**Command Line:**

```powershell
pio run --target upload
```

### 8.5. Monitor Serial

**Trong VSCode:**

1. Command Palette → "PlatformIO: Serial Monitor"
2. Hoặc click icon 🔌 (plug) trên thanh dưới

**Command Line:**

```powershell
pio device monitor --baud 115200
```

**Output mẫu:**

```
========================================
   VENDING MACHINE - ESP32 VERSION     
========================================

[MAIN] Initializing modules...
[PUMP] Initialized successfully
[MAIN] Pump driver: OK
[FLOW] Initialized
[MAIN] Flow control: OK
[BUTTON] Initialized
[MAIN] Button handler: OK

[FSM] Initialization complete!
[FSM] Configuration:
  - STATE_1: HOLD @ 70%
  - STATE_2: 100ml @ 80%
  - STATE_3: 20ml @ 65%
  - STATE_4: 50ml @ 70%
  - STATE_5: 100ml @ 20%

[MAIN] System ready!
[MAIN] Press a button to start...

[FSM] IDLE -> STATE_2 (after 5234 ms)
[FSM] Start dispensing 100ml @ 80%
[FLOW] Target: 100000 ul (1966 pulses)
[FLOW] Dispensing started at 80%
[ 10%] 10.234 ml
[ 25%] 25.123 ml
[ 50%] 50.456 ml
[ 75%] 75.789 ml
[100%] 100.123 ml
[FLOW] Complete! Dispensed: 100123 ul
[FSM] STATE_2 -> IDLE (after 12345 ms)
[FSM] Done! Dispensed: 100 ml (Total: 100 ml, 1 times)
[FSM] Ready! Waiting for button...
```

---

## 9. Debug và Xử Lý Lỗi

### 9.1. Lỗi Thường Gặp

| Lỗi                       | Nguyên nhân               | Giải pháp               |
| ------------------------- | ------------------------- | ----------------------- |
| **Upload failed**         | Wrong port / Driver       | Cài driver CP2102/CH340 |
| **No FG signal**          | Thiếu kết nối / sai pin   | Kiểm tra GPIO27         |
| **Motor không quay**      | Thiếu nguồn 24V / MOSFET  | Kiểm tra mạch driver    |
| **Button không phản hồi** | Thiếu pull-up (GPIO34-39) | Thêm điện trở 10kΩ      |
| **guru meditation error** | Stack overflow            | Tăng stack size         |

### 9.2. Kiểm Tra Từng Module

**Test Pump PWM:**

```cpp
void testPWM() {
    ledcAttach(PIN_PWM_MOTOR, PWM_FREQUENCY, PWM_RESOLUTION);
    
    for (int duty = 0; duty <= 100; duty += 10) {
        uint32_t pwm_value = (duty * PWM_MAX_DUTY) / 100;
        ledcWrite(PIN_PWM_MOTOR, pwm_value);
        Serial.printf("PWM: %d%% (%d)\n", duty, pwm_value);
        delay(1000);
    }
}
```

**Test Buttons:**

```cpp
void testButtons() {
    for (int i = 0; i < NO_OF_BUTTONS; i++) {
        int state = digitalRead(BUTTON_PINS[i]);
        Serial.printf("BTN_%d (GPIO%d): %s\n", 
                      i, BUTTON_PINS[i],
                      state == LOW ? "PRESSED" : "released");
    }
}
```

**Test FG Signal:**

```cpp
void testFG() {
    static uint32_t last_count = 0;
    uint32_t current = Pump_GetPulseCount();
    
    Serial.printf("Pulses: %lu (+%lu), RPM: %lu\n",
                  current,
                  current - last_count,
                  Pump_GetPumpHeadRPM());
    
    last_count = current;
}
```

### 9.3. Enable Debug Output

Trong `platformio.ini`:

```ini
build_flags = 
    -DCORE_DEBUG_LEVEL=4    ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
    -DPUMP_DEBUG=1
```

---

## 10. Mở Rộng Dự Án

### 10.1. Thêm WiFi Control

```cpp
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "VendingMachine";
const char* password = "12345678";

WebServer server(80);

void handleRoot() {
    String html = "<html><body>";
    html += "<h1>Vending Machine Control</h1>";
    html += "<p>State: " + String(VendingFSM_GetStateName()) + "</p>";
    html += "<a href='/dispense/50'>Dispense 50ml</a><br>";
    html += "<a href='/dispense/100'>Dispense 100ml</a><br>";
    html += "<a href='/stop'>Emergency Stop</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void handleDispense() {
    String volume = server.pathArg(0);
    // Trigger dispense...
    server.send(200, "text/plain", "Dispensing " + volume + "ml");
}

void handleStop() {
    VendingFSM_EmergencyStop();
    server.send(200, "text/plain", "Stopped!");
}

void setupWiFi() {
    WiFi.softAP(ssid, password);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    
    server.on("/", handleRoot);
    server.on("/dispense/{}", handleDispense);
    server.on("/stop", handleStop);
    server.begin();
}
```

### 10.2. Thêm Bluetooth Control

```cpp
#include "BluetoothSerial.h"

BluetoothSerial SerialBT;

void setupBluetooth() {
    SerialBT.begin("VendingMachine");
    Serial.println("Bluetooth started. Pair with 'VendingMachine'");
}

void handleBluetoothCommand() {
    if (SerialBT.available()) {
        String cmd = SerialBT.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "50") {
            VendingFSM_SetVolume(VM_STATE_2, 50);
            // Trigger STATE_2
        } else if (cmd == "100") {
            VendingFSM_SetVolume(VM_STATE_2, 100);
            // Trigger STATE_2
        } else if (cmd == "stop") {
            VendingFSM_EmergencyStop();
        }
        
        SerialBT.println("OK: " + cmd);
    }
}
```

### 10.3. Thêm OLED Display

```cpp
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setupDisplay() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        return;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
}

void updateDisplay() {
    display.clearDisplay();
    display.setCursor(0, 0);
    
    display.println("=== VENDING MACHINE ===");
    display.println();
    display.print("State: ");
    display.println(VendingFSM_GetStateName());
    
    if (FlowControl_IsDispensing()) {
        display.print("Progress: ");
        display.print(FlowControl_GetProgress());
        display.println("%");
        
        display.print("Volume: ");
        display.print(FlowControl_GetDispensedVolume_ml());
        display.println(" ml");
    }
    
    display.display();
}
```

### 10.4. Cấu Trúc Dự Án Mở Rộng

```
vending_machine_esp32/
├── platformio.ini
├── include/
│   ├── config.h
│   ├── pump.h
│   ├── button.h
│   ├── flow_control.h
│   ├── vending_fsm.h
│   ├── wifi_control.h      // NEW
│   ├── bluetooth_control.h // NEW
│   └── display.h           // NEW
├── src/
│   ├── main.cpp
│   ├── pump.cpp
│   ├── button.cpp
│   ├── flow_control.cpp
│   ├── vending_fsm.cpp
│   ├── wifi_control.cpp    // NEW
│   ├── bluetooth_control.cpp // NEW
│   └── display.cpp         // NEW
└── data/                   // Web files for SPIFFS
    └── index.html
```

---

## 📌 Tổng Kết

### Checklist Triển Khai

- [ ] Cài đặt PlatformIO extension trong VSCode
- [ ] Tạo project mới với board ESP32
- [ ] Copy code từ template trên
- [ ] Cấu hình `platformio.ini`
- [ ] Kết nối phần cứng theo sơ đồ
- [ ] Build và upload firmware
- [ ] Test từng module riêng lẻ
- [ ] Test toàn bộ hệ thống
- [ ] (Tùy chọn) Thêm WiFi/Bluetooth/Display

### Tài Nguyên Tham Khảo

- [ESP32 Arduino Core Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [ESP32 LEDC (PWM) Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)
- [ESP32 PCNT (Pulse Counter)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/pcnt.html)

---

**Tài liệu này được tạo tự động từ phân tích dự án STM32F103C8 Vending Machine.**
**Ngày tạo: March 4, 2026**
