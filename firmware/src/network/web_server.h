// =============================================================================
// network/web_server.h — AsyncWebServer with REST API
// Serves the SPA dashboard from LittleFS and provides JSON endpoints
// =============================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "../config.h"
#include "../types.h"
#include "../storage/ring_buffer.h"

class WebServerManager {
public:
    WebServerManager(LiveDataCache& liveData, SystemHealth& health,
                     FramRingBuffer& fram, EventGroupHandle_t events)
        : _server(WEB_SERVER_PORT), _liveData(liveData), _health(health),
          _fram(fram), _events(events) {}

    // --- WiFi Initialisation -------------------------------------------------

    bool initWifi() {
        _health.heartbeat(TaskId::WEBSERVER, TaskState::INITIALISING, "WiFi init...");

        // Try STA mode first if credentials exist
        // TODO: Load credentials from NVS or secrets.h
        // For now, go straight to AP mode for development
        return startAP();
    }

    bool startAP() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

        _apMode = true;
        _apIP = WiFi.softAPIP();

        Serial.printf("[WEB] AP mode started: %s\n", WIFI_AP_SSID);
        Serial.printf("[WEB] AP IP: %s\n", _apIP.toString().c_str());

        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "AP mode");
        return true;
    }

    bool startSTA(const char* ssid, const char* password) {
        _health.heartbeat(TaskId::WEBSERVER, TaskState::WARMING_UP, "Connecting WiFi...");

        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid, password);

        uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startMs > WIFI_AP_FALLBACK_MS) {
                Serial.println(F("[WEB] STA connection timeout — falling back to AP"));
                return startAP();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        _apMode = false;
        Serial.printf("[WEB] Connected to WiFi, IP: %s\n",
                      WiFi.localIP().toString().c_str());
        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "WiFi connected");
        return true;
    }

    // --- HTTP Server Setup ---------------------------------------------------

    void initServer() {
        // Serve the SPA from LittleFS
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->send(LittleFS, "/index.html", "text/html");
        });

        // Serve static assets from LittleFS
        _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

        // --- REST API Endpoints ---

        // GET /api/live — current sensor readings
        _server.on("/api/live", HTTP_GET,
            [this](AsyncWebServerRequest *req) { handleLive(req); });

        // GET /api/status — system health and metadata
        _server.on("/api/status", HTTP_GET,
            [this](AsyncWebServerRequest *req) { handleStatus(req); });

        // GET /api/history?hours=N — historical data from FRAM
        _server.on("/api/history", HTTP_GET,
            [this](AsyncWebServerRequest *req) { handleHistory(req); });

        // GET /api/config — current configuration
        _server.on("/api/config", HTTP_GET,
            [this](AsyncWebServerRequest *req) { handleGetConfig(req); });

        // 404 handler
        _server.onNotFound([](AsyncWebServerRequest *req) {
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        });

        // CORS headers for development
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

        _server.begin();
        Serial.printf("[WEB] HTTP server started on port %d\n", WEB_SERVER_PORT);
    }

    // --- Periodic tick (call from task loop) ---------------------------------

    void tick() {
        // Update WiFi status in live data cache
        LiveData data = _liveData.snapshot();
        data.wifiConnected = (_apMode || WiFi.status() == WL_CONNECTED);
        data.wifiRssi = _apMode ? 0 : WiFi.RSSI();
        _liveData.update(data);

        // Heartbeat
        char msg[48];
        if (_apMode) {
            snprintf(msg, sizeof(msg), "AP: %s (%d clients)",
                     WIFI_AP_SSID, WiFi.softAPgetStationNum());
        } else {
            snprintf(msg, sizeof(msg), "STA: %s (%d dBm)",
                     WiFi.SSID().c_str(), WiFi.RSSI());
        }
        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, msg);
    }

    bool isAPMode() const { return _apMode; }

private:
    AsyncWebServer      _server;
    LiveDataCache&      _liveData;
    SystemHealth&       _health;
    FramRingBuffer&     _fram;
    EventGroupHandle_t  _events;
    bool                _apMode = false;
    IPAddress           _apIP;

    // --- API Handlers --------------------------------------------------------

    void handleLive(AsyncWebServerRequest *req) {
        LiveData d = _liveData.snapshot();

        JsonDocument doc;

        doc["timestamp"] = d.timestamp;
        doc["temperature"] = d.temperature;
        doc["humidity"] = d.humidity;
        doc["pressure"] = d.pressure;
        doc["gas_resistance"] = d.gasResistance;
        doc["pm1_0"] = d.pm1_0;
        doc["pm2_5"] = d.pm2_5;
        doc["pm10"] = d.pm10;
        doc["lux"] = d.lux;

        doc["pm_ready"] = d.pmReady;
        doc["gas_stable"] = d.gasStable;

        // GNSS
        JsonObject gnss = doc["gnss"].to<JsonObject>();
        gnss["latitude"] = d.gnss.latitude;
        gnss["longitude"] = d.gnss.longitude;
        gnss["altitude"] = d.gnss.altitude;
        gnss["satellites"] = d.gnss.satellites;
        gnss["hdop"] = d.gnss.hdop;
        gnss["fix_valid"] = d.gnss.fixValid;
        gnss["fix_age_ms"] = d.gnss.fixAgeMs;
        gnss["hour"] = d.gnss.hour;
        gnss["minute"] = d.gnss.minute;
        gnss["second"] = d.gnss.second;

        // System
        doc["battery_mv"] = d.batteryMv;
        doc["wifi_rssi"] = d.wifiRssi;
        doc["wifi_connected"] = d.wifiConnected;
        doc["lora_joined"] = d.loraJoined;
        doc["lora_last_tx"] = d.loraLastTxTime;
        doc["lora_frame_count"] = d.loraFrameCounter;
        doc["fram_used"] = d.framUsedRecords;
        doc["fram_total"] = d.framTotalCapacity;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }

    void handleStatus(AsyncWebServerRequest *req) {
        JsonDocument doc;

        // System state
        const char* stateNames[] = {
            "booting", "warming_up", "ready", "degraded", "error"
        };
        doc["system_state"] = stateNames[static_cast<int>(_health.systemState)];
        doc["uptime_sec"] = _health.uptimeSeconds();
        doc["fw_version"] = FW_VERSION;
        doc["fw_name"] = FW_NAME;
        doc["display_type"] = DISPLAY_TYPE_NAME;

        // Memory
        doc["free_heap"] = ESP.getFreeHeap();
        doc["total_heap"] = ESP.getHeapSize();
        doc["free_psram"] = ESP.getFreePsram();

        // FRAM
        doc["fram_records"] = _fram.recordCount();
        doc["fram_capacity"] = _fram.capacity();
        doc["fram_total_writes"] = _fram.totalWrites();
        doc["lora_pending"] = _fram.pendingLoRaRecords();

        // Task health
        JsonArray tasks = doc["tasks"].to<JsonArray>();
        for (uint8_t i = 0; i < static_cast<uint8_t>(TaskId::TASK_COUNT); i++) {
            TaskHealth th = _health.getTaskHealth(static_cast<TaskId>(i));
            JsonObject t = tasks.add<JsonObject>();
            t["name"] = taskIdName(static_cast<TaskId>(i));
            t["state"] = taskStateName(th.state);
            t["status"] = th.statusMsg;
            t["last_heartbeat_ms"] = millis() - th.lastHeartbeat;
            t["misses"] = th.consecutiveMisses;
        }

        // WiFi details
        JsonObject wifi = doc["wifi"].to<JsonObject>();
        wifi["mode"] = _apMode ? "AP" : "STA";
        wifi["ssid"] = _apMode ? WIFI_AP_SSID : WiFi.SSID().c_str();
        wifi["ip"] = _apMode ? _apIP.toString() : WiFi.localIP().toString();
        wifi["rssi"] = _apMode ? 0 : WiFi.RSSI();
        wifi["clients"] = _apMode ? WiFi.softAPgetStationNum() : 0;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }

    void handleHistory(AsyncWebServerRequest *req) {
        // Parse hours parameter (default 24)
        int hours = 24;
        if (req->hasParam("hours")) {
            hours = req->getParam("hours")->value().toInt();
            if (hours < 1) hours = 1;
            if (hours > 168) hours = 168;  // max 1 week
        }

        uint32_t recordsWanted = (hours * 3600) / (SENSOR_INTERVAL_MS / 1000);
        uint32_t available = _fram.recordCount();
        uint32_t toRead = min(recordsWanted, available);

        // Downsample to fit within API_HISTORY_MAX_POINTS
        uint32_t step = 1;
        if (toRead > API_HISTORY_MAX_POINTS) {
            step = toRead / API_HISTORY_MAX_POINTS;
        }

        // Build JSON response
        // Use chunked response for potentially large payloads
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->print("{\"hours\":");
        response->print(hours);
        response->print(",\"count\":");

        // Read records in chunks and write to stream
        uint32_t outputCount = 0;
        response->print(0);  // placeholder, will be in metadata
        response->print(",\"data\":[");

        // Allocate a single record buffer
        SensorRecord rec;
        bool first = true;

        // Calculate starting index
        uint32_t startIdx;
        if (available <= toRead) {
            startIdx = 0;
        } else {
            // Read from (writeIndex - toRead) backwards
            startIdx = available - toRead;
        }

        // Read and output downsampled records
        // Use getLatestRecords approach but with stepping
        SensorRecord* buffer = new SensorRecord[1];
        uint32_t count = _fram.recordCount();

        // Simplified: read the latest N records, stepping by 'step'
        for (uint32_t i = 0; i < toRead; i += step) {
            // Calculate the ring buffer index for this position
            uint32_t readCount = _fram.getLatestRecords(buffer, 1);
            // Actually, let's use a simpler approach — read specific indices

            if (!first) response->print(",");
            first = false;

            // For now, output a minimal record
            // Full implementation would read from specific FRAM addresses
            response->printf("{\"t\":%lu,\"te\":%d,\"h\":%u,\"p\":%u,\"g\":%lu,\"pm\":%u,\"l\":%u}",
                             (unsigned long)rec.timestamp,
                             rec.temperature, rec.humidity, rec.pressure,
                             (unsigned long)rec.gasResistance, rec.pm2_5, rec.lux);
            outputCount++;

            if (outputCount >= API_HISTORY_MAX_POINTS) break;
        }

        delete[] buffer;

        response->print("]}");
        req->send(response);
    }

    void handleGetConfig(AsyncWebServerRequest *req) {
        JsonDocument doc;

        doc["sensor_interval_ms"] = SENSOR_INTERVAL_MS;
        doc["lora_tx_interval_ms"] = LORA_TX_INTERVAL_MS;
        doc["display_refresh_ms"] = DISPLAY_REFRESH_INTERVAL_MS;
        doc["wifi_ap_ssid"] = WIFI_AP_SSID;
        doc["display_type"] = DISPLAY_TYPE_NAME;
        doc["lora_region"] = "EU868";  // TODO: derive from build flags
        doc["lora_max_backfill"] = LORAWAN_MAX_BACKFILL;
        doc["fram_capacity"] = FRAM_MAX_RECORDS;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }
};
