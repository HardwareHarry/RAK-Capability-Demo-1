// =============================================================================
// network/wifi_manager.h — WiFi connection management
// STA mode (home network) with AP fallback for setup/travel
// mDNS for local discovery (sensorhub.local)
// =============================================================================
#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "../config.h"
#include "../types.h"

// =============================================================================
// WiFi Credentials — loaded from secrets.h, NVS, or AP captive portal
//
// Create a file src/secrets.h (gitignored) with:
//   #define WIFI_STA_SSID "YourNetworkName"
//   #define WIFI_STA_PASS "YourPassword"
//
// If secrets.h doesn't exist, credentials are loaded from NVS
// (saved via the web config API). If NVS is also empty, AP mode is used.
// =============================================================================

#if __has_include("../secrets.h")
    #include "../secrets.h"
    #define HAS_COMPILE_TIME_CREDS 1
#else
    #define HAS_COMPILE_TIME_CREDS 0
#endif

class WifiManager {
public:
    WifiManager(SystemHealth& health) : _health(health) {}

    // --- Initialise WiFi — tries STA first, falls back to AP ----------------

    bool init() {
        _health.heartbeat(TaskId::WEBSERVER, TaskState::INITIALISING, "WiFi init...");

        // Disable WiFi power saving for reliability
        WiFi.setSleep(false);

        // Try to load credentials: compile-time first, then NVS
        String ssid, password;
        bool haveCreds = _loadCredentials(ssid, password);

        if (haveCreds) {
            Serial.printf("[WIFI] Attempting STA connection to '%s'\n", ssid.c_str());
            if (_connectSTA(ssid.c_str(), password.c_str())) {
                _startMDNS();
                return true;
            }
            Serial.println(F("[WIFI] STA connection failed — falling back to AP"));
        } else {
            Serial.println(F("[WIFI] No stored credentials — starting AP mode"));
        }

        // Fall back to AP mode
        _startAP();
        _startMDNS();
        return true;
    }

    // --- Periodic health check (call from web server task loop) --------------

    void tick() {
        if (_mode == WIFI_STA) {
            // Check if STA connection dropped
            if (WiFi.status() != WL_CONNECTED) {
                _staDisconnectedMs += 5000;  // tick interval

                if (_staDisconnectedMs > _reconnectTimeoutMs) {
                    Serial.println(F("[WIFI] STA disconnected — attempting reconnect"));
                    _staDisconnectedMs = 0;
                    _reconnectAttempts++;

                    WiFi.reconnect();

                    // After several failed reconnects, fall back to AP
                    if (_reconnectAttempts > 5) {
                        Serial.println(F("[WIFI] Multiple reconnect failures — "
                                         "switching to AP mode"));
                        _startAP();
                        _reconnectAttempts = 0;
                    }
                }
            } else {
                _staDisconnectedMs = 0;
                _reconnectAttempts = 0;
            }
        }
    }

    // --- Credential management -----------------------------------------------

    // Save new WiFi credentials to NVS and optionally connect
    bool saveCredentials(const char* ssid, const char* password, bool connect = true) {
        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", password);
        prefs.end();

        Serial.printf("[WIFI] Credentials saved for '%s'\n", ssid);

        if (connect) {
            return switchToSTA(ssid, password);
        }
        return true;
    }

    // Clear stored credentials from NVS
    void clearCredentials() {
        Preferences prefs;
        prefs.begin("wifi", false);
        prefs.remove("ssid");
        prefs.remove("pass");
        prefs.end();
        Serial.println(F("[WIFI] Stored credentials cleared"));
    }

    // Switch from AP to STA mode (e.g., after credentials entered via web UI)
    bool switchToSTA(const char* ssid, const char* password) {
        Serial.printf("[WIFI] Switching to STA mode: '%s'\n", ssid);

        WiFi.softAPdisconnect(true);
        if (_connectSTA(ssid, password)) {
            _startMDNS();
            return true;
        }

        // Failed — restart AP
        Serial.println(F("[WIFI] STA switch failed — restarting AP"));
        _startAP();
        return false;
    }

    // Switch to AP mode (e.g., for travel/demo)
    void switchToAP() {
        WiFi.disconnect(true);
        _startAP();
        _startMDNS();
    }

    // --- Status accessors ----------------------------------------------------

    bool isConnected() const {
        return (_mode == WIFI_STA && WiFi.status() == WL_CONNECTED) ||
               _mode == WIFI_AP;
    }

    bool isSTAMode() const      { return _mode == WIFI_STA; }
    bool isAPMode() const       { return _mode == WIFI_AP; }

    int8_t rssi() const {
        if (_mode == WIFI_STA) return WiFi.RSSI();
        return 0;
    }

    IPAddress localIP() const {
        if (_mode == WIFI_STA) return WiFi.localIP();
        return WiFi.softAPIP();
    }

    String ssid() const {
        if (_mode == WIFI_STA) return WiFi.SSID();
        return String(WIFI_AP_SSID);
    }

    uint8_t apClients() const {
        if (_mode == WIFI_AP) return WiFi.softAPgetStationNum();
        return 0;
    }

    const char* hostname() const { return MDNS_HOSTNAME; }

    // Status string for heartbeat
    void getStatusString(char* buf, size_t len) const {
        if (_mode == WIFI_STA && WiFi.status() == WL_CONNECTED) {
            snprintf(buf, len, "STA: %s (%d dBm)",
                     WiFi.SSID().c_str(), WiFi.RSSI());
        } else if (_mode == WIFI_AP) {
            snprintf(buf, len, "AP: %s (%d clients)",
                     WIFI_AP_SSID, WiFi.softAPgetStationNum());
        } else {
            snprintf(buf, len, "Disconnected (reconnecting)");
        }
    }

private:
    SystemHealth&   _health;
    wifi_mode_t     _mode = WIFI_OFF;
    uint32_t        _staDisconnectedMs = 0;
    uint32_t        _reconnectTimeoutMs = 15000;  // try reconnect every 15s
    uint8_t         _reconnectAttempts = 0;

    static constexpr const char* MDNS_HOSTNAME = "sensorhub";

    // --- Load credentials from compile-time or NVS --------------------------

    bool _loadCredentials(String& ssid, String& password) {
        // Priority 1: Compile-time from secrets.h
        #if HAS_COMPILE_TIME_CREDS
            #ifdef WIFI_STA_SSID
                ssid = WIFI_STA_SSID;
                #ifdef WIFI_STA_PASS
                    password = WIFI_STA_PASS;
                #else
                    password = "";
                #endif
                Serial.println(F("[WIFI] Using compile-time credentials"));
                return true;
            #endif
        #endif

        // Priority 2: NVS stored credentials
        Preferences prefs;
        prefs.begin("wifi", true);  // read-only
        ssid = prefs.getString("ssid", "");
        password = prefs.getString("pass", "");
        prefs.end();

        if (ssid.length() > 0) {
            Serial.println(F("[WIFI] Using NVS stored credentials"));
            return true;
        }

        return false;
    }

    // --- Connect to a WiFi network in STA mode ------------------------------

    bool _connectSTA(const char* ssid, const char* password) {
        _health.heartbeat(TaskId::WEBSERVER, TaskState::WARMING_UP,
                          "Connecting WiFi...");

        WiFi.mode(WIFI_STA);
        WiFi.setHostname(MDNS_HOSTNAME);
        WiFi.begin(ssid, password);

        uint32_t startMs = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - startMs > WIFI_AP_FALLBACK_MS) {
                Serial.printf("[WIFI] Connection to '%s' timed out after %lu ms\n",
                              ssid, WIFI_AP_FALLBACK_MS);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(500));

            // Update heartbeat with progress
            char msg[48];
            snprintf(msg, sizeof(msg), "Connecting '%s' (%lus)",
                     ssid, (millis() - startMs) / 1000);
            _health.heartbeat(TaskId::WEBSERVER, TaskState::WARMING_UP, msg);
        }

        _mode = WIFI_STA;
        Serial.printf("[WIFI] Connected to '%s'\n", ssid);
        Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
        Serial.printf("[WIFI] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());

        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "WiFi connected");
        return true;
    }

    // --- Start AP mode -------------------------------------------------------

    void _startAP() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);

        _mode = WIFI_AP;

        Serial.printf("[WIFI] AP started: '%s' (password: '%s')\n",
                      WIFI_AP_SSID, WIFI_AP_PASSWORD);
        Serial.printf("[WIFI] AP IP: %s\n", WiFi.softAPIP().toString().c_str());

        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "AP mode");
    }

    // --- Start mDNS responder ------------------------------------------------

    void _startMDNS() {
        if (MDNS.begin(MDNS_HOSTNAME)) {
            // Advertise the web server
            MDNS.addService("http", "tcp", WEB_SERVER_PORT);

            // Advertise as a sensor hub for Home Assistant discovery
            // HA can discover devices advertising _esphomeweb or custom services
            MDNS.addService("wisblock-sensor", "tcp", WEB_SERVER_PORT);

            // Add TXT records with device info for discovery
            // These appear in mDNS browse results
            MDNS.addServiceTxt("http", "tcp", "firmware", FW_NAME);
            MDNS.addServiceTxt("http", "tcp", "version", FW_VERSION);
            MDNS.addServiceTxt("http", "tcp", "api", "/api/live");

            Serial.printf("[WIFI] mDNS started: %s.local\n", MDNS_HOSTNAME);
            Serial.printf("[WIFI] Dashboard: http://%s.local/\n", MDNS_HOSTNAME);
        } else {
            Serial.println(F("[WIFI] WARNING: mDNS failed to start"));
        }
    }
};
