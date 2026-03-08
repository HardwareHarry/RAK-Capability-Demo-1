// =============================================================================
// sensors/environment.h — BME680 Environment Sensor Driver (RAK1906)
// Temperature, humidity, pressure, and VOC gas resistance
// Manages forced-mode measurement cycles and gas heater warm-up state
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Adafruit_BME680.h>
#include "../config.h"

class EnvironmentSensor {
public:
    bool init() {
        if (!_bme.begin(BME680_I2C_ADDR)) {
            Serial.println(F("[ENV] ERROR: BME680 not found at 0x76"));
            _available = false;
            return false;
        }

        // Configure oversampling and filter
        _bme.setTemperatureOversampling(BME680_OS_8X);
        _bme.setHumidityOversampling(BME680_OS_2X);
        _bme.setPressureOversampling(BME680_OS_4X);
        _bme.setIIRFilterSize(BME680_FILTER_SIZE_3);

        // Configure gas heater: 320C for 150ms is a good general-purpose setting
        _bme.setGasHeater(320, 150);  // temperature (C), duration (ms)

        _available = true;
        _initTimeMs = millis();

        Serial.println(F("[ENV] BME680 initialised"));
        Serial.println(F("[ENV] Gas sensor will stabilise over ~5 minutes"));

        return true;
    }

    // Trigger a forced measurement and read all values
    // Call this on your ~3 second cycle
    // Returns true if measurement completed successfully
    bool measure() {
        if (!_available) return false;

        // beginReading() triggers forced mode, returns approximate
        // end time in ms. We need to wait for it.
        unsigned long endTime = _bme.beginReading();
        if (endTime == 0) {
            Serial.println(F("[ENV] WARNING: Failed to begin measurement"));
            _consecutiveErrors++;
            return false;
        }

        // Wait for measurement to complete
        // Typically ~180ms + heater duration
        if (!_bme.endReading()) {
            Serial.println(F("[ENV] WARNING: Failed to complete measurement"));
            _consecutiveErrors++;
            return false;
        }

        // Read values
        _temperature = _bme.temperature;          // degrees C (float)
        _humidity = _bme.humidity;                 // % (float)
        _pressure = _bme.pressure / 100.0f;       // Pa -> hPa (float)
        _gasResistance = _bme.gas_resistance;      // Ohms (uint32)
        _gasValid = (_bme.gas_resistance > 0);

        _consecutiveErrors = 0;
        _lastReadMs = millis();

        return true;
    }

    // --- Scaled values for FRAM storage ---

    int16_t temperatureScaled() const {
        if (!_available || _consecutiveErrors > 0) return SENSOR_UNAVAILABLE_I16;
        return static_cast<int16_t>(_temperature * 100.0f);
    }

    uint16_t humidityScaled() const {
        if (!_available || _consecutiveErrors > 0) return SENSOR_UNAVAILABLE_U16;
        return static_cast<uint16_t>(_humidity * 10.0f);
    }

    uint16_t pressureScaled() const {
        if (!_available || _consecutiveErrors > 0) return SENSOR_UNAVAILABLE_U16;
        return static_cast<uint16_t>(_pressure * 10.0f);
    }

    uint32_t gasResistanceRaw() const {
        if (!_available || !_gasValid) return SENSOR_UNAVAILABLE_U32;
        return _gasResistance;
    }

    // --- Raw float values for live display ---

    float temperature() const       { return _temperature; }
    float humidity() const          { return _humidity; }
    float pressure() const          { return _pressure; }
    uint32_t gasResistance() const  { return _gasResistance; }

    // --- Status ---

    bool isAvailable() const        { return _available; }
    bool isGasValid() const         { return _gasValid; }

    // Gas sensor needs ~5 minutes to stabilise after power-on
    // and ~1 hour for best accuracy. We track this for status reporting.
    bool isGasStable() const {
        if (!_available) return false;
        return (millis() - _initTimeMs) >= WARMUP_BME680_GAS_MS;
    }

    // Seconds since gas heater was powered on
    uint32_t gasWarmupElapsedSec() const {
        return (millis() - _initTimeMs) / 1000;
    }

    // Approximate IAQ category based on gas resistance
    // This is a rough heuristic — for proper IAQ use Bosch BSEC library
    const char* airQualityLabel() const {
        if (!_gasValid || !isGasStable()) return "Stabilising";
        if (_gasResistance > 300000) return "Excellent";
        if (_gasResistance > 200000) return "Good";
        if (_gasResistance > 100000) return "Fair";
        if (_gasResistance > 50000)  return "Poor";
        return "Bad";
    }

private:
    Adafruit_BME680     _bme;
    bool                _available = false;
    uint32_t            _initTimeMs = 0;
    uint32_t            _lastReadMs = 0;
    uint8_t             _consecutiveErrors = 0;

    // Latest readings
    float               _temperature = 0.0f;
    float               _humidity = 0.0f;
    float               _pressure = 0.0f;
    uint32_t            _gasResistance = 0;
    bool                _gasValid = false;
};
