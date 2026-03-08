// =============================================================================
// sensors/gnss.h — u-blox ZOE-M8Q GNSS Driver (RAK12500)
// Parses NMEA sentences from UART1, provides position and UTC time
// Supports RTC synchronisation on first valid fix
// =============================================================================
#pragma once

#include <Arduino.h>
#include <TinyGPS++.h>
#include "../config.h"

class GnssSensor {
public:
    bool init() {
        // UART1 should already be initialised in main.cpp setup()
        // with Serial1.begin(GNSS_BAUD, SERIAL_8N1, PIN_GNSS_RX, PIN_GNSS_TX)
        // We just verify we can see characters arriving.

        _initTimeMs = millis();
        _available = true;

        Serial.println(F("[GNSS] ZOE-M8Q driver initialised on UART1"));
        Serial.printf("[GNSS] Baud: %d, RX pin: %d, TX pin: %d\n",
                      GNSS_BAUD, PIN_GNSS_RX, PIN_GNSS_TX);
        Serial.println(F("[GNSS] Waiting for satellite fix..."));

        return true;
    }

    // Call this frequently (every loop iteration) to feed NMEA data
    // from UART1 into the TinyGPS++ parser.
    // Returns true if new position data is available since last call.
    bool update() {
        if (!_available) return false;

        bool newData = false;

        // Read all available bytes from GNSS UART
        while (Serial1.available() > 0) {
            char c = Serial1.read();
            _charsProcessed++;

            if (_gps.encode(c)) {
                newData = true;
            }
        }

        // Track whether we've ever received any characters at all
        if (_charsProcessed > 0 && !_receivingData) {
            _receivingData = true;
            Serial.printf("[GNSS] Receiving NMEA data (%lu chars)\n", _charsProcessed);
        }

        // Check for first fix
        if (!_hasEverHadFix && _gps.location.isValid() && _gps.location.isUpdated()) {
            _hasEverHadFix = true;
            _firstFixMs = millis();
            uint32_t ttff = (_firstFixMs - _initTimeMs) / 1000;
            Serial.printf("[GNSS] First fix acquired! TTFF: %lu seconds\n", ttff);
            Serial.printf("[GNSS] Position: %.7f, %.7f\n",
                          _gps.location.lat(), _gps.location.lng());
        }

        // Update fix validity tracking
        if (_gps.location.isValid()) {
            _lastFixMs = millis();
        }

        return newData;
    }

    // --- Position data ---

    // Latitude in degrees x 1e7 (for FRAM storage)
    int32_t latitudeScaled() const {
        if (!hasValidFix()) return SENSOR_UNAVAILABLE_I32;
        return static_cast<int32_t>(_gps.location.lat() * 1e7);
    }

    // Longitude in degrees x 1e7 (for FRAM storage)
    int32_t longitudeScaled() const {
        if (!hasValidFix()) return SENSOR_UNAVAILABLE_I32;
        return static_cast<int32_t>(_gps.location.lng() * 1e7);
    }

    double latitude() const     { return _gps.location.lat(); }
    double longitude() const    { return _gps.location.lng(); }
    double altitude() const     { return _gps.altitude.meters(); }

    // --- Satellite info ---

    uint32_t satellites() const { return _gps.satellites.value(); }
    double hdop() const         { return _gps.hdop.hdop(); }

    // --- Time data (UTC from GNSS) ---

    bool hasValidTime() const {
        return _gps.time.isValid() && _gps.date.isValid() &&
               _gps.date.year() >= 2025;
    }

    uint16_t year() const       { return _gps.date.year(); }
    uint8_t month() const       { return _gps.date.month(); }
    uint8_t day() const         { return _gps.date.day(); }
    uint8_t hour() const        { return _gps.time.hour(); }
    uint8_t minute() const      { return _gps.time.minute(); }
    uint8_t second() const      { return _gps.time.second(); }

    // --- Status ---

    bool isAvailable() const    { return _available; }
    bool isReceiving() const    { return _receivingData; }
    bool hasEverHadFix() const  { return _hasEverHadFix; }

    // Current fix is "valid" if we had a fix within the last 10 seconds
    bool hasValidFix() const {
        if (!_hasEverHadFix) return false;
        return (millis() - _lastFixMs) < 10000;
    }

    // Age of last fix in milliseconds
    uint32_t fixAgeMs() const {
        if (!_hasEverHadFix) return UINT32_MAX;
        return millis() - _lastFixMs;
    }

    // Time to first fix in seconds (0 if no fix yet)
    uint32_t ttffSeconds() const {
        if (!_hasEverHadFix) return 0;
        return (_firstFixMs - _initTimeMs) / 1000;
    }

    // Total NMEA characters received
    uint32_t charsProcessed() const { return _charsProcessed; }

    // Sentences that passed checksum / failed checksum
    uint32_t sentencesValid() const {
        return _gps.passedChecksum();
    }
    uint32_t sentencesFailed() const {
        return _gps.failedChecksum();
    }

    // Status string for display
    void getStatusString(char* buf, size_t len) const {
        if (!_available) {
            snprintf(buf, len, "Not available");
        } else if (!_receivingData) {
            snprintf(buf, len, "No NMEA data");
        } else if (!_hasEverHadFix) {
            uint32_t elapsed = (millis() - _initTimeMs) / 1000;
            snprintf(buf, len, "Searching... %lus (%lu SVs)",
                     elapsed, satellites());
        } else if (hasValidFix()) {
            snprintf(buf, len, "Fix: %lu SVs, HDOP %.1f",
                     satellites(), hdop());
        } else {
            snprintf(buf, len, "Fix lost (%lus ago)",
                     fixAgeMs() / 1000);
        }
    }

private:
    TinyGPSPlus    _gps;
    bool           _available = false;
    bool           _receivingData = false;
    bool           _hasEverHadFix = false;
    uint32_t       _initTimeMs = 0;
    uint32_t       _firstFixMs = 0;
    uint32_t       _lastFixMs = 0;
    uint32_t       _charsProcessed = 0;
};
