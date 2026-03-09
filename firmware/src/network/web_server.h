// =============================================================================
// network/web_server.h — AsyncWebServer with REST API
// Serves the SPA dashboard from LittleFS and provides JSON endpoints
// Uses WifiManager for connection handling and mDNS
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
#include "wifi_manager.h"
#include "captive_portal.h"
#include "../ota_manager.h"
#include "../location/geocoder.h"

// Globals owned by main.cpp and initialized in webServerTaskFn.
extern OTAManager* g_otaManager;
extern Geocoder* g_geocoder;

class WebServerManager {
public:
    WebServerManager(LiveDataCache& liveData, SystemHealth& health,
                     FramRingBuffer& fram, EventGroupHandle_t events)
        : _server(WEB_SERVER_PORT), _wifi(health),
          _captivePortal(nullptr),
          _liveData(liveData), _health(health),
          _fram(fram), _events(events) {}

    // --- Initialisation (call once from task) --------------------------------

    void init() {
        _wifi.init();

        // Initialize captive portal
        _captivePortal = new CaptivePortal(_server, _health);
        if (_wifi.isAPMode()) {
            _captivePortal->activate();
        }

        _setupRoutes();
        _server.begin();
        Serial.printf("[WEB] HTTP server started on port %d\n", WEB_SERVER_PORT);
        Serial.printf("[WEB] Dashboard: http://%s.local/\n", _wifi.hostname());
    }

    // --- Periodic tick (call from task loop) ---------------------------------

    void tick() {
        _wifi.tick();

        // Update WiFi status in live data cache
        LiveData data = _liveData.snapshot();
        data.wifiConnected = _wifi.isConnected();
        data.wifiRssi = _wifi.rssi();
        _liveData.update(data);

        // Manage captive portal based on WiFi mode
        if (_wifi.isAPMode() && _captivePortal && !_captivePortal->isActive()) {
            _captivePortal->activate();
            Serial.println(F("[WEB] Captive portal activated (AP mode)"));
        }
        if (_wifi.isSTAMode() && _captivePortal && _captivePortal->isActive()) {
            _captivePortal->deactivate();
            Serial.println(F("[WEB] Captive portal deactivated (STA mode)"));
        }

        // Captive DNS + timeout processing.
        if (_captivePortal) {
            _captivePortal->tick();
        }

        // Heartbeat
        char msg[48];
        _wifi.getStatusString(msg, sizeof(msg));
        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, msg);
    }

    // --- Accessors -----------------------------------------------------------

    WifiManager& wifi() { return _wifi; }

private:
    AsyncWebServer      _server;
    WifiManager         _wifi;
    CaptivePortal*      _captivePortal;
    LiveDataCache&      _liveData;
    SystemHealth&       _health;
    FramRingBuffer&     _fram;
    EventGroupHandle_t  _events;

    // --- Route Setup ---------------------------------------------------------

    void _setupRoutes() {
        // Serve the SPA from LittleFS
        _server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
            req->send(LittleFS, "/index.html", "text/html");
        });

        // Serve static assets from LittleFS
        _server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

        // --- REST API Endpoints ---

        _server.on("/api/live", HTTP_GET,
            [this](AsyncWebServerRequest *req) { _handleLive(req); });

        _server.on("/api/status", HTTP_GET,
            [this](AsyncWebServerRequest *req) { _handleStatus(req); });

        _server.on("/api/history", HTTP_GET,
            [this](AsyncWebServerRequest *req) { _handleHistory(req); });

        _server.on("/api/config", HTTP_GET,
            [this](AsyncWebServerRequest *req) { _handleGetConfig(req); });

        // --- WiFi Configuration Endpoints ---

        // POST /api/wifi — save new WiFi credentials and optionally connect
        _server.on("/api/wifi", HTTP_POST,
            [](AsyncWebServerRequest *req) {},  // body handler below
            nullptr,
            [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                   size_t index, size_t total) {
                _handleSetWifi(req, data, len);
            });

        // GET /api/wifi — get current WiFi status (no passwords exposed)
        _server.on("/api/wifi", HTTP_GET,
            [this](AsyncWebServerRequest *req) { _handleGetWifi(req); });

        // --- OTA Firmware Update Endpoints ---

        // POST /api/firmware/upload — upload new firmware binary
        _server.on("/api/firmware/upload", HTTP_POST,
            [this](AsyncWebServerRequest *req) {
                // Check if OTA is available
                if (!g_otaManager) {
                    req->send(503, "application/json",
                              "{\"error\":\"OTA system not initialized\"}");
                    return;
                }
                // Start OTA if content length is valid
                if (req->contentLength() > 0) {
                    if (!g_otaManager->startUpdate(req->contentLength())) {
                        req->send(400, "application/json",
                                  "{\"error\":\"Cannot start OTA update\"}");
                    }
                }
            },
            nullptr,
            [this](AsyncWebServerRequest *req, uint8_t *data, size_t len,
                   size_t index, size_t total) {
                // Handle firmware binary chunks
                if (g_otaManager && g_otaManager->isUpdating()) {
                    if (!g_otaManager->writeUpdateData(data, len)) {
                        req->send(400, "application/json",
                                  "{\"error\":\"OTA write failed\"}");
                    }
                }
                // On final chunk, finish update
                if (index + len == total) {
                    if (g_otaManager && g_otaManager->endUpdate()) {
                        // Will reboot, so this response may not reach client
                        req->send(200, "application/json",
                                  "{\"success\":true,\"message\":\"Rebooting with new firmware\"}");
                    }
                }
            });

        // GET /api/firmware/version — get current firmware version info
        _server.on("/api/firmware/version", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
                JsonDocument doc;
                doc["version"] = FW_VERSION;
                doc["name"] = FW_NAME;
                doc["build_date"] = __DATE__;
                doc["build_time"] = __TIME__;
                String json;
                serializeJson(doc, json);
                req->send(200, "application/json", json);
            });

        // GET /api/location — get current location (city name from GPS via offline KD-tree)
        _server.on("/api/location", HTTP_GET,
            [this](AsyncWebServerRequest *req) {
                LiveData data = _liveData.snapshot();
                JsonDocument doc;
                if (data.gnss.fixValid && g_geocoder) {
                    char locBuf[96] = {0};
                    g_geocoder->getLocationName(data.gnss.latitude,
                                                data.gnss.longitude,
                                                locBuf, sizeof(locBuf), true);
                    GeocodeMeta meta = g_geocoder->lastMeta();
                    doc["name"] = locBuf;
                    doc["source"] = Geocoder::sourceName(meta.source);
                    doc["used_cache"] = meta.usedCache;
                    doc["cache_age_ms"] = meta.cacheAgeMs;
                    doc["movement_since_lookup_m"] = meta.movementSinceLookupM;
                    doc["latitude"] = data.gnss.latitude / 1e7;
                    doc["longitude"] = data.gnss.longitude / 1e7;
                    doc["has_fix"] = true;
                } else {
                    doc["name"] = "unknown";
                    doc["source"] = "none";
                    doc["used_cache"] = false;
                    doc["cache_age_ms"] = 0;
                    doc["movement_since_lookup_m"] = 0;
                    doc["has_fix"] = false;
                }
                String json;
                serializeJson(doc, json);
                req->send(200, "application/json", json);
            });

        // POST /api/wifi/ap — switch to AP mode
        _server.on("/api/wifi/ap", HTTP_POST,
            [this](AsyncWebServerRequest *req) {
                _wifi.switchToAP();
                req->send(200, "application/json",
                          "{\"status\":\"ok\",\"mode\":\"AP\"}");
            });

        // POST /api/wifi/clear — clear stored credentials
        _server.on("/api/wifi/clear", HTTP_POST,
            [this](AsyncWebServerRequest *req) {
                _wifi.clearCredentials();
                req->send(200, "application/json",
                          "{\"status\":\"ok\",\"message\":\"credentials cleared\"}");
            });

        // 404 handler
        _server.onNotFound([this](AsyncWebServerRequest *req) {
            if (_captivePortal && _captivePortal->isActive() && _wifi.isAPMode()) {
                req->redirect("/setup");
                return;
            }
            req->send(404, "application/json", "{\"error\":\"not found\"}");
        });

        // CORS headers for development
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods",
                                             "GET, POST, OPTIONS");
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers",
                                             "Content-Type");
    }

    // --- API Handlers --------------------------------------------------------

    void _handleLive(AsyncWebServerRequest *req) {
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

    void _handleStatus(AsyncWebServerRequest *req) {
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

        // System Health - Temperature Monitoring
        doc["chip_temperature_c"] = _health.getChipTemperatureC();
        const char* thermalStates[] = {"normal", "warm", "critical"};
        doc["thermal_state"] = thermalStates[_health.getThermalState()];

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
        wifi["mode"] = _wifi.isAPMode() ? "AP" : "STA";
        wifi["ssid"] = _wifi.ssid();
        wifi["ip"] = _wifi.localIP().toString();
        wifi["rssi"] = _wifi.rssi();
        wifi["hostname"] = String(_wifi.hostname()) + ".local";
        wifi["clients"] = _wifi.apClients();

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }

    void _handleHistory(AsyncWebServerRequest *req) {
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

        // Build JSON response using chunked streaming
        AsyncResponseStream *response = req->beginResponseStream("application/json");
        response->printf("{\"hours\":%d,\"step\":%lu,\"data\":[", hours, step);

        SensorRecord rec;
        bool first = true;
        uint32_t outputCount = 0;

        // Read downsampled records from FRAM
        // We read the latest 'toRead' records, stepping by 'step'
        uint32_t readStart = (available > toRead) ? (available - toRead) : 0;

        for (uint32_t i = readStart; i < available && outputCount < API_HISTORY_MAX_POINTS; i += step) {
            // Calculate actual ring buffer index
            uint32_t ringIdx = (_fram.recordCount() < FRAM_MAX_RECORDS)
                ? i  // buffer hasn't wrapped
                : i % FRAM_MAX_RECORDS;

            if (_fram.readRecord(ringIdx, rec)) {
                if (!first) response->print(",");
                first = false;

                response->printf(
                    "{\"t\":%lu,\"te\":%d,\"h\":%u,\"p\":%u,\"g\":%lu,\"pm\":%u,\"l\":%u}",
                    (unsigned long)rec.timestamp,
                    rec.temperature, rec.humidity, rec.pressure,
                    (unsigned long)rec.gasResistance, rec.pm2_5, rec.lux);
                outputCount++;
            }
        }

        response->printf("],\"count\":%lu}", outputCount);
        req->send(response);
    }

    void _handleGetConfig(AsyncWebServerRequest *req) {
        JsonDocument doc;

        doc["sensor_interval_ms"] = SENSOR_INTERVAL_MS;
        doc["lora_tx_interval_ms"] = LORA_TX_INTERVAL_MS;
        doc["display_refresh_ms"] = DISPLAY_REFRESH_INTERVAL_MS;
        doc["display_type"] = DISPLAY_TYPE_NAME;
        doc["lora_region"] = "EU868";  // TODO: derive from build flags
        doc["lora_max_backfill"] = LORAWAN_MAX_BACKFILL;
        doc["fram_capacity"] = FRAM_MAX_RECORDS;
        doc["hostname"] = String(_wifi.hostname()) + ".local";

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }

    // --- WiFi Configuration Handlers -----------------------------------------

    void _handleSetWifi(AsyncWebServerRequest *req, uint8_t *data, size_t len) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, data, len);

        if (err) {
            req->send(400, "application/json",
                      "{\"error\":\"invalid JSON\"}");
            return;
        }

        const char* ssid = doc["ssid"];
        const char* pass = doc["password"];
        bool connect = doc["connect"] | true;

        if (!ssid || strlen(ssid) == 0) {
            req->send(400, "application/json",
                      "{\"error\":\"ssid is required\"}");
            return;
        }

        // Save and optionally connect
        // Note: if connect=true and it succeeds, the AP will shut down
        // and the client will need to reconnect on the new network
        bool saved = _wifi.saveCredentials(ssid, pass ? pass : "", connect);

        JsonDocument resp;
        resp["status"] = saved ? "ok" : "failed";
        resp["ssid"] = ssid;
        resp["mode"] = _wifi.isSTAMode() ? "STA" : "AP";
        resp["ip"] = _wifi.localIP().toString();
        resp["hostname"] = String(_wifi.hostname()) + ".local";

        if (saved && connect && _wifi.isSTAMode()) {
            resp["message"] = "Connected! Reconnect to your network and "
                              "browse to http://" + String(_wifi.hostname()) + ".local/";
        }

        String json;
        serializeJson(resp, json);
        req->send(200, "application/json", json);
    }

    void _handleGetWifi(AsyncWebServerRequest *req) {
        JsonDocument doc;

        doc["mode"] = _wifi.isAPMode() ? "AP" : "STA";
        doc["ssid"] = _wifi.ssid();
        doc["ip"] = _wifi.localIP().toString();
        doc["rssi"] = _wifi.rssi();
        doc["hostname"] = String(_wifi.hostname()) + ".local";
        doc["ap_ssid"] = WIFI_AP_SSID;
        doc["connected"] = _wifi.isConnected();
        doc["clients"] = _wifi.apClients();

        // Don't expose passwords in the API

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    }
};
