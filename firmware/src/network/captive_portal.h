// =============================================================================
// network/captive_portal.h — Captive portal for AP mode WiFi setup
// Detects captive portal requests and redirects to setup page
// =============================================================================
#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include "../config.h"
#include "../types.h"

#if __has_include(<DNSServer.h>)
    #include <DNSServer.h>
    #define HAS_DNS_SERVER 1
#else
    #define HAS_DNS_SERVER 0
#endif

class CaptivePortal {
public:
    CaptivePortal(AsyncWebServer& server, SystemHealth& health)
        : _server(server), _health(health), _isActive(false), _activatedAtMs(0), _dnsStarted(false) {}

    // Activate captive portal (call when entering AP mode)
    void activate() {
        if (_isActive) return;

        _isActive = true;
        _activatedAtMs = millis();
        Serial.println(F("[CAPTIVE] Portal activated"));
        _health.heartbeat(TaskId::WEBSERVER, TaskState::RUNNING, "Captive portal active");

        // DNS catch-all improves automatic captive portal behavior across OSes.
        _startDnsCatchAll();

        _setupDetectionRoutes();
        _setupPortalPage();
    }

    // Deactivate captive portal (call when switching to STA mode)
    void deactivate() {
        if (!_isActive) return;
        _stopDnsCatchAll();
        _isActive = false;
        Serial.println(F("[CAPTIVE] Portal deactivated"));
    }

    // Must be called periodically from WebServerManager::tick().
    void tick() {
        if (!_isActive) return;

#if HAS_DNS_SERVER
        if (_dnsStarted) {
            _dns.processNextRequest();
        }
#endif

        // Safety timeout: if still in AP setup mode too long, restart cleanly.
        if ((millis() - _activatedAtMs) >= CAPTIVE_PORTAL_TIMEOUT_MS) {
            Serial.println(F("[CAPTIVE] Timeout reached in AP setup mode; rebooting"));
            delay(100);
            ESP.restart();
        }
    }

    bool isActive() const { return _isActive; }

private:
    AsyncWebServer& _server;
    SystemHealth& _health;
    bool _isActive;
    uint32_t _activatedAtMs;
#if HAS_DNS_SERVER
    DNSServer _dns;
#endif
    bool _dnsStarted;

    void _startDnsCatchAll() {
#if HAS_DNS_SERVER
        if (_dnsStarted) return;
        IPAddress apIp = WiFi.softAPIP();
        // Resolve all hostnames to AP while captive portal is active.
        _dns.start(CAPTIVE_DNS_PORT, "*", apIp);
        _dnsStarted = true;
        Serial.printf("[CAPTIVE] DNS catch-all started on %u -> %s\n", CAPTIVE_DNS_PORT, apIp.toString().c_str());
#else
        if (!_dnsStarted) {
            _dnsStarted = true;  // mark once to avoid repeated logs
            Serial.println(F("[CAPTIVE] DNSServer not available; DNS catch-all disabled"));
        }
#endif
    }

    void _stopDnsCatchAll() {
#if HAS_DNS_SERVER
        if (!_dnsStarted) return;
        _dns.stop();
#endif
        _dnsStarted = false;
        Serial.println(F("[CAPTIVE] DNS catch-all stopped"));
    }

    // Handle captive portal detection requests
    // Major OS detection logic:
    // - Android: GETs /generate_204 (expects 204 No Content)
    // - iOS: GETs /hotspot-detect.html (expects 200 with HTML)
    // - Windows: GETs /ncsi.txt (expects 200 with text)
    void _setupDetectionRoutes() {
        // Android captive portal detection
        _server.on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest *request) {
            Serial.println(F("[CAPTIVE] Android detection request"));
            request->redirect("/setup");
        });

        // iOS captive portal detection
        _server.on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest *request) {
            Serial.println(F("[CAPTIVE] iOS detection request"));
            request->redirect("/setup");
        });

        // Windows captive portal detection
        _server.on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest *request) {
            Serial.println(F("[CAPTIVE] Windows detection request"));
            request->redirect("/setup");
        });
    }

    void _setupPortalPage() {
        _server.on("/setup", HTTP_GET, [this](AsyncWebServerRequest *request) {
            String html = _getPortalHTML();
            request->send(200, "text/html; charset=utf-8", html);
        });
    }

    String _getPortalHTML() {
        return R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WisBlock Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 16px;
        }
        .container {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 32px;
            max-width: 400px;
            width: 100%;
        }
        h1 {
            font-size: 28px;
            margin-bottom: 8px;
            color: #333;
            text-align: center;
        }
        .subtitle {
            text-align: center;
            color: #666;
            margin-bottom: 28px;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            font-weight: 500;
            margin-bottom: 8px;
            color: #333;
            font-size: 14px;
        }
        input {
            width: 100%;
            padding: 12px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 16px;
            transition: border-color 0.3s;
            font-family: inherit;
        }
        input:focus {
            outline: none;
            border-color: #667eea;
        }
        button {
            width: 100%;
            padding: 12px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            margin-top: 8px;
        }
        .status {
            margin-top: 20px;
            padding: 12px;
            border-radius: 8px;
            text-align: center;
            display: none;
            font-size: 14px;
        }
        .status.success {
            background: #e8f5e9;
            color: #2e7d32;
            display: block;
        }
        .status.error {
            background: #ffebee;
            color: #c62828;
            display: block;
        }
        .loading {
            display: inline-block;
            width: 16px;
            height: 16px;
            border: 3px solid #f3f3f3;
            border-top: 3px solid #667eea;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin-right: 8px;
            vertical-align: middle;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>WisBlock Setup</h1>
        <p class="subtitle">Configure WiFi Network</p>

        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">Network Name (SSID)</label>
                <input type="text" id="ssid" name="ssid" placeholder="Your WiFi network name" required>
            </div>

            <div class="form-group">
                <label for="password">Password</label>
                <input type="password" id="password" name="password" placeholder="WiFi password">
            </div>

            <button type="submit">Connect</button>
        </form>

        <div id="status" class="status"></div>
    </div>

    <script>
        const form = document.getElementById('wifiForm');
        const statusDiv = document.getElementById('status');

        form.addEventListener('submit', async (e) => {
            e.preventDefault();

            const payload = {
                ssid: document.getElementById('ssid').value,
                password: document.getElementById('password').value,
                connect: true
            };

            statusDiv.textContent = 'Connecting...';
            statusDiv.className = 'status';

            try {
                // Reuse existing firmware endpoint.
                const response = await fetch('/api/wifi', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });

                const data = await response.json();
                if (response.ok && data.status === 'ok') {
                    statusDiv.textContent = 'Connected. Reconnect to your WiFi and open sensorhub.local';
                    statusDiv.className = 'status success';
                } else {
                    statusDiv.textContent = 'Error: ' + (data.error || data.status || 'Unknown error');
                    statusDiv.className = 'status error';
                }
            } catch (err) {
                statusDiv.textContent = 'Connection failed: ' + err.message;
                statusDiv.className = 'status error';
            }
        });
    </script>
</body>
</html>
)";
    }
};

