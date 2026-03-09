// =============================================================================
// display/display_factory.h — Display driver factory and implementations
// Compile-time selection via DISPLAY_EINK or DISPLAY_TFT build flags
// E-Ink (RAK14000): 360×240, 3-color, persistent, low-power
// TFT (RAK14014): 240×320, 16-bit color, fast-refresh, visual appeal
// =============================================================================
#pragma once

#include "../types.h"
#include "../config.h"
#include <Adafruit_GFX.h>

// =============================================================================
// Drawing Utilities — Shared between E-Ink and TFT
// =============================================================================

namespace DisplayUtils {
    // Convert task/system state to status character
    inline char getStatusChar(TaskState state) {
        switch (state) {
            case TaskState::RUNNING:      return '*';  // Asterisk for ready
            case TaskState::WARMING_UP:   return '~';  // Tilde for warming
            case TaskState::DEGRADED:     return '!';  // Exclamation for degraded
            case TaskState::ERROR:        return 'X';  // X for error
            default:                      return '?';
        }
    }

    // Get status bar fill character (0-5 filled blocks)
    inline void getProgressBar(uint8_t filled, char* bar) {
        static const char* BLOCKS[] = {"-----", "#----", "##---", "###--", "####-", "#####"};
        filled = (filled > 5) ? 5 : filled;
        strcpy(bar, BLOCKS[filled]);
    }

    // Format temperature with degree symbol
    inline void formatTemp(float celsius, char* buf, size_t len) {
        snprintf(buf, len, "%.1f°C", celsius);
    }

    // Format humidity
    inline void formatHumidity(uint16_t humidity, char* buf, size_t len) {
        snprintf(buf, len, "%.1f%%", humidity / 10.0f);
    }

    // Format pressure
    inline void formatPressure(uint16_t pressure, char* buf, size_t len) {
        snprintf(buf, len, "%u hPa", pressure / 10);
    }

    // Get air quality label from VOC resistance
    inline const char* getAirQualityLabel(uint32_t gasResistance) {
        if (gasResistance > 300000) return "Excellent";
        if (gasResistance > 200000) return "Good";
        if (gasResistance > 100000) return "Fair";
        if (gasResistance > 50000)  return "Poor";
        return "Bad";
    }

    // Get AQI color (for TFT) based on PM2.5
    inline uint16_t getPM25Color(uint16_t pm25) {
        // RGB565 encoding: (R << 11) | (G << 5) | B
        if (pm25 <= 12)  return 0x07E0;  // Green #22c55e
        if (pm25 <= 35)  return 0xFBC0;  // Amber #f59e0b
        return 0xF800;                   // Red #ef4444
    }

    // Uptime formatter (seconds → "Xh Ym")
    inline void formatUptime(uint32_t seconds, char* buf, size_t len) {
        uint32_t hours = seconds / 3600;
        uint32_t minutes = (seconds % 3600) / 60;
        snprintf(buf, len, "%luh %um", hours, minutes);
    }

    // FRAM usage as percentage
    inline uint8_t getFramUsagePercent(uint32_t used, uint32_t capacity) {
        if (capacity == 0) return 0;
        return (used * 100) / capacity;
    }
}

// =============================================================================
// E-Ink Display Driver (RAK14000)
// 360×240px, 3-color (black/white/red), persistent display
// =============================================================================

#if defined(DISPLAY_EINK) && DISPLAY_EINK == 1

#include <Adafruit_EPD.h>

class EInkDisplay : public DisplayDriver {
public:
    bool init() override {
        Serial.println(F("[DISPLAY] Initializing E-Ink (RAK14000) 360×240..."));

        // Initialize SPI and EPD
        // Note: Adafruit_IL91874 for 3.7" e-ink with 3 colors
        _epd = new Adafruit_IL91874(
            360, 240,               // width, height
            PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_CS, PIN_DISP_BUSY
        );

        if (!_epd) {
            Serial.println(F("[DISPLAY] ERROR: Failed to allocate E-Ink display"));
            return false;
        }

        _epd->begin(THINKINK_TRICOLOR);  // Note: begin() returns void, not int
        _epd->setRotation(1);  // Landscape mode (360×240)
        _ready = true;

        Serial.println(F("[DISPLAY] E-Ink display ready"));
        return true;
    }

    void showBootScreen(const SystemHealth& health) override {
        if (!_ready) return;

        _epd->clearBuffer();
        _epd->setTextColor(EPD_BLACK);

        // === HEADER ===
        _epd->setTextSize(2);
        _epd->setCursor(60, 15);
        _epd->println(F("WISBLOCK SENSOR HUB"));

        _epd->setTextSize(1);
        _epd->setCursor(140, 35);
        _epd->printf("v%s", FW_VERSION);

        // Box around header
        _epd->drawRect(5, 5, 350, 45, EPD_BLACK);

        // === INITIALIZATION PROGRESS ===
        _epd->setTextSize(1);
        _epd->setCursor(10, 60);
        _epd->println(F("System Initialization:"));

        struct {
            const char* name;
            TaskId id;
        } tasks[] = {
            {"BME680 (Environment)     ", TaskId::SENSOR},
            {"PMSA003I (Particulate)   ", TaskId::SENSOR},
            {"GNSS (Position)          ", TaskId::SENSOR},
            {"WiFi Connection          ", TaskId::WEBSERVER},
            {"LoRaWAN OTAA Join        ", TaskId::LORA},
        };

        int y = 80;
        for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
            const auto& task = tasks[i];
            const auto& health_task = health.tasks[static_cast<int>(task.id)];

            // Task name
            _epd->setCursor(10, y);
            _epd->print(task.name);

            // Progress bar (20 chars wide: ████████████████████)
            _epd->setCursor(200, y);
            _epd->print("[");

            uint8_t filled = 0;
            bool useRed = false;
            if (health_task.state == TaskState::RUNNING) {
                filled = 20;
            } else if (health_task.state == TaskState::WARMING_UP) {
                filled = 12;
            } else if (health_task.state == TaskState::ERROR) {
                useRed = true;
            }

            if (useRed) _epd->setTextColor(EPD_RED);
            for (uint8_t j = 0; j < 20; j++) {
                _epd->print(j < filled ? "#" : "-");
            }
            if (useRed) _epd->setTextColor(EPD_BLACK);

            _epd->print("] ");

            // Status indicator
            if (health_task.state == TaskState::RUNNING) {
                _epd->print("OK");
            } else if (health_task.state == TaskState::WARMING_UP) {
                _epd->print("...");
            } else if (health_task.state == TaskState::ERROR) {
                _epd->setTextColor(EPD_RED);
                _epd->print("ERR");
                _epd->setTextColor(EPD_BLACK);
            } else {
                _epd->print("---");
            }

            y += 18;
        }

        // === FOOTER: SYSTEM STATE ===
        _epd->drawRect(5, 205, 350, 30, EPD_BLACK);
        _epd->setTextSize(1);
        _epd->setCursor(10, 215);
        _epd->print(F("Status: "));

        const char* stateStr = "UNKNOWN";
        bool stateWarning = false;
        switch (health.systemState) {
            case SystemState::READY:      stateStr = "READY"; break;
            case SystemState::WARMING_UP: stateStr = "WARMING UP"; stateWarning = true; break;
            case SystemState::DEGRADED:   stateStr = "DEGRADED"; stateWarning = true; break;
            case SystemState::ERROR:      stateStr = "ERROR"; stateWarning = true; break;
            default:                      stateStr = "BOOTING"; break;
        }

        if (stateWarning) _epd->setTextColor(EPD_RED);
        _epd->print(stateStr);
        _epd->setTextColor(EPD_BLACK);

        _epd->display();
        Serial.println(F("[DISPLAY] E-Ink boot screen rendered"));
    }

    void showDashboard(const LiveData& data, const SystemHealth& health) override {
        if (!_ready) return;

        _epd->clearBuffer();
        _epd->setTextColor(EPD_BLACK);

        // === STATUS BAR (Top) ===
        _epd->setTextSize(1);
        _epd->setCursor(5, 8);

        // WiFi status
        if (data.wifiConnected) {
            _epd->print("WiFi:");
            // Signal strength bars
            int bars = (data.wifiRssi >= -50) ? 4 : (data.wifiRssi >= -60) ? 3 : (data.wifiRssi >= -70) ? 2 : 1;
            for (int i = 0; i < bars; i++) _epd->print((char)('!' + i)); // Unicode approximation
            _epd->printf(" %ddBm", data.wifiRssi);
        } else {
            _epd->print("WiFi: --");
        }

        _epd->print("  |  ");

        // LoRa status
        _epd->print("LoRa: ");
        if (data.loraJoined) {
            _epd->print("OK");
        } else {
            _epd->setTextColor(EPD_RED);
            _epd->print("--");
            _epd->setTextColor(EPD_BLACK);
        }

        _epd->print("  |  ");

        // GNSS status
        _epd->print("GPS: ");
        if (data.gnss.fixValid) {
            _epd->printf("%u SVs", data.gnss.satellites);
        } else {
            _epd->print("--");
        }

        _epd->print("  |  ");

        // FRAM usage
        uint8_t framPct = DisplayUtils::getFramUsagePercent(data.framUsedRecords, data.framTotalCapacity);
        _epd->printf("FRAM: %u%%", framPct);

        _epd->drawLine(0, 20, 360, 20, EPD_BLACK);

        // === CARD 1: ENVIRONMENT (Large values) ===
        _epd->drawRect(5, 25, 350, 55, EPD_BLACK);
        _epd->setCursor(10, 30);
        _epd->print(F("ENVIRONMENT"));

        // Temperature (large)
        _epd->setTextSize(3);
        _epd->setCursor(10, 42);
        _epd->printf("%.1f", data.temperature / 100.0f);
        _epd->setTextSize(2);
        _epd->print("C");  // Degree symbol not available, use C

        // Humidity & Pressure (smaller, side by side)
        _epd->setTextSize(1);
        _epd->setCursor(150, 45);
        _epd->printf("Hum: %.1f%%", data.humidity / 10.0f);
        _epd->setCursor(150, 60);
        _epd->printf("Press: %uhPa", data.pressure / 10);

        // VOC quality
        _epd->setCursor(260, 45);
        _epd->print("VOC:");
        _epd->setCursor(260, 57);
        const char* aqLabel = DisplayUtils::getAirQualityLabel(data.gasResistance);
        if (data.gasResistance < 100000) _epd->setTextColor(EPD_RED);
        _epd->print(aqLabel);
        _epd->setTextColor(EPD_BLACK);

        // === CARD 2: PARTICULATE MATTER ===
        _epd->drawRect(5, 85, 170, 45, EPD_BLACK);
        _epd->setCursor(10, 90);
        _epd->print(F("PARTICULATE MATTER"));

        _epd->setTextSize(1);
        _epd->setCursor(10, 105);
        _epd->printf("PM2.5: %2u ug/m3", data.pm2_5);
        _epd->setCursor(10, 118);
        _epd->printf("PM10:  %2u ug/m3", data.pm10);

        // AQI label
        const char* aqiLabel = "Good";
        bool aqiWarning = false;
        if (data.pm2_5 > 55) { aqiLabel = "Unhealthy"; aqiWarning = true; }
        else if (data.pm2_5 > 35) { aqiLabel = "USG"; aqiWarning = true; }
        else if (data.pm2_5 > 12) { aqiLabel = "Moderate"; }

        _epd->setCursor(115, 110);
        if (aqiWarning) _epd->setTextColor(EPD_RED);
        _epd->printf("[%s]", aqiLabel);
        _epd->setTextColor(EPD_BLACK);

        // === CARD 3: LIGHT ===
        _epd->drawRect(180, 85, 175, 45, EPD_BLACK);
        _epd->setCursor(185, 90);
        _epd->print(F("LIGHT"));

        _epd->setTextSize(2);
        _epd->setCursor(185, 105);
        _epd->printf("%u", data.lux / 10);
        _epd->setTextSize(1);
        _epd->print(" lux");

        _epd->setCursor(185, 120);
        float luxVal = data.lux / 10.0f;
        const char* lightLabel = (luxVal < 50) ? "Dim" : (luxVal < 500) ? "Indoor" : (luxVal < 1000) ? "Bright" : "Daylight";
        _epd->print(lightLabel);

        // === CARD 4: GNSS LOCATION ===
        _epd->drawRect(5, 135, 350, 50, EPD_BLACK);
        _epd->setCursor(10, 140);
        _epd->print(F("GNSS POSITION"));

        if (data.gnss.fixValid) {
            float lat = data.gnss.latitude / 1e7f;
            float lon = data.gnss.longitude / 1e7f;

            _epd->setTextSize(1);
            _epd->setCursor(10, 155);
            _epd->printf("Lat: %8.4f", lat);
            _epd->setCursor(140, 155);
            _epd->printf("Lon: %8.4f", lon);

            _epd->setCursor(10, 170);
            _epd->printf("Alt: %dm", (int)(data.gnss.altitude / 1000));
            _epd->setCursor(140, 170);
            _epd->printf("HDOP: %.1f", data.gnss.hdop / 100.0f);

            // TODO: Add geocoded location when available
            // _epd->setCursor(10, 180);
            // _epd->printf("Location: %s", geocodedCity);
        } else {
            _epd->setTextSize(1);
            _epd->setCursor(10, 160);
            _epd->setTextColor(EPD_RED);
            _epd->print("Searching for satellites...");
            _epd->setTextColor(EPD_BLACK);
        }

        // === CARD 5: SYSTEM STATUS ===
        _epd->drawRect(5, 190, 350, 45, EPD_BLACK);
        _epd->setCursor(10, 195);
        _epd->setTextSize(1);
        _epd->print(F("SYSTEM"));

        _epd->setCursor(10, 210);
        _epd->printf("FRAM: %u/%u (%u%%)",
                    data.framUsedRecords, data.framTotalCapacity, framPct);

        _epd->setCursor(210, 210);
        _epd->printf("Batt: %u.%02uV", data.batteryMv / 1000, (data.batteryMv % 1000) / 10);

        _epd->setCursor(10, 223);
        char uptimeBuf[32];
        DisplayUtils::formatUptime((millis() - health.bootTime) / 1000, uptimeBuf, sizeof(uptimeBuf));
        _epd->printf("Uptime: %s", uptimeBuf);

        _epd->setCursor(210, 223);
        if (data.loraJoined && data.loraLastTxTime > 0) {
            uint32_t secAgo = ((millis() / 1000) - data.loraLastTxTime);
            _epd->printf("TX: %lum ago", secAgo / 60);
        } else {
            _epd->print("TX: --");
        }

        _epd->display();
        Serial.println(F("[DISPLAY] E-Ink dashboard rendered"));
    }

    void showError(const char* message) override {
        if (!_ready) return;

        _epd->clearBuffer();
        _epd->setTextColor(EPD_RED);
        _epd->setTextSize(2);

        // Red banner
        _epd->drawRect(0, 0, 360, 50, EPD_RED);
        _epd->fillRect(0, 0, 360, 50, EPD_RED);
        _epd->setTextColor(EPD_BLACK);
        _epd->setCursor(20, 25);
        _epd->println(F("ERROR"));

        _epd->setTextColor(EPD_RED);
        _epd->setTextSize(1);
        _epd->setCursor(20, 80);
        _epd->println(message);

        _epd->display();
        Serial.printf("[DISPLAY] E-Ink error: %s\n", message);
    }

    bool isPersistent() const override { return true; }
    uint32_t minRefreshMs() const override { return 300000; }  // 5 minutes minimum

private:
    Adafruit_IL91874* _epd = nullptr;
    bool _ready = false;
};

#endif  // DISPLAY_EINK

// =============================================================================
// TFT Display Driver (RAK14014)
// 240×320px (portrait), 16-bit color, fast refresh
// =============================================================================

#if defined(DISPLAY_TFT) && DISPLAY_TFT == 1

#include <Adafruit_ILI9341.h>
#include <Adafruit_FT6206.h>

class TftDisplay : public DisplayDriver {
public:
    bool init() override {
        Serial.println(F("[DISPLAY] Initializing TFT LCD (RAK14014) 240×320..."));

        // Initialize ILI9341 SPI display
        _tft = new Adafruit_ILI9341(PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST);

        if (!_tft) {
            Serial.println(F("[DISPLAY] ERROR: Failed to allocate TFT display"));
            return false;
        }

        _tft->begin();
        _tft->setRotation(0);  // Portrait mode
        _tft->fillScreen(ILI9341_BLACK);

        // Initialize touch controller
        _touch = new Adafruit_FT6206();
        if (!_touch->begin(40)) {
            Serial.println(F("[DISPLAY] WARNING: Touch controller failed"));
            // Not fatal — display still works without touch
        }

        // Set backlight on
        pinMode(PIN_DISP_BL, OUTPUT);
        digitalWrite(PIN_DISP_BL, HIGH);

        _ready = true;
        Serial.println(F("[DISPLAY] TFT display ready"));
        return true;
    }

    void showBootScreen(const SystemHealth& health) override {
        if (!_ready) return;

        // Define professional color palette (RGB565)
        static const uint16_t BG_PRIMARY = 0x0841;     // Dark navy #0a0e1a
        static const uint16_t BG_CARD = 0x1082;        // Card bg #111827
        static const uint16_t BORDER = 0x1CA4;         // Border #1e293b
        static const uint16_t TEXT_PRIMARY = 0xF7BE;   // Off-white #f1f5f9
        static const uint16_t TEXT_MUTED = 0x94B2;     // Muted #94a3b8
        static const uint16_t COLOR_GREEN = 0x2589;    // Green #22c55e
        static const uint16_t COLOR_AMBER = 0xFCC0;    // Amber #f59e0b
        static const uint16_t COLOR_RED = 0xF800;      // Red #ef4444
        static const uint16_t COLOR_BLUE = 0x3C1F;     // Blue #3b82f6

        _tft->fillScreen(BG_PRIMARY);

        // === HEADER WITH GRADIENT EFFECT ===
        _tft->fillRect(0, 0, 240, 50, BG_CARD);
        _tft->drawRect(0, 0, 240, 50, BORDER);

        _tft->setTextColor(TEXT_PRIMARY);
        _tft->setTextSize(2);
        _tft->setCursor(20, 10);
        _tft->print(F("WISBLOCK"));
        _tft->setCursor(15, 27);
        _tft->print(F("SENSOR HUB"));

        _tft->setTextSize(1);
        _tft->setTextColor(TEXT_MUTED);
        _tft->setCursor(165, 37);
        _tft->printf("v%s", FW_VERSION);

        // === INITIALIZATION PROGRESS ===
        _tft->setTextColor(TEXT_MUTED);
        _tft->setTextSize(1);
        _tft->setCursor(10, 60);
        _tft->print(F("System Initialization:"));

        struct {
            const char* name;
            TaskId id;
        } tasks[] = {
            {"BME680 Environment ", TaskId::SENSOR},
            {"PMSA003I Particul.", TaskId::SENSOR},
            {"ZOE-M8Q GNSS      ", TaskId::SENSOR},
            {"WiFi Connection  ", TaskId::WEBSERVER},
            {"LoRaWAN OTAA Join", TaskId::LORA},
        };

        int y = 80;
        for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
            const auto& task = tasks[i];
            const auto& health_task = health.tasks[static_cast<int>(task.id)];

            // Status indicator (colored dot)
            uint16_t dotColor = COLOR_RED;
            uint16_t barColor = COLOR_RED;
            uint16_t barWidth = 0;
            const char* statusText = "---";

            if (health_task.state == TaskState::RUNNING) {
                dotColor = COLOR_GREEN;
                barColor = COLOR_GREEN;
                barWidth = 110;
                statusText = "OK";
            } else if (health_task.state == TaskState::WARMING_UP) {
                dotColor = COLOR_AMBER;
                barColor = COLOR_AMBER;
                barWidth = 60;
                statusText = "...";
            }

            // Colored status dot
            _tft->fillCircle(12, y, 4, dotColor);

            // Task name
            _tft->setTextColor(TEXT_PRIMARY);
            _tft->setCursor(25, y - 4);
            _tft->print(task.name);

            // Progress bar background
            _tft->drawRect(145, y - 6, 70, 10, BORDER);
            _tft->fillRect(146, y - 5, 68, 8, BG_PRIMARY);

            // Progress bar fill
            if (barWidth > 0) {
                uint16_t scaledWidth = (barWidth * 68) / 110;  // Scale to fit
                _tft->fillRect(146, y - 5, scaledWidth, 8, barColor);
            }

            // Status text
            _tft->setTextColor(dotColor);
            _tft->setCursor(220, y - 4);
            _tft->print(statusText);

            y += 35;
            if (health_task.state == TaskState::RUNNING) {
                barWidth = 120;
            } else if (health_task.state == TaskState::WARMING_UP) {
                barWidth = 60;
            }
            _tft->drawRect(120, y - 12, 110, 8, 0x4a52);
            if (barWidth > 0) {
                _tft->fillRect(120, y - 12, barWidth, 8, dotColor);
            }

            y += 35;
        }

        // === FOOTER: SYSTEM STATE ===
        _tft->fillRect(0, 280, 240, 40, BG_CARD);
        _tft->drawRect(0, 280, 240, 40, BORDER);

        _tft->setTextColor(TEXT_MUTED);
        _tft->setTextSize(1);
        _tft->setCursor(10, 290);
        _tft->print(F("Status:"));

        const char* stateStr = "UNKNOWN";
        uint16_t stateColor = TEXT_PRIMARY;
        switch (health.systemState) {
            case SystemState::READY:
                stateStr = "READY";
                stateColor = COLOR_GREEN;
                break;
            case SystemState::WARMING_UP:
                stateStr = "WARMING UP";
                stateColor = COLOR_AMBER;
                break;
            case SystemState::DEGRADED:
                stateStr = "DEGRADED";
                stateColor = COLOR_AMBER;
                break;
            case SystemState::ERROR:
                stateStr = "ERROR";
                stateColor = COLOR_RED;
                break;
            default:
                stateStr = "BOOTING";
                break;
        }

        _tft->setCursor(55, 290);
        _tft->setTextColor(stateColor);
        _tft->setTextSize(2);
        _tft->print(stateStr);

        // Uptime
        _tft->setTextSize(1);
        _tft->setTextColor(TEXT_MUTED);
        _tft->setCursor(10, 305);
        char uptimeBuf[32];
        DisplayUtils::formatUptime((millis() - health.bootTime) / 1000, uptimeBuf, sizeof(uptimeBuf));
        _tft->printf("Uptime: %s", uptimeBuf);

        Serial.println(F("[DISPLAY] TFT boot screen rendered"));
    }

    void showDashboard(const LiveData& data, const SystemHealth& health) override {
        if (!_ready) return;

        _tft->fillScreen(0x0a0e);  // Dark navy

        // Define colors
        static const uint16_t COLOR_TEXT = 0xf1f5;
        static const uint16_t COLOR_LABEL = 0x94a3;
        static const uint16_t COLOR_BORDER = 0x1e29;

        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(2);

        // Header
        _tft->setCursor(10, 10);
        _tft->print(F("SENSOR HUB"));
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(140, 12);
        _tft->printf("Up: %luh", (millis() - health.bootTime) / 3600000);

        // Status indicator
        uint16_t statusColor = (health.systemState == SystemState::READY) ? 0x07e0 : 0xfbc0;
        _tft->fillCircle(225, 15, 5, statusColor);

        // Card 1: Environment (Temperature, Humidity, Pressure)
        _drawCard(10, 30, "ENVIRONMENT", COLOR_BORDER);
        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(3);
        char tempBuf[16];
        DisplayUtils::formatTemp(data.temperature / 100.0f, tempBuf, sizeof(tempBuf));
        _tft->setCursor(20, 50);
        _tft->print(tempBuf);

        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(20, 80);
        _tft->printf("Humidity: %.1f%%", data.humidity / 10.0f);
        _tft->setCursor(20, 92);
        _tft->printf("Pressure: %u hPa", data.pressure / 10);

        // VOC quality indicator
        _tft->setTextColor(COLOR_TEXT);
        _tft->setCursor(20, 104);
        _tft->printf("VOC: %s", DisplayUtils::getAirQualityLabel(data.gasResistance));

        // Card 2: Particulate Matter
        _drawCard(10, 120, "PARTICULATE", COLOR_BORDER);
        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(2);
        _tft->setCursor(20, 140);
        _tft->printf("PM2.5: %u", data.pm2_5);
        _tft->setCursor(20, 158);
        _tft->printf("PM10: %u", data.pm10);
        _tft->setTextSize(1);
        _tft->setCursor(20, 172);
        _tft->printf("µg/m³");

        // Card 3: Light
        _drawCard(10, 185, "LIGHT", COLOR_BORDER);
        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(2);
        _tft->setCursor(20, 205);
        _tft->printf("%u lux", data.lux / 10);

        // Card 4: System Status (compact)
        _drawCard(120, 120, "SYSTEM", COLOR_BORDER);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(130, 140);
        _tft->print(F("WiFi:"));
        _tft->setTextColor(COLOR_TEXT);
        _tft->printf(" %s", data.wifiConnected ? "OK" : "NO");

        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(130, 154);
        _tft->print(F("LoRa:"));
        _tft->setTextColor(COLOR_TEXT);
        _tft->printf(" %s", data.loraJoined ? "OK" : "NO");

        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(130, 168);
        _tft->print(F("FRAM:"));
        _tft->setTextColor(COLOR_TEXT);
        _tft->printf(" %u%%", DisplayUtils::getFramUsagePercent(data.framUsedRecords, data.framTotalCapacity));

        _tft->setTextColor(COLOR_LABEL);
        _tft->setCursor(130, 182);
        _tft->print(F("Bat:"));
        _tft->setTextColor(COLOR_TEXT);
        _tft->printf(" %u.%02uV", data.batteryMv / 1000, (data.batteryMv % 1000) / 10);

        // Card 5: GNSS
        _drawCard(10, 240, "GNSS", COLOR_BORDER);
        _tft->setTextSize(1);
        _tft->setTextColor(COLOR_TEXT);
        float lat = data.gnss.latitude / 1e7f;
        float lon = data.gnss.longitude / 1e7f;
        _tft->setCursor(20, 258);
        _tft->printf("%.2f°N", lat);
        _tft->setCursor(20, 270);
        _tft->printf("%.2f°W", lon);
        _tft->setCursor(20, 282);
        _tft->printf("%u satellites", data.gnss.satellites);

        Serial.println(F("[DISPLAY] TFT dashboard rendered"));
    }

    void showError(const char* message) override {
        if (!_ready) return;

        _tft->fillScreen(0xf800);  // Red
        _tft->setTextColor(0xffff);  // White text
        _tft->setTextSize(2);
        _tft->setCursor(20, 50);
        _tft->print(F("ERROR"));

        _tft->setTextSize(1);
        _tft->setCursor(10, 100);
        _tft->println(message);

        Serial.printf("[DISPLAY] TFT error: %s\n", message);
    }

    bool isPersistent() const override { return false; }
    uint32_t minRefreshMs() const override { return 2000; }  // 2 seconds

private:
    Adafruit_ILI9341* _tft = nullptr;
    Adafruit_FT6206* _touch = nullptr;
    bool _ready = false;

    // Helper: Draw a card frame with title
    void _drawCard(int16_t x, int16_t y, const char* title, uint16_t borderColor) {
        _tft->drawRect(x, y, 220, 65, borderColor);
        _tft->setTextColor(0x94a3);  // Label color
        _tft->setTextSize(1);
        _tft->setCursor(x + 5, y + 5);
        _tft->print(title);
    }
};

#endif  // DISPLAY_TFT

// =============================================================================
// Factory Function
// =============================================================================

inline DisplayDriver* createDisplay() {
    #if defined(DISPLAY_EINK) && DISPLAY_EINK == 1
        return new EInkDisplay();
    #elif defined(DISPLAY_TFT) && DISPLAY_TFT == 1
        return new TftDisplay();
    #else
        #error "No display type defined. Set DISPLAY_EINK=1 or DISPLAY_TFT=1 in build_flags"
    #endif
}
