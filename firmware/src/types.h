// =============================================================================
// types.h — Shared types for inter-task communication
// =============================================================================
#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/event_groups.h>
#include "config.h"

// =============================================================================
// Task Identity & State
// =============================================================================

enum class TaskId : uint8_t {
    SENSOR = 0,
    LORA,
    WEBSERVER,
    DISPLAY_TASK,   // was DISPLAY — conflicts with Arduino.h macro
    TASK_COUNT  // must be last
};

enum class TaskState : uint8_t {
    NOT_STARTED,
    INITIALISING,
    WARMING_UP,
    RUNNING,
    DEGRADED,       // running but with reduced capability
    ERROR,
    RECOVERING
};

enum class SystemState : uint8_t {
    BOOTING,
    WARMING_UP,
    READY,
    DEGRADED,       // some non-essential subsystems failed
    ERROR
};

// Human-readable names for logging and display
inline const char* taskStateName(TaskState s) {
    switch (s) {
        case TaskState::NOT_STARTED:   return "Not Started";
        case TaskState::INITIALISING:  return "Initialising";
        case TaskState::WARMING_UP:    return "Warming Up";
        case TaskState::RUNNING:       return "Running";
        case TaskState::DEGRADED:      return "Degraded";
        case TaskState::ERROR:         return "Error";
        case TaskState::RECOVERING:    return "Recovering";
        default:                       return "Unknown";
    }
}

inline const char* taskIdName(TaskId id) {
    switch (id) {
        case TaskId::SENSOR:        return "Sensor";
        case TaskId::LORA:          return "LoRa";
        case TaskId::WEBSERVER:     return "WebServer";
        case TaskId::DISPLAY_TASK:  return "Display";
        default:                    return "Unknown";
    }
}

// =============================================================================
// Per-Task Health Record
// =============================================================================

struct TaskHealth {
    volatile uint32_t   lastHeartbeat;      // millis() of last check-in
    volatile TaskState  state;
    uint32_t            timeoutMs;          // max gap before declared dead
    volatile uint8_t    consecutiveMisses;  // missed heartbeat counter
    char                statusMsg[48];      // human-readable for display
};

// =============================================================================
// System Health — owned by Supervisor, read by all
// =============================================================================

struct SystemHealth {
    TaskHealth          tasks[static_cast<size_t>(TaskId::TASK_COUNT)];
    volatile SystemState systemState;
    SemaphoreHandle_t   mutex;
    uint32_t            bootTime;           // millis() at boot
    uint32_t            lastRestartReason;  // stored in RTC memory across reboots

    // ESP32-S3 Internal Temperature Monitoring (system health indicator)
    int16_t             chipTemperatureC;   // degrees C × 100 (e.g., 4200 = 42.00°C)
    uint32_t            lastTempReadMs;     // millis() of last temperature read
    uint8_t             thermalThrottleState;  // 0=normal, 1=warm, 2=critical

    void init() {
        mutex = xSemaphoreCreateMutex();
        systemState = SystemState::BOOTING;
        bootTime = millis();
        lastRestartReason = 0;
        chipTemperatureC = 0;
        lastTempReadMs = 0;
        thermalThrottleState = 0;

        // Configure per-task timeouts
        tasks[static_cast<size_t>(TaskId::SENSOR)].timeoutMs         = WDT_TIMEOUT_SENSOR;
        tasks[static_cast<size_t>(TaskId::LORA)].timeoutMs           = WDT_TIMEOUT_LORA;
        tasks[static_cast<size_t>(TaskId::WEBSERVER)].timeoutMs      = WDT_TIMEOUT_WEBSERVER;
        tasks[static_cast<size_t>(TaskId::DISPLAY_TASK)].timeoutMs   = WDT_TIMEOUT_DISPLAY;

        for (auto& t : tasks) {
            t.lastHeartbeat = millis();
            t.state = TaskState::NOT_STARTED;
            t.consecutiveMisses = 0;
            t.statusMsg[0] = '\0';
        }
    }

    // Called by each task to report it's alive
    void heartbeat(TaskId id, TaskState state, const char* msg = nullptr) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            auto& t = tasks[static_cast<size_t>(id)];
            t.lastHeartbeat = millis();
            t.state = state;
            t.consecutiveMisses = 0;
            if (msg) {
                strncpy(t.statusMsg, msg, sizeof(t.statusMsg) - 1);
                t.statusMsg[sizeof(t.statusMsg) - 1] = '\0';
            }
            xSemaphoreGive(mutex);
        }
    }

    // Read a snapshot of one task's health (thread-safe)
    TaskHealth getTaskHealth(TaskId id) {
        TaskHealth copy;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = tasks[static_cast<size_t>(id)];
            xSemaphoreGive(mutex);
        }
        return copy;
    }

    uint32_t uptimeSeconds() const {
        return (millis() - bootTime) / 1000;
    }

    // Read ESP32-S3 internal temperature via ADC and update thermal state
    // Call periodically (e.g., every 5 seconds) from supervisor task
    void updateChipTemperature() {
        // ESP32-S3 internal temperature sensor (TSENS)
        // Temperature ADC is on channel ADC1_CHANNEL_0 (GPIO 1)
        // Output is approximately 0.5mV per degree C, offset to room temp
        // Formula from IDF: T_celsius = (uint32_t)(adc_reading - 32) / 1.81 + 20.26

        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            int raw = analogRead(ADC_TEMPERATURE_PIN);  // ADC1_CHANNEL_0 mapped

            // Simple approximation (fine for monitoring purposes)
            // ESP32-S3: ~1475 counts at 25°C, changes ~11 counts per °C
            int temp_c = (int16_t)((raw - 1475) / 11 + 25) * 100;

            chipTemperatureC = temp_c;
            lastTempReadMs = millis();

            // Determine thermal throttle state
            // WARN: 75°C, CRITICAL: 85°C (ESP32 max rated is 125°C)
            int16_t temp = temp_c / 100;  // Convert to integer °C
            uint8_t oldState = thermalThrottleState;

            if (temp >= 85) {
                thermalThrottleState = 2;  // CRITICAL
            } else if (temp >= 75) {
                thermalThrottleState = 1;  // WARM
            } else if (temp < 70) {
                thermalThrottleState = 0;  // NORMAL
            }
            // Hysteresis: stay in WARM until below 70°C, CRITICAL until below 75°C

            if (thermalThrottleState != oldState && thermalThrottleState > 0) {
                if (thermalThrottleState == 1) {
                    systemState = SystemState::DEGRADED;  // Throttle IO operations
                } else if (thermalThrottleState == 2) {
                    systemState = SystemState::ERROR;      // Critical condition
                }
            } else if (thermalThrottleState == 0 && systemState == SystemState::DEGRADED) {
                systemState = SystemState::READY;          // Resume normal ops
            }

            xSemaphoreGive(mutex);
        }
    }

    // Get current chip temperature (thread-safe)
    int16_t getChipTemperatureC() {
        int16_t temp = 0;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            temp = chipTemperatureC;
            xSemaphoreGive(mutex);
        }
        return temp / 100;  // Return as integer °C
    }

    // Get thermal throttle state: 0=normal, 1=warm/throttled, 2=critical
    uint8_t getThermalState() {
        uint8_t state = 0;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            state = thermalThrottleState;
            xSemaphoreGive(mutex);
        }
        return state;
    }
};

// =============================================================================
// Live Data Cache — latest sensor readings for web API and display
// =============================================================================

struct GnssData {
    int32_t  latitude;          // degrees × 1e7
    int32_t  longitude;         // degrees × 1e7
    int32_t  altitude;          // mm above MSL
    uint8_t  satellites;
    uint16_t hdop;              // × 100
    bool     fixValid;
    uint32_t fixAgeMs;          // millis() since last valid fix
    uint8_t  hour, minute, second;
    uint16_t year;
    uint8_t  month, day;
};

struct LiveData {
    // Sensor readings (same as SensorRecord but with extras)
    uint32_t timestamp;
    int16_t  temperature;
    uint16_t humidity;
    uint16_t pressure;
    uint32_t gasResistance;
    uint16_t pm1_0;
    uint16_t pm2_5;
    uint16_t pm10;
    uint16_t lux;

    // GNSS (extended data not stored in FRAM)
    GnssData gnss;

    // Sensor readiness flags
    bool     pmReady;           // true after WARMUP_PMSA003I_MS
    bool     gasStable;         // true after WARMUP_BME680_GAS_MS
    bool     gnssHasFix;

    // System info
    uint16_t batteryMv;
    int8_t   wifiRssi;
    bool     wifiConnected;
    bool     loraJoined;
    uint32_t loraLastTxTime;
    uint32_t loraFrameCounter;
    uint32_t framUsedRecords;
    uint32_t framTotalCapacity;

    // Snapshot timestamp
    uint32_t lastUpdateMs;      // millis() when this was last written
};

struct LiveDataCache {
    LiveData            data;
    SemaphoreHandle_t   mutex;

    void init() {
        mutex = xSemaphoreCreateMutex();
        memset(&data, 0, sizeof(data));
        data.temperature    = SENSOR_UNAVAILABLE_I16;
        data.humidity       = SENSOR_UNAVAILABLE_U16;
        data.pressure       = SENSOR_UNAVAILABLE_U16;
        data.gasResistance  = SENSOR_UNAVAILABLE_U32;
        data.pm1_0          = SENSOR_UNAVAILABLE_U16;
        data.pm2_5          = SENSOR_UNAVAILABLE_U16;
        data.pm10           = SENSOR_UNAVAILABLE_U16;
        data.lux            = SENSOR_UNAVAILABLE_U16;
        data.gnss.latitude  = SENSOR_UNAVAILABLE_I32;
        data.gnss.longitude = SENSOR_UNAVAILABLE_I32;
        data.framTotalCapacity = FRAM_MAX_RECORDS;
    }

    // Writer: update all sensor data atomically
    void update(const LiveData& newData) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            data = newData;
            data.lastUpdateMs = millis();
            xSemaphoreGive(mutex);
        }
    }

    // Reader: get a consistent snapshot
    LiveData snapshot() {
        LiveData copy;
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            copy = data;
            xSemaphoreGive(mutex);
        }
        return copy;
    }
};

// =============================================================================
// Event Group Bits — inter-task signalling
// =============================================================================
constexpr EventBits_t EVT_NEW_RECORD        = (1 << 0); // New record written to FRAM
constexpr EventBits_t EVT_DISPLAY_REFRESH   = (1 << 1); // Display should update
constexpr EventBits_t EVT_STATE_CHANGE      = (1 << 2); // System/task state changed
constexpr EventBits_t EVT_LORA_TX_DONE      = (1 << 3); // LoRa transmission completed
constexpr EventBits_t EVT_CONFIG_CHANGED    = (1 << 4); // Configuration was updated

// =============================================================================
// Display Abstraction — interface for both E-Ink and TFT
// =============================================================================

class DisplayDriver {
public:
    virtual ~DisplayDriver() = default;

    virtual bool init() = 0;

    // Boot status screen — called repeatedly during warm-up
    virtual void showBootScreen(const SystemHealth& health) = 0;

    // Live dashboard — called periodically once system is ready
    virtual void showDashboard(const LiveData& data, const SystemHealth& health) = 0;

    // Error screen
    virtual void showError(const char* message) = 0;

    // Display characteristics (used by supervisor to set refresh timing)
    virtual bool isPersistent() const = 0;      // true for e-ink (retains image)
    virtual uint32_t minRefreshMs() const = 0;  // minimum time between refreshes
};
