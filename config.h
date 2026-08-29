#pragma once

#include <Arduino.h>

// =============================================================
// Debug logging (set an item to 0 to silence that module)
// =============================================================
#define DEBUG_GLOBAL      1
#define DEBUG_MOTOR       1
#define DEBUG_VIBRATION   1
#define DEBUG_AUDIO       1
#define DEBUG_MATRIX      1
#define DEBUG_BUTTON      1
#define DEBUG_API         1
#define DEBUG_WIFI        1

// =============================================================
// Wi-Fi, NTP and web debug UI
// Time synchronization starts asynchronously; setup() never waits for it.
// =============================================================
constexpr char WIFI_SSID[] = "kbhot";
constexpr char WIFI_PASS[] = "00000001";
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;

constexpr int32_t NTP_GMT_OFFSET_SEC = 9 * 60 * 60;  // Korea Standard Time
constexpr int32_t NTP_DAYLIGHT_OFFSET_SEC = 0;
constexpr char NTP_SERVER_1[] = "pool.ntp.org";
constexpr char NTP_SERVER_2[] = "time.google.com";
constexpr time_t NTP_MIN_VALID_EPOCH = 1609459200;  // 2021-01-01 UTC

constexpr uint16_t WEB_SERVER_PORT = 80;
constexpr char WEB_SOCKET_PATH[] = "/ws";
constexpr uint32_t STATUS_PUSH_INTERVAL = 1000;
constexpr uint8_t WIFI_COMMAND_QUEUE_LENGTH = 8;
constexpr size_t WIFI_STATUS_JSON_BUFFER_SIZE = 512;
constexpr size_t WIFI_ACK_JSON_BUFFER_SIZE = 128;

// =============================================================
// Seoul subway timetable API
// =============================================================
constexpr char SUBWAY_API_BASE_URL[] = "http://openapi.seoul.go.kr:8088/";
constexpr char SUBWAY_API_KEY[] = "67726f57766461683530747742654f";
constexpr char SUBWAY_API_SERVICE[] = "SearchSTNTimeTableByFRCodeService";
constexpr uint8_t TRAIN_DIRECTION = 1;  // 1=up/inner, 2=down/outer
constexpr int32_t STATION_WINDOW = 90;  // seconds before/after arrival
constexpr uint16_t MAX_TIMES = 320;

constexpr uint32_t API_STATION_CHECK_INTERVAL_MS = 1000;
constexpr uint32_t API_REFRESH_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
constexpr uint32_t API_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t API_INTER_REQUEST_DELAY_MS = 300;
constexpr uint16_t API_HTTP_TIMEOUT_MS = 10000;
constexpr size_t API_JSON_DOCUMENT_CAPACITY = 20480;
constexpr uint32_t API_TASK_STACK_BYTES = 16384;
constexpr uint8_t API_TASK_PRIORITY = 1;

// =============================================================
// GPIO map - ESP32 DevKitC / ESP32-WROOM-32D
// =============================================================
constexpr uint8_t PIN_MATRIX_DIN = 23;
constexpr uint8_t PIN_MATRIX_CLK = 18;
constexpr uint8_t PIN_MATRIX_CS  = 27;

constexpr uint8_t PIN_DFPLAYER_RX = 16;  // ESP32 RX2 <- DFPlayer TX
constexpr uint8_t PIN_DFPLAYER_TX = 17;  // ESP32 TX2 -> DFPlayer RX (1 kohm series)

constexpr uint8_t PIN_STEPPER_IN1 = 25;
constexpr uint8_t PIN_STEPPER_IN2 = 26;
constexpr uint8_t PIN_STEPPER_IN3 = 32;
constexpr uint8_t PIN_STEPPER_IN4 = 33;

constexpr uint8_t PIN_VIBRATION = 14;    // Drives an NPN base resistor, not the motor
constexpr uint8_t PIN_BUTTON = 21;       // Button to GND; internal pull-up is enabled
constexpr uint8_t PIN_BUTTON_LED = 22;   // GPIO -> 220~330 ohm -> LED -> GND

// =============================================================
// Serial command input
// =============================================================
constexpr uint32_t SERIAL_BAUD_RATE = 115200;
constexpr uint32_t SERIAL_READY_WAIT_MS = 1500;
constexpr uint32_t SERIAL_COMMAND_IDLE_MS = 50;
constexpr size_t SERIAL_COMMAND_BUFFER_SIZE = 32;

// =============================================================
// Button and confirmation LED
// =============================================================
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LED_ON_MS = 1000;

// =============================================================
// Vibration motor
// =============================================================
constexpr uint32_t VIBRATION_DURATION_MS = 1000;
constexpr uint8_t VIBRATION_ACTIVE_LEVEL = HIGH;

// =============================================================
// MAX7219 8x32 matrix
// MATRIX_HARDWARE_TYPE is consumed after MD_MAX72XX.h is included.
// Change FC16_HW if the physical module uses a different wiring layout.
// =============================================================
#define MATRIX_HARDWARE_TYPE MD_MAX72XX::FC16_HW
constexpr uint8_t MATRIX_DEVICE_COUNT = 4;
constexpr uint8_t MATRIX_WIDTH = MATRIX_DEVICE_COUNT * 8;
constexpr uint8_t MATRIX_HEIGHT = 8;
constexpr uint8_t MATRIX_INTENSITY = 2;  // 0..15
constexpr uint32_t MATRIX_SCROLL_INTERVAL_MS = 60;
constexpr uint32_t MATRIX_STATIC_DISPLAY_MS = 5000;
constexpr uint8_t MATRIX_SCROLL_REPEAT = 2;

// Number of blank pixel columns inserted only between adjacent 8x8 glyphs.
// Despite the legacy CELLS name, 1 means one column, 2 means two columns, etc.
// 0 preserves the original tightly packed rendering.
constexpr uint8_t MATRIX_GLYPH_SPACING_CELLS = 1;

// MD_MAX72XX numbers this FC-16 chain from the rightmost physical column.
// The renderer uses normal left-to-right screen coordinates, so translate
// logical X to the library's descending column order.
constexpr bool MATRIX_REVERSE_COLUMNS = true;
constexpr bool MATRIX_FLIP_VERTICAL = false;

// Rotate each 8x8 Hangul glyph while keeping station character order and the
// 32-column display layout unchanged.
constexpr bool MATRIX_ROTATE_GLYPHS_CCW = false;

// =============================================================
// DFPlayer Mini
// =============================================================
constexpr uint32_t DFPLAYER_BAUD_RATE = 9600;
constexpr uint8_t DFPLAYER_VOLUME = 24;  // 0..30

// =============================================================
// 28BYJ-48 + ULN2003
// MOTOR_STEPS_PER_REV is deliberately configurable because geared motors vary.
// =============================================================
constexpr int32_t MOTOR_STEPS_PER_REV = 4096;
constexpr float MOTOR_MAX_SPEED = 700.0F;
constexpr float MOTOR_ACCELERATION = 350.0F;
constexpr uint8_t STATION_POSITION_COUNT = 5;
