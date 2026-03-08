// =============================================================================
// sensors/rtc.h — RV-3028-C7 Real-Time Clock Driver (RAK12002)
// Provides timestamps for FRAM records and system time
// Supports synchronisation from GNSS UTC time
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <RV-3028-C7.h>
#include "../config.h"

class RtcDriver {
public:
    bool init() {
        if (!_rtc.begin()) {
            Serial.println(F("[RTC] ERROR: RV-3028 not found on I2C bus"));
            _available = false;
            return false;
        }

        // Use 24-hour mode
        _rtc.set24Hour();

        // Enable battery switchover for supercap backup
        // Mode 1 = Direct Switching Mode (recommended for supercap)
        // This function is unavailable in this driver.
        // _rtc.enableBatteryInterrupt();

        _available = true;
        Serial.println(F("[RTC] RV-3028 initialised"));

        // Read current time to check if it's valid
        if (_rtc.updateTime()) {
            Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n",
                          _rtc.getYear() + 2000, _rtc.getMonth(), _rtc.getDate(),
                          _rtc.getHours(), _rtc.getMinutes(), _rtc.getSeconds());

            // Check if time looks valid (year > 2024 suggests it's been set)
            if (_rtc.getYear() + 2000 >= 2025) {
                _timeValid = true;
            } else {
                Serial.println(F("[RTC] Time appears unset — waiting for GNSS sync"));
                _timeValid = false;
            }
        }

        return true;
    }

    // Get current time as Unix epoch (seconds since 1970-01-01)
    uint32_t getEpoch() {
        if (!_available) return 0;
        if (!_rtc.updateTime()) return 0;

        // Convert to Unix epoch using a simple calculation
        // tm struct: year since 1900, month 0-11
        struct tm timeinfo;
        timeinfo.tm_year = (_rtc.getYear() + 2000) - 1900;
        timeinfo.tm_mon  = _rtc.getMonth() - 1;  // RV-3028 months are 1-12
        timeinfo.tm_mday = _rtc.getDate();
        timeinfo.tm_hour = _rtc.getHours();
        timeinfo.tm_min  = _rtc.getMinutes();
        timeinfo.tm_sec  = _rtc.getSeconds();
        timeinfo.tm_isdst = 0;

        time_t epoch = mktime(&timeinfo);
        return static_cast<uint32_t>(epoch);
    }

    // Set RTC from GNSS UTC time
    bool setFromGnss(uint16_t year, uint8_t month, uint8_t day,
                     uint8_t hour, uint8_t minute, uint8_t second) {
        if (!_available) return false;

        // Sanity check
        if (year < 2025 || year > 2099 || month < 1 || month > 12 ||
            day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
            Serial.println(F("[RTC] Invalid GNSS time — ignoring"));
            return false;
        }

        // Only set if time differs by more than 2 seconds to avoid
        // unnecessary writes during normal operation
        if (_timeValid) {
            _rtc.updateTime();
            int32_t currentYear = _rtc.getYear() + 2000;
            int32_t drift = abs((int32_t)second - (int32_t)_rtc.getSeconds());
            if (currentYear == year && drift <= 2) {
                return true;  // Already in sync
            }
        }

        bool ok = _rtc.setTime(second, minute, hour, 0, day, month, year - 2000);
        if (ok) {
            _timeValid = true;
            _lastSyncMs = millis();
            Serial.printf("[RTC] Synchronised from GNSS: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                          year, month, day, hour, minute, second);
        } else {
            Serial.println(F("[RTC] ERROR: Failed to set time"));
        }
        return ok;
    }

    // Get human-readable time string for display
    void getTimeString(char* buf, size_t len) {
        if (!_available || !_timeValid) {
            snprintf(buf, len, "--:--:--");
            return;
        }
        _rtc.updateTime();
        snprintf(buf, len, "%02d:%02d:%02d",
                 _rtc.getHours(), _rtc.getMinutes(), _rtc.getSeconds());
    }

    void getDateString(char* buf, size_t len) {
        if (!_available || !_timeValid) {
            snprintf(buf, len, "----/--/--");
            return;
        }
        _rtc.updateTime();
        snprintf(buf, len, "%04d-%02d-%02d",
                 _rtc.getYear() + 2000, _rtc.getMonth(), _rtc.getDate());
    }

    bool isAvailable() const    { return _available; }
    bool isTimeValid() const    { return _timeValid; }
    uint32_t lastSyncMs() const { return _lastSyncMs; }

private:
    RV3028  _rtc;
    bool    _available = false;
    bool    _timeValid = false;
    uint32_t _lastSyncMs = 0;
};
