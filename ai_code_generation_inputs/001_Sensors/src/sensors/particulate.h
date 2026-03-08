// =============================================================================
// sensors/particulate.h — PMSA003I Particulate Matter Sensor Driver (RAK12039)
// PM1.0, PM2.5, PM10 readings via I2C with 30-second warm-up tracking
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Adafruit_PM25AQI.h>
#include "../config.h"

class ParticulateSensor {
public:
    bool init() {
        if (!_aqi.begin_I2C()) {
            Serial.println(F("[PM] ERROR: PMSA003I not found on I2C bus"));
            _available = false;
            return false;
        }

        _available = true;
        _initTimeMs = millis();

        Serial.println(F("[PM] PMSA003I initialised"));
        Serial.printf("[PM] Fan warm-up: %lu seconds required\n",
                      WARMUP_PMSA003I_MS / 1000);

        return true;
    }

    // Read the latest PM data from the sensor
    // The PMSA003I updates its I2C registers once per second internally.
    // Call this at least once per second to keep data fresh.
    // Returns true if new data was available.
    bool read() {
        if (!_available) return false;

        PM25_AQI_Data data;
        if (!_aqi.read(&data)) {
            // Not necessarily an error — sensor may not have new data yet
            _readAttemptsFailed++;
            if (_readAttemptsFailed > 10) {
                Serial.println(F("[PM] WARNING: No data for 10+ consecutive reads"));
                _consecutiveErrors++;
            }
            return false;
        }

        _readAttemptsFailed = 0;
        _consecutiveErrors = 0;

        // Store standard concentration values (corrected to standard atmosphere)
        _pm1_0_std = data.pm10_standard;
        _pm2_5_std = data.pm25_standard;
        _pm10_std  = data.pm100_standard;

        // Store environmental values (ambient conditions)
        _pm1_0_env = data.pm10_env;
        _pm2_5_env = data.pm25_env;
        _pm10_env  = data.pm100_env;

        // Store particle counts per 0.1L
        _particles_03 = data.particles_03um;
        _particles_05 = data.particles_05um;
        _particles_10 = data.particles_10um;
        _particles_25 = data.particles_25um;
        _particles_50 = data.particles_50um;
        _particles_100 = data.particles_100um;

        _lastReadMs = millis();
        _hasData = true;

        return true;
    }

    // --- Scaled values for FRAM storage (using standard concentration) ---

    uint16_t pm1_0() const {
        if (!isReady()) return SENSOR_UNAVAILABLE_U16;
        return _pm1_0_std;
    }

    uint16_t pm2_5() const {
        if (!isReady()) return SENSOR_UNAVAILABLE_U16;
        return _pm2_5_std;
    }

    uint16_t pm10() const {
        if (!isReady()) return SENSOR_UNAVAILABLE_U16;
        return _pm10_std;
    }

    // --- Environmental concentration values (for display) ---

    uint16_t pm1_0_env() const  { return _pm1_0_env; }
    uint16_t pm2_5_env() const  { return _pm2_5_env; }
    uint16_t pm10_env() const   { return _pm10_env; }

    // --- Particle count data (for detailed display) ---

    uint16_t particles03() const  { return _particles_03; }
    uint16_t particles05() const  { return _particles_05; }
    uint16_t particles10() const  { return _particles_10; }
    uint16_t particles25() const  { return _particles_25; }
    uint16_t particles50() const  { return _particles_50; }
    uint16_t particles100() const { return _particles_100; }

    // --- Status ---

    bool isAvailable() const    { return _available; }
    bool hasData() const        { return _hasData; }

    // Sensor needs ~30 seconds of fan operation before readings are reliable
    bool isWarmedUp() const {
        if (!_available) return false;
        return (millis() - _initTimeMs) >= WARMUP_PMSA003I_MS;
    }

    // Ready = warmed up AND has received at least one valid reading
    bool isReady() const {
        return isWarmedUp() && _hasData;
    }

    // Seconds remaining in warm-up period (0 if warmed up)
    uint32_t warmupRemainingSec() const {
        if (isWarmedUp()) return 0;
        return (WARMUP_PMSA003I_MS - (millis() - _initTimeMs)) / 1000;
    }

    // AQI category based on PM2.5 (US EPA breakpoints)
    const char* aqiCategory() const {
        if (!isReady()) return "Warming up";
        if (_pm2_5_std <= 12)  return "Good";
        if (_pm2_5_std <= 35)  return "Moderate";
        if (_pm2_5_std <= 55)  return "Unhealthy (Sensitive)";
        if (_pm2_5_std <= 150) return "Unhealthy";
        if (_pm2_5_std <= 250) return "Very Unhealthy";
        return "Hazardous";
    }

private:
    Adafruit_PM25AQI    _aqi;
    bool                _available = false;
    bool                _hasData = false;
    uint32_t            _initTimeMs = 0;
    uint32_t            _lastReadMs = 0;
    uint8_t             _consecutiveErrors = 0;
    uint16_t            _readAttemptsFailed = 0;

    // Standard concentration (ug/m3, corrected to standard atmosphere)
    uint16_t            _pm1_0_std = 0;
    uint16_t            _pm2_5_std = 0;
    uint16_t            _pm10_std = 0;

    // Environmental concentration (ug/m3, ambient conditions)
    uint16_t            _pm1_0_env = 0;
    uint16_t            _pm2_5_env = 0;
    uint16_t            _pm10_env = 0;

    // Particle counts per 0.1L of air
    uint16_t            _particles_03 = 0;
    uint16_t            _particles_05 = 0;
    uint16_t            _particles_10 = 0;
    uint16_t            _particles_25 = 0;
    uint16_t            _particles_50 = 0;
    uint16_t            _particles_100 = 0;
};
