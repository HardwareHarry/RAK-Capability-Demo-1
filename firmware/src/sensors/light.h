// =============================================================================
// sensors/light.h — VEML7700 Ambient Light Sensor Driver (RAK12010)
// Simple I2C lux reading with auto-gain and integration time
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Adafruit_VEML7700.h>
#include "../config.h"

class LightSensor {
public:
    bool init() {
        if (!_veml.begin()) {
            Serial.println(F("[LIGHT] ERROR: VEML7700 not found on I2C bus"));
            _available = false;
            return false;
        }

        // Configure for general-purpose indoor/outdoor use
        // Gain x1, integration time 100ms gives good range 0-~6500 lux
        // Auto-ranging handles extremes
        _veml.setGain(VEML7700_GAIN_1);
        _veml.setIntegrationTime(VEML7700_IT_100MS);

        // Enable power saving mode 1 (wakes every ~500ms)
        // Good balance of responsiveness and power
        _veml.powerSaveEnable(true);
        _veml.setPowerSaveMode(VEML7700_POWERSAVE_MODE1);

        _available = true;
        Serial.println(F("[LIGHT] VEML7700 initialised"));

        // Take an initial reading to verify
        float lux = _veml.readLux();
        Serial.printf("[LIGHT] Initial reading: %.1f lux\n", lux);

        return true;
    }

    // Read lux value, returns scaled uint16 (lux x 10)
    // Returns SENSOR_UNAVAILABLE_U16 on failure
    uint16_t readLux() {
        if (!_available) return SENSOR_UNAVAILABLE_U16;

        float lux = _veml.readLux();

        // Sanity check — VEML7700 range is 0 to ~120,000 lux
        if (lux < 0 || lux > 120000.0f || isnan(lux)) {
            _consecutiveErrors++;
            if (_consecutiveErrors > 5) {
                Serial.println(F("[LIGHT] WARNING: Multiple consecutive read failures"));
            }
            return SENSOR_UNAVAILABLE_U16;
        }

        _consecutiveErrors = 0;
        _lastLux = lux;

        // Scale to uint16: lux x 10
        // Max representable: 6553.5 lux in uint16 at x10 scale
        // For higher values, cap at 65534 (which represents 6553.4 lux)
        // This covers most indoor/outdoor scenarios without needing float in FRAM
        uint32_t scaled = static_cast<uint32_t>(lux * 10.0f);
        if (scaled > 65534) scaled = 65534;

        return static_cast<uint16_t>(scaled);
    }

    // Get raw float value for live display (more precision than stored value)
    float readLuxFloat() {
        if (!_available) return -1.0f;
        return _veml.readLux();
    }

    // Get the last successfully read value
    float lastLux() const       { return _lastLux; }
    bool isAvailable() const    { return _available; }

private:
    Adafruit_VEML7700   _veml;
    bool                _available = false;
    float               _lastLux = 0.0f;
    uint8_t             _consecutiveErrors = 0;
};
