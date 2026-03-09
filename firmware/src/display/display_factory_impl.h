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
            case TaskState::RUNNING:      return '●';  // Filled circle
            case TaskState::WARMING_UP:   return '◐';  // Half circle
            case TaskState::DEGRADED:     return '◑';  // Degraded
            case TaskState::ERROR:        return '◌';  // Empty circle
            default:                      return '○';
        }
    }

    // Get status bar fill character (0-5 filled blocks)
    inline void getProgressBar(uint8_t filled, char* bar) {
        static const char* BLOCKS[] = {"□□□□□", "■□□□□", "■■□□□", "■■■□□", "■■■■□", "■■■■■"};
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

        int ret = _epd->begin(THINKINK_TRICOLOR);
        if (ret != 0) {
            Serial.printf("[DISPLAY] ERROR: EPD begin failed, code %d\n", ret);
            return false;
        }

        _epd->setRotation(1);  // Landscape mode (360×240)
        _ready = true;

        Serial.println(F("[DISPLAY] E-Ink display ready"));
        return true;
    }

    void showBootScreen(const SystemHealth& health) override {
        if (!_ready) return;

        _epd->clearBuffer();

        // Header
        _epd->setFont(&FreeSans12pt7b);
        _epd->setTextColor(EPD_BLACK);
        _epd->setCursor(10, 25);
        _epd->print(F("WisBlock Sensor Hub v0.1.0"));

        // Horizontal line
        _epd->drawLine(10, 35, 350, 35, EPD_BLACK);

        // Task list: 8 major subsystems
        struct {
            const char* name;
            TaskId id;
            const char* detail;
        } tasks[] = {
            {"BME680 Env", TaskId::SENSOR, "Heater stabilising"},
            {"PM2.5 Air", TaskId::SENSOR, "Fan warming up"},
            {"GNSS Position", TaskId::SENSOR, "Searching"},
            {"Light Level", TaskId::SENSOR, "Ready"},
            {"RTC Time", TaskId::SENSOR, "Ready"},
            {"WiFi Conn", TaskId::WEBSERVER, "Connecting"},
            {"LoRaWAN", TaskId::LORA, "Joining"},
            {"Display", TaskId::DISPLAY_TASK, "Ready"},
        };

        _epd->setFont(&FreeMono9pt7b);
        int y = 55;
        for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
            const auto& task = tasks[i];
            const auto& health_task = health.tasks[static_cast<int>(task.id)];

            // Name
            _epd->setTextColor(EPD_BLACK);
            _epd->setCursor(10, y);
            _epd->print(task.name);

            // Progress bar
            char bar[10];
            uint8_t progress = 0;
            if (health_task.state == TaskState::RUNNING) {
                progress = 5;
            } else if (health_task.state == TaskState::WARMING_UP) {
                progress = 2;
            } else if (health_task.state == TaskState::ERROR) {
                progress = 0;
                _epd->setTextColor(EPD_RED);  // Red for errors
            }
            DisplayUtils::getProgressBar(progress, bar);
            _epd->print("  [");
            _epd->print(bar);
            _epd->print("]");

            y += 20;
        }

        // Footer: System state
        _epd->setFont(&FreeSans9pt7b);
        _epd->setTextColor(EPD_BLACK);
        const char* stateStr = (health.systemState == SystemState::READY) ? "READY" :
                               (health.systemState == SystemState::WARMING_UP) ? "WARMING UP" : "BOOTING";
        _epd->setCursor(10, 230);
        _epd->print("System: ");
        _epd->print(stateStr);

        _epd->display();
        Serial.println(F("[DISPLAY] E-Ink boot screen rendered"));
    }

    void showDashboard(const LiveData& data, const SystemHealth& health) override {
        if (!_ready) return;

        _epd->clearBuffer();
        _epd->setTextColor(EPD_BLACK);

        // Header: Environment (temperature, humidity, pressure + LoRa status)
        _epd->setFont(&FreeMono12pt7b);
        char tempBuf[16], humBuf[16], presBuf[16];
        DisplayUtils::formatTemp(data.temperature / 100.0f, tempBuf, sizeof(tempBuf));
        DisplayUtils::formatHumidity(data.humidity, humBuf, sizeof(humBuf));
        DisplayUtils::formatPressure(data.pressure, presBuf, sizeof(presBuf));

        _epd->setCursor(10, 25);
        _epd->print(tempBuf);
        _epd->print("  ");
        _epd->print(humBuf);
        _epd->print("  ");
        _epd->print(presBuf);

        // LoRa status (right side)
        if (data.loraJoined) {
            _epd->setTextColor(EPD_RED);  // Red accent
            _epd->setCursor(290, 25);
            _epd->print(F("🔼 Lora"));
        }

        // Horizontal rule
        _epd->setTextColor(EPD_BLACK);
        _epd->drawLine(10, 35, 350, 35, EPD_BLACK);

        // Three columns: Particulate | Light | GNSS
        _epd->setFont(&FreeMono9pt7b);

        // Column 1: Particulate
        _epd->setCursor(10, 60);
        _epd->print(F("Particulate"));
        _epd->setCursor(10, 75);
        _epd->printf("PM2.5:%3u  PM10:%3u", data.pm2_5, data.pm10);
        _epd->setCursor(10, 90);
        _epd->printf("PM1.0:%3u", data.pm1_0);

        // Column 2: Light
        _epd->setCursor(130, 60);
        _epd->print(F("Light"));
        _epd->setCursor(130, 75);
        _epd->printf("Lux:%u", data.lux / 10);

        // Column 3: GNSS
        _epd->setCursor(240, 60);
        _epd->print(F("GNSS"));
        char latBuf[20], lonBuf[20];
        float lat = data.gnss.latitude / 1e7f;
        float lon = data.gnss.longitude / 1e7f;
        snprintf(latBuf, sizeof(latBuf), "%.2f N", lat);
        snprintf(lonBuf, sizeof(lonBuf), "%.2f W", lon);
        _epd->setCursor(240, 75);
        _epd->print(latBuf);
        _epd->setCursor(240, 90);
        _epd->printf("%s (%u SVs)", lonBuf, data.gnss.satellites);

        // VOC/Air Quality section
        _epd->setCursor(10, 115);
        _epd->print(F("VOC Resistance:"));
        char vocBuf[24];
        snprintf(vocBuf, sizeof(vocBuf), "%lu Ohm (%s)", data.gasResistance,
                 DisplayUtils::getAirQualityLabel(data.gasResistance));
        _epd->setCursor(10, 130);
        _epd->print(vocBuf);

        // System Health section
        _epd->setCursor(200, 115);
        _epd->print(F("System"));
        _epd->setCursor(200, 130);
        _epd->printf("FRAM: %u%%", DisplayUtils::getFramUsagePercent(data.framUsedRecords, data.framTotalCapacity));
        _epd->setCursor(200, 145);
        _epd->printf("WiFi:%s", data.wifiConnected ? "OK" : "--");
        _epd->setCursor(200, 160);
        _epd->printf("LoRa:%s", data.loraJoined ? "OK" : "--");

        // Footer: Timestamp and system state
        _epd->setFont(&FreeSans9pt7b);
        _epd->drawLine(10, 170, 350, 170, EPD_BLACK);
        _epd->setCursor(10, 190);
        _epd->printf("Battery: %u.%02uV", data.batteryMv / 1000, (data.batteryMv % 1000) / 10);

        char uptimeBuf[16];
        uint32_t uptimeMs = millis() - health.bootTime;
        DisplayUtils::formatUptime(uptimeMs / 1000, uptimeBuf, sizeof(uptimeBuf));
        _epd->setCursor(200, 190);
        _epd->printf("Up: %s", uptimeBuf);

        _epd->setCursor(10, 210);
        _epd->print(F("Ready  Last update: "));
        _epd->printf("%02u:%02u:%02u",
                     (data.gnss.hour % 24), data.gnss.minute, data.gnss.second);

        _epd->display();
        Serial.println(F("[DISPLAY] E-Ink dashboard rendered"));
    }

    void showError(const char* message) override {
        if (!_ready) return;

        _epd->clearBuffer();
        _epd->setTextColor(EPD_RED);
        _epd->setFont(&FreeSans12pt7b);

        // Red banner
        _epd->fillRect(0, 0, 360, 60, EPD_RED);
        _epd->setTextColor(EPD_BLACK);
        _epd->setCursor(20, 40);
        _epd->print(F("ERROR"));

        _epd->setTextColor(EPD_RED);
        _epd->setFont(&FreeSans9pt7b);
        _epd->setCursor(20, 90);
        _epd->print(message);

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

        _tft->fillScreen(0x0a0e);  // Dark navy background

        // Define colors (RGB565)
        static const uint16_t COLOR_TEXT = 0xf1f5;     // Off-white
        static const uint16_t COLOR_GREEN = 0x07e0;    // Green
        static const uint16_t COLOR_AMBER = 0xfbc0;    // Amber
        static const uint16_t COLOR_RED = 0xf800;      // Red

        // Header
        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(2);
        _tft->setCursor(10, 10);
        _tft->print(F("WISBLOCK HUB"));
        _tft->setTextSize(1);
        _tft->setCursor(10, 30);
        _tft->print(F("Initializing..."));

        // Horizontal line
        _tft->drawLine(0, 45, 240, 45, 0x4a52);

        // Task list with animated progress
        struct {
            const char* name;
            TaskId id;
        } tasks[] = {
            {"BME680", TaskId::SENSOR},
            {"PM2.5", TaskId::SENSOR},
            {"GNSS", TaskId::SENSOR},
            {"Light", TaskId::SENSOR},
            {"RTC", TaskId::SENSOR},
            {"WiFi", TaskId::WEBSERVER},
            {"LoRa", TaskId::LORA},
            {"Display", TaskId::DISPLAY_TASK},
        };

        int y = 60;
        for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
            const auto& task = tasks[i];
            const auto& health_task = health.tasks[static_cast<int>(task.id)];

            // Status indicator (colored dot)
            uint16_t dotColor = COLOR_RED;
            if (health_task.state == TaskState::RUNNING) {
                dotColor = COLOR_GREEN;
            } else if (health_task.state == TaskState::WARMING_UP) {
                dotColor = COLOR_AMBER;
            }
            _tft->fillCircle(15, y - 5, 4, dotColor);

            // Task name
            _tft->setTextColor(COLOR_TEXT);
            _tft->setTextSize(1);
            _tft->setCursor(30, y - 8);
            _tft->print(task.name);

            // Progress bar (simple filled rectangle)
            uint16_t barWidth = 0;
            if (health_task.state == TaskState::RUNNING) {
                barWidth = 120;
            } else if (health_task.state == TaskState::WARMING_UP) {
                barWidth = 60;
            }
            _tft->drawRect(120, y - 12, 110, 8, 0x4a52);
            if (barWidth > 0) {
                _tft->fillRect(120, y - 12, barWidth, 8, dotColor);
            }

            y += 30;
        }

        // Footer with system state
        _tft->drawLine(0, 290, 240, 290, 0x4a52);
        _tft->setTextColor(COLOR_TEXT);
        _tft->setTextSize(1);
        _tft->setCursor(10, 300);
        const char* stateStr = (health.systemState == SystemState::READY) ? "READY" :
                               (health.systemState == SystemState::WARMING_UP) ? "WARMING UP" : "BOOTING";
        _tft->printf("System: %s", stateStr);

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

