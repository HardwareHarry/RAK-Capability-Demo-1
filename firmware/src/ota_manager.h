// =============================================================================
// ota_manager.h — Over-the-Air firmware update handler
// Manages ESP32-S3 OTA using built-in Update library
// =============================================================================
#pragma once

#include <Arduino.h>
#include <Update.h>
#include "config.h"
#include "types.h"

class OTAManager {
public:
    OTAManager(SystemHealth& health, DisplayDriver* display = nullptr)
        : _health(health), _display(display), _isUpdating(false), _updateProgress(0) {}

    // Initialize OTA — call once at startup
    void init() {
        Serial.println(F("[OTA] Manager initialized"));
        _isUpdating = false;
        _updateProgress = 0;
    }

    // Start receiving firmware update from HTTP stream
    // Called by web server with Content-Length header
    bool startUpdate(size_t contentLength) {
        if (_isUpdating) {
            Serial.println(F("[OTA] ERROR: Update already in progress"));
            return false;
        }

        if (!Update.begin(contentLength)) {
            Serial.printf("[OTA] ERROR: Cannot start OTA, free space: %u bytes\n",
                         ESP.getFreeSketchSpace());
            return false;
        }

        _isUpdating = true;
        _updateProgress = 0;
        _updateSize = contentLength;
        _updateStartTime = millis();

        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "OTA: Starting...");
        Serial.printf("[OTA] Starting firmware update (%u bytes)\n", contentLength);

        return true;
    }

    // Process incoming OTA data chunk
    // Call repeatedly as data arrives (from web server request body)
    bool writeUpdateData(const uint8_t* data, size_t len) {
        if (!_isUpdating) {
            return false;
        }

        size_t written = Update.write((uint8_t*)data, len);
        if (written != len) {
            Serial.printf("[OTA] ERROR: Wrote %u/%u bytes\n", written, len);
            _finishUpdateFailed();
            return false;
        }

        _updateProgress += written;
        uint8_t percent = (_updateProgress * 100) / _updateSize;

        // Update every 10%
        if (percent > _lastReportedPercent + 10 || percent >= 100) {
            _lastReportedPercent = percent;
            Serial.printf("[OTA] Progress: %u%% (%u/%u bytes)\n",
                         percent, _updateProgress, _updateSize);

            char msg[48];
            snprintf(msg, sizeof(msg), "OTA: %u%% complete", percent);
            _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, msg);

            // Update display if available
            if (_display && percent % 25 == 0) {
                char dispMsg[32];
                snprintf(dispMsg, sizeof(dispMsg), "OTA Update\n%u%%", percent);
                _display->showError(dispMsg);  // Reuse error display for OTA progress
            }
        }

        return true;
    }

    // Finish update and reboot if successful
    bool endUpdate() {
        if (!_isUpdating) {
            return false;
        }

        if (!Update.end(true)) {  // true = reboot after update
            Serial.printf("[OTA] ERROR: Update failed, code: %u\n", Update.getError());
            _finishUpdateFailed();
            return false;
        }

        uint32_t elapsedMs = millis() - _updateStartTime;
        uint32_t speedKbps = (_updateSize / 1024) / (elapsedMs / 1000);

        Serial.printf("[OTA] SUCCESS: Update complete (%u KB in %lu ms, ~%lu KB/s)\n",
                     _updateSize / 1024, elapsedMs, speedKbps);

        _isUpdating = false;
        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "OTA: Rebooting...");

        // Device will reboot automatically due to Update.end(true)
        return true;
    }

    // Abort ongoing update
    void abortUpdate() {
        if (_isUpdating) {
            Update.abort();
            _isUpdating = false;
            Serial.println(F("[OTA] Update aborted"));
            _health.heartbeat(TaskId::WEBSERVER, TaskState::DEGRADED, "OTA aborted");
        }
    }

    // Get current OTA status
    bool isUpdating() const { return _isUpdating; }
    uint8_t getProgress() const { return _updateProgress * 100 / _updateSize; }
    size_t getBytesReceived() const { return _updateProgress; }
    size_t getTotalBytes() const { return _updateSize; }

    // Check free sketch space (max firmware size we can accept)
    static size_t getFreeSketchSpace() {
        return ESP.getFreeSketchSpace();
    }

    // Get current firmware version
    static const char* getFirmwareVersion() {
        return FW_VERSION;  // Defined in config.h
    }

    // Get firmware name
    static const char* getFirmwareName() {
        return FW_NAME;  // Defined in config.h
    }

private:
    SystemHealth& _health;
    DisplayDriver* _display;

    bool _isUpdating;
    size_t _updateProgress;
    size_t _updateSize;
    uint32_t _updateStartTime;
    uint8_t _lastReportedPercent;

    void _finishUpdateFailed() {
        Update.abort();
        _isUpdating = false;
        _updateProgress = 0;

        Serial.println(F("[OTA] Update failed"));
        _health.heartbeat(TaskId::WEBSERVER, TaskState::ERROR, "OTA failed");

        if (_display) {
            _display->showError("OTA Update Failed");
        }
    }
};

