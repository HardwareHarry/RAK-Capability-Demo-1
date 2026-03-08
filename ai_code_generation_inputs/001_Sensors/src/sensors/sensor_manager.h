// =============================================================================
// sensors/sensor_manager.h — Orchestrates all sensor drivers
// Manages continuous reads, warm-up state, collection cycles, and FRAM writes
// =============================================================================
#pragma once

#include <Arduino.h>
#include "../config.h"
#include "../types.h"
#include "../storage/ring_buffer.h"
#include "rtc.h"
#include "light.h"
#include "environment.h"
#include "particulate.h"
#include "gnss.h"

class SensorManager {
public:
    SensorManager(FramRingBuffer& fram, LiveDataCache& liveData,
                  SystemHealth& health, EventGroupHandle_t events)
        : _fram(fram), _liveData(liveData), _health(health), _events(events) {}

    // --- Initialisation (called once at task start) -------------------------

    void init() {
        _health.heartbeat(TaskId::SENSOR, TaskState::INITIALISING,
                          "Starting sensors...");

        // Initialise each sensor — failures are non-fatal, we track per-sensor
        Serial.println(F("[SENSORS] Initialising all sensors..."));

        bool rtcOk   = _rtc.init();
        bool lightOk = _light.init();
        bool envOk   = _env.init();
        bool pmOk    = _pm.init();
        bool gnssOk  = _gnss.init();

        Serial.println(F("[SENSORS] Init summary:"));
        Serial.printf("  RTC:    %s\n", rtcOk   ? "OK" : "FAILED");
        Serial.printf("  Light:  %s\n", lightOk ? "OK" : "FAILED");
        Serial.printf("  BME680: %s\n", envOk   ? "OK" : "FAILED");
        Serial.printf("  PM:     %s\n", pmOk    ? "OK" : "FAILED");
        Serial.printf("  GNSS:   %s\n", gnssOk  ? "OK" : "FAILED");

        _initComplete = true;
        _rtcSynced = false;
    }

    // --- Main loop tick (called every ~250ms from SensorTask) ----------------

    void tick() {
        if (!_initComplete) return;

        uint32_t now = millis();

        // --- Continuous: GNSS NMEA parsing (every tick) ---
        if (_gnss.isAvailable()) {
            _gnss.update();

            // Sync RTC from GNSS on first valid time
            if (!_rtcSynced && _gnss.hasValidTime() && _rtc.isAvailable()) {
                bool synced = _rtc.setFromGnss(
                    _gnss.year(), _gnss.month(), _gnss.day(),
                    _gnss.hour(), _gnss.minute(), _gnss.second()
                );
                if (synced) {
                    _rtcSynced = true;
                }
            }
        }

        // --- Continuous: PM sensor reads (every tick, sensor updates at 1Hz) ---
        if (_pm.isAvailable()) {
            _pm.read();
        }

        // --- Periodic: BME680 forced measurement (every ~3 seconds) ---
        if (_env.isAvailable() && (now - _lastEnvReadMs >= 3000)) {
            _lastEnvReadMs = now;
            _env.measure();
        }

        // --- Update heartbeat with current warm-up status ---
        _updateHeartbeat();

        // --- Collection cycle (every SENSOR_INTERVAL_MS) ---
        if (now - _lastCollectionMs >= SENSOR_INTERVAL_MS) {
            _lastCollectionMs = now;
            _collect();
        }

        // --- Update live data cache (every tick for responsiveness) ---
        _updateLiveData();
    }

    // --- Accessor for individual sensor drivers (for status reporting) -------

    const RtcDriver&          rtc() const   { return _rtc; }
    const LightSensor&        light() const { return _light; }
    const EnvironmentSensor&  env() const   { return _env; }
    const ParticulateSensor&  pm() const    { return _pm; }
    const GnssSensor&         gnss() const  { return _gnss; }

private:
    // External references
    FramRingBuffer&     _fram;
    LiveDataCache&      _liveData;
    SystemHealth&       _health;
    EventGroupHandle_t  _events;

    // Sensor drivers
    RtcDriver           _rtc;
    LightSensor         _light;
    EnvironmentSensor   _env;
    ParticulateSensor   _pm;
    GnssSensor          _gnss;

    // State
    bool                _initComplete = false;
    bool                _rtcSynced = false;
    uint32_t            _lastEnvReadMs = 0;
    uint32_t            _lastCollectionMs = 0;
    uint32_t            _collectionCount = 0;

    // --- Collection: snapshot all sensors and write to FRAM ------------------

    void _collect() {
        SensorRecord record;
        memset(&record, 0, sizeof(record));

        // Timestamp
        record.timestamp = _rtc.getEpoch();

        // Environment (BME680)
        record.temperature    = _env.temperatureScaled();
        record.humidity       = _env.humidityScaled();
        record.pressure       = _env.pressureScaled();
        record.gasResistance  = _env.gasResistanceRaw();

        // Light (VEML7700) — read fresh for the collection
        record.lux = _light.readLux();

        // Particulate matter (PMSA003I)
        record.pm1_0 = _pm.pm1_0();
        record.pm2_5 = _pm.pm2_5();
        record.pm10  = _pm.pm10();

        // GNSS position
        record.latitude  = _gnss.latitudeScaled();
        record.longitude = _gnss.longitudeScaled();

        // Write to FRAM
        if (_fram.isInitialised()) {
            if (_fram.writeRecord(record)) {
                _collectionCount++;

                // Signal other tasks that new data is available
                xEventGroupSetBits(_events, EVT_NEW_RECORD);

                if (_collectionCount % 10 == 0) {
                    Serial.printf("[SENSORS] Collection #%lu written to FRAM "
                                  "(%.1f%% full)\n",
                                  _collectionCount, _fram.usagePercent());
                }
            } else {
                Serial.println(F("[SENSORS] WARNING: FRAM write failed"));
            }
        }
    }

    // --- Update the live data cache for web server and display ---------------

    void _updateLiveData() {
        LiveData data;

        data.timestamp       = _rtc.getEpoch();
        data.temperature     = _env.temperatureScaled();
        data.humidity        = _env.humidityScaled();
        data.pressure        = _env.pressureScaled();
        data.gasResistance   = _env.gasResistanceRaw();
        data.pm1_0           = _pm.pm1_0();
        data.pm2_5           = _pm.pm2_5();
        data.pm10            = _pm.pm10();
        data.lux             = _light.readLux();

        // GNSS extended data
        data.gnss.latitude   = _gnss.latitudeScaled();
        data.gnss.longitude  = _gnss.longitudeScaled();
        data.gnss.altitude   = static_cast<int32_t>(_gnss.altitude() * 1000);
        data.gnss.satellites = static_cast<uint8_t>(_gnss.satellites());
        data.gnss.hdop       = static_cast<uint16_t>(_gnss.hdop() * 100);
        data.gnss.fixValid   = _gnss.hasValidFix();
        data.gnss.fixAgeMs   = _gnss.fixAgeMs();

        if (_gnss.hasValidTime()) {
            data.gnss.year   = _gnss.year();
            data.gnss.month  = _gnss.month();
            data.gnss.day    = _gnss.day();
            data.gnss.hour   = _gnss.hour();
            data.gnss.minute = _gnss.minute();
            data.gnss.second = _gnss.second();
        }

        // Sensor readiness
        data.pmReady         = _pm.isReady();
        data.gasStable       = _env.isGasStable();
        data.gnssHasFix      = _gnss.hasValidFix();

        // System info
        data.batteryMv       = _readBatteryMv();
        data.framUsedRecords = _fram.recordCount();
        data.framTotalCapacity = _fram.capacity();

        // WiFi and LoRa status are set by their respective tasks,
        // so we preserve existing values
        LiveData existing = _liveData.snapshot();
        data.wifiRssi       = existing.wifiRssi;
        data.wifiConnected  = existing.wifiConnected;
        data.loraJoined     = existing.loraJoined;
        data.loraLastTxTime = existing.loraLastTxTime;
        data.loraFrameCounter = existing.loraFrameCounter;

        _liveData.update(data);
    }

    // --- Heartbeat with warm-up status reporting ---

    void _updateHeartbeat() {
        char msg[48];

        bool allReady = true;

        if (!_pm.isWarmedUp()) {
            allReady = false;
            snprintf(msg, sizeof(msg), "PM warming (%lus left)",
                     _pm.warmupRemainingSec());
        } else if (!_gnss.hasEverHadFix()) {
            allReady = false;
            char gnssStatus[32];
            _gnss.getStatusString(gnssStatus, sizeof(gnssStatus));
            snprintf(msg, sizeof(msg), "GNSS: %s", gnssStatus);
        } else if (!_env.isGasStable()) {
            // Not blocking — just informational
            snprintf(msg, sizeof(msg), "Running (gas: %lus/%lus)",
                     _env.gasWarmupElapsedSec(), WARMUP_BME680_GAS_MS / 1000);
        } else {
            snprintf(msg, sizeof(msg), "All sensors nominal");
        }

        TaskState state = allReady ? TaskState::RUNNING : TaskState::WARMING_UP;
        _health.heartbeat(TaskId::SENSOR, state, msg);
    }

    // --- Battery voltage via ADC ---

    uint16_t _readBatteryMv() {
        // On WisBlock, battery voltage is available via ADC on WB_A0
        // with a voltage divider. The RAK19012 power board routes VBAT
        // through a divider to ADC_VBAT.
        // For RAK3312: analogRead returns 0-4095 for 0-3.3V
        // Voltage divider ratio is typically 2:1, so actual VBAT = ADC * 2
        int raw = analogRead(WB_A0);
        float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
        return static_cast<uint16_t>(voltage * 1000.0f);
    }
};
