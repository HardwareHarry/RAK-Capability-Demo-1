// =============================================================================
// config.h — WisBlock Sensor Hub Configuration
// Hardware pin definitions, constants, and compile-time settings
// =============================================================================
#pragma once

#include <Arduino.h>

// =============================================================================
// RAK3312 WisBlock Pin Definitions (RAK19011 Base Board)
// Reference: RAK3312 Quick Start Guide + RAK19011 Datasheet
// =============================================================================

// --- I2C Bus (shared by all I2C sensors on Slots A-F) -----------------------
#define PIN_I2C_SDA             SDA   // WisBlock I2C data
#define PIN_I2C_SCL             SCL   // WisBlock I2C clock

// --- SPI Bus (shared, directly from ESP32-S3) -------------------------------
#define PIN_SPI_MOSI            MOSI
#define PIN_SPI_MISO            MISO
#define PIN_SPI_CLK             SCK

// --- UART1 (Slot D — GNSS) -------------------------------------------------
#define PIN_GNSS_TX             WB_IO5  // ESP32 TX → GNSS RX
#define PIN_GNSS_RX             WB_IO4  // GNSS TX → ESP32 RX
#define GNSS_BAUD               9600

// --- WisBlock IO Control ----------------------------------------------------
#define PIN_3V3_S_EN            WB_IO2  // Controls 3V3_S switched sensor rail

// --- FRAM SPI Chip Select (Slot F) ------------------------------------------
// The CS pin depends on which slot the FRAM occupies.
// Slot F CS maps to WB_A1 on RAK19011.
#define PIN_FRAM_CS             WB_A1
#define FRAM_SPI_SPEED          16000000  // 16 MHz — FRAM supports up to 40 MHz

// --- Display Configuration --------------------------------------------------
// Both display types use IO Slot 1 (SPI-based)

#if defined(DISPLAY_EINK) && DISPLAY_EINK == 1
    // RAK14000 E-Ink Display (3.52", 360×240, tri-colour)
    #define PIN_DISP_CS         WB_IO3
    #define PIN_DISP_DC         WB_IO4  // Note: may conflict with GNSS on Slot D
    #define PIN_DISP_RST        WB_IO5
    #define PIN_DISP_BUSY       WB_IO6
    #define DISPLAY_TYPE_NAME   "E-Ink (RAK14000)"
    #define DISPLAY_WIDTH       360
    #define DISPLAY_HEIGHT      240

#elif defined(DISPLAY_TFT) && DISPLAY_TFT == 1
    // RAK14014 TFT LCD Display (2.4", 240×320, IPS, touch)
    #define PIN_DISP_CS         WB_IO3
    #define PIN_DISP_DC         WB_IO5
    #define PIN_DISP_RST        WB_IO6
    #define PIN_DISP_BL         WB_IO4  // Backlight control
    #define PIN_TOUCH_INT       WB_IO6  // FT6336 touch interrupt
    #define DISPLAY_TYPE_NAME   "TFT LCD (RAK14014)"
    #define DISPLAY_WIDTH       240
    #define DISPLAY_HEIGHT      320

#else
    #error "No display selected! Define DISPLAY_EINK=1 or DISPLAY_TFT=1 in build_flags"
#endif

// --- IO Expander (RAK13003 MCP23017, IO Slot 2) -----------------------------
#define IO_EXPANDER_I2C_ADDR    0x20    // A0=A1=A2=GND default
#define IO_EXP_LED_SENSOR       0       // GPA0 — sensor activity LED
#define IO_EXP_LED_LORA         1       // GPA1 — LoRa TX indicator
#define IO_EXP_LED_WIFI         2       // GPA2 — WiFi status LED
#define IO_EXP_LED_ERROR        3       // GPA3 — error indicator
#define IO_EXP_LED_GNSS         4       // GPA4 — GNSS fix indicator
#define IO_EXP_BTN_MODE         8       // GPB0 — mode button input

// =============================================================================
// I2C Device Addresses
// =============================================================================
#define BME680_I2C_ADDR         0x76    // RAK1906 default
#define VEML7700_I2C_ADDR       0x10    // Fixed address
#define PMSA003I_I2C_ADDR       0x12    // Fixed address
#define RV3028_I2C_ADDR         0x52    // Fixed address
#define FT6336_I2C_ADDR         0x38    // Touch controller (TFT only)

// =============================================================================
// Sensor Record — packed for FRAM storage efficiency
// =============================================================================
struct __attribute__((packed)) SensorRecord {
    uint32_t timestamp;         // Unix epoch from RTC
    int16_t  temperature;       // °C × 100    (2345 = 23.45°C)
    uint16_t humidity;          // %  × 10     (655  = 65.5%)
    uint16_t pressure;          // hPa × 10    (10132 = 1013.2 hPa)
    uint32_t gasResistance;     // Ohms raw
    uint16_t pm1_0;             // µg/m³
    uint16_t pm2_5;             // µg/m³
    uint16_t pm10;              // µg/m³
    uint16_t lux;               // lux × 10    (5432 = 543.2 lux)
    int32_t  latitude;          // degrees × 1e7
    int32_t  longitude;         // degrees × 1e7
};
// Size: 30 bytes per record → ~34,900 records in 1MB FRAM → ~12 days at 30s

static_assert(sizeof(SensorRecord) == 30, "SensorRecord must be exactly 30 bytes");

// Sentinel values for unavailable readings
constexpr int16_t  SENSOR_UNAVAILABLE_I16   = INT16_MIN;
constexpr uint16_t SENSOR_UNAVAILABLE_U16   = UINT16_MAX;
constexpr uint32_t SENSOR_UNAVAILABLE_U32   = UINT32_MAX;
constexpr int32_t  SENSOR_UNAVAILABLE_I32   = INT32_MIN;

// =============================================================================
// FRAM Ring Buffer Configuration
// =============================================================================
constexpr uint32_t FRAM_SIZE_BYTES          = 1048576;  // 1MB (RAK15007)
constexpr uint32_t FRAM_HEADER_SIZE         = 64;       // Reserved for metadata
constexpr uint32_t FRAM_RECORD_SIZE         = sizeof(SensorRecord);
constexpr uint32_t FRAM_DATA_AREA           = FRAM_SIZE_BYTES - FRAM_HEADER_SIZE;
constexpr uint32_t FRAM_MAX_RECORDS         = FRAM_DATA_AREA / FRAM_RECORD_SIZE;

// Magic number to validate FRAM header integrity
constexpr uint32_t FRAM_MAGIC               = 0x57425348; // "WBSH"

// FRAM Header structure (stored at address 0x0000)
struct __attribute__((packed)) FramHeader {
    uint32_t magic;              // FRAM_MAGIC if initialised
    uint32_t writeIndex;         // Next write position (record index)
    uint32_t recordCount;        // Total records written (wraps at FRAM_MAX_RECORDS)
    uint32_t totalWrites;        // Lifetime write counter (never wraps)
    uint32_t lastLoRaSendIndex;  // Ring buffer index of last successfully sent record
    uint32_t lastLoRaSendTime;   // Timestamp of last successful LoRa TX
    uint8_t  reserved[FRAM_HEADER_SIZE - 24]; // Pad to FRAM_HEADER_SIZE
};

static_assert(sizeof(FramHeader) == FRAM_HEADER_SIZE, "FramHeader must match FRAM_HEADER_SIZE");

// =============================================================================
// Task Configuration
// =============================================================================

// Task stack sizes (bytes)
constexpr uint32_t STACK_SUPERVISOR         = 4096;
constexpr uint32_t STACK_SENSOR             = 8192;
constexpr uint32_t STACK_LORA               = 8192;
constexpr uint32_t STACK_WEBSERVER          = 8192;
constexpr uint32_t STACK_DISPLAY            = 4096;

// Task priorities (higher = more important)
constexpr UBaseType_t PRIORITY_SUPERVISOR   = 5;
constexpr UBaseType_t PRIORITY_SENSOR       = 3;
constexpr UBaseType_t PRIORITY_WEBSERVER    = 3;
constexpr UBaseType_t PRIORITY_LORA         = 2;
constexpr UBaseType_t PRIORITY_DISPLAY      = 1;

// Task core assignments
constexpr BaseType_t CORE_SUPERVISOR        = 0;
constexpr BaseType_t CORE_SENSOR            = 0;
constexpr BaseType_t CORE_LORA              = 0;
constexpr BaseType_t CORE_WEBSERVER         = 1;
constexpr BaseType_t CORE_DISPLAY           = 1;

// Watchdog timeouts (ms) — how long before Supervisor declares a task dead
constexpr uint32_t WDT_TIMEOUT_SENSOR       = 10000;    // 10s  (cycles every ~3s)
constexpr uint32_t WDT_TIMEOUT_LORA         = 900000;   // 15m  (long idle between TX)
constexpr uint32_t WDT_TIMEOUT_WEBSERVER    = 30000;    // 30s
constexpr uint32_t WDT_TIMEOUT_DISPLAY      = 600000;   // 10m  (slow refresh)

// Max consecutive missed heartbeats before recovery action
constexpr uint8_t  WDT_SOFT_RECOVERY_AFTER  = 3;
constexpr uint8_t  WDT_HARD_RESTART_AFTER   = 6;

// =============================================================================
// Sensor Warm-Up Configuration
// =============================================================================
constexpr uint32_t WARMUP_PMSA003I_MS       = 30000;    // 30s fan stabilisation
constexpr uint32_t WARMUP_BME680_GAS_MS     = 300000;   // 5m  initial heater stabilise
constexpr uint32_t WARMUP_GNSS_MAX_MS       = 180000;   // 3m  max wait for first fix

// =============================================================================
// LoRaWAN Configuration
// =============================================================================

// OTAA credentials — override these in a separate secrets.h or via NVS
#ifndef LORAWAN_DEVEUI
#define LORAWAN_DEVEUI  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_APPEUI
#define LORAWAN_APPEUI  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif
#ifndef LORAWAN_APPKEY
#define LORAWAN_APPKEY  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
#endif

constexpr uint8_t  LORAWAN_PORT             = 1;
constexpr uint8_t  LORAWAN_MAX_BACKFILL     = 10;  // Max missed records per TX

// LoRa payload structure (20 bytes — current reading)
struct __attribute__((packed)) LoRaPayload {
    int16_t  temperature;       // °C × 100
    uint16_t humidity;          // %  × 10
    uint16_t pressure;          // hPa × 10
    uint32_t gasResistance;     // Ohms
    uint16_t pm2_5;             // µg/m³
    uint16_t pm10;              // µg/m³
    uint16_t lux;               // lux × 10
    uint16_t batteryMv;         // millivolts
    uint16_t statusFlags;       // bitfield
};

static_assert(sizeof(LoRaPayload) == 20, "LoRaPayload must be exactly 20 bytes");

// Status flag bits for LoRa payload
constexpr uint16_t STATUS_GNSS_FIX          = (1 << 0);
constexpr uint16_t STATUS_PM_READY          = (1 << 1);
constexpr uint16_t STATUS_GAS_STABLE        = (1 << 2);
constexpr uint16_t STATUS_WIFI_CONNECTED    = (1 << 3);
constexpr uint16_t STATUS_FRAM_OK           = (1 << 4);
constexpr uint16_t STATUS_BACKFILL_PENDING  = (1 << 5);

// =============================================================================
// WiFi Configuration
// =============================================================================
#define WIFI_AP_SSID            "WisBlock-SensorHub"
#define WIFI_AP_PASSWORD        "wisblock123"
#define WIFI_AP_FALLBACK_MS     30000   // Fall back to AP mode after 30s STA failure

// =============================================================================
// Web Server Configuration
// =============================================================================
constexpr uint16_t WEB_SERVER_PORT          = 80;
constexpr uint32_t API_LIVE_CACHE_MS        = 1000;   // Min interval between live reads
constexpr uint16_t API_HISTORY_MAX_POINTS   = 300;    // Max datapoints returned in history

// =============================================================================
// Version Information
// =============================================================================
#define FW_NAME                 "WisBlock Sensor Hub"
#define FW_VERSION              "0.1.0"
#define FW_BUILD_DATE           __DATE__
#define FW_BUILD_TIME           __TIME__
