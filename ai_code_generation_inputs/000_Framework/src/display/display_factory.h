// =============================================================================
// display/display_factory.h — Display driver factory and implementations
// Compile-time selection via DISPLAY_EINK or DISPLAY_TFT build flags
// =============================================================================
#pragma once

#include "../types.h"
#include "../config.h"

// =============================================================================
// E-Ink Display Driver (RAK14000)
// =============================================================================

#if defined(DISPLAY_EINK) && DISPLAY_EINK == 1

#include <Adafruit_GFX.h>
#include <Adafruit_EPD.h>

class EInkDisplay : public DisplayDriver {
public:
    bool init() override {
        // TODO: Initialise Adafruit_IL91874 or appropriate EPD driver
        //       with PIN_DISP_CS, PIN_DISP_DC, PIN_DISP_RST, PIN_DISP_BUSY
        Serial.println(F("[DISPLAY] E-Ink (RAK14000) initialised"));
        _ready = true;
        return true;
    }

    void showBootScreen(const SystemHealth& health) override {
        if (!_ready) return;
        // E-Ink boot screen: black/white text with red highlights for errors
        //
        // Layout (360×240, landscape):
        // ┌──────────────────────────────────────┐
        // │  WisBlock Sensor Hub v0.1.0           │
        // │  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  │
        // │  BME680      [■■■□□] Heater stab...   │
        // │  PM2.5       [■■□□□] Fan warming (18s) │
        // │  GNSS        [■□□□□] Searching (4 SVs) │
        // │  Light       [■■■■■] Ready (243 lx)    │
        // │  RTC         [■■■■■] 2026-03-07 14:23  │
        // │  WiFi        [■■■□□] Connecting...     │
        // │  LoRaWAN     [□□□□□] Joining...        │
        // │  Display     [■■■■■] Ready             │
        // └──────────────────────────────────────┘

        // TODO: Implement with Adafruit GFX primitives
        //       Use partial refresh if supported to avoid full redraw flicker

        Serial.println(F("[DISPLAY] E-Ink boot screen rendered"));
    }

    void showDashboard(const LiveData& data, const SystemHealth& health) override {
        if (!_ready) return;
        // E-Ink dashboard: current values + mini sparklines
        //
        // Layout:
        // ┌──────────────────────────────────────┐
        // │ 23.4°C  65.5%  1013.2 hPa   ▲ LoRa  │
        // │ ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  │
        // │ PM2.5: 12 µg/m³   PM10: 18 µg/m³     │
        // │ Light: 543 lux    VOC: 42.3 kΩ       │
        // │ GPS: 50.9°N 1.4°W  (7 SVs)           │
        // │ ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━  │
        // │ FRAM: 2.1% (712/34900)  ▲7 pending    │
        // │ Up: 4h 23m   Battery: 3.82V           │
        // └──────────────────────────────────────┘

        // TODO: Implement with Adafruit GFX
        Serial.println(F("[DISPLAY] E-Ink dashboard rendered"));
    }

    void showError(const char* message) override {
        if (!_ready) return;
        // Red banner with error text
        // TODO: Implement
        Serial.printf("[DISPLAY] E-Ink error: %s\n", message);
    }

    bool isPersistent() const override { return true; }
    uint32_t minRefreshMs() const override { return 60000; } // 1 min minimum

private:
    bool _ready = false;
    // Adafruit_IL91874* _epd = nullptr;  // TODO: actual EPD instance
};

#endif // DISPLAY_EINK

// =============================================================================
// TFT Display Driver (RAK14014)
// =============================================================================

#if defined(DISPLAY_TFT) && DISPLAY_TFT == 1

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_FT6206.h>

class TftDisplay : public DisplayDriver {
public:
    bool init() override {
        // TODO: Initialise ILI9341 with PIN_DISP_CS, PIN_DISP_DC
        //       Initialise FT6336 touch controller via I2C
        //       Set backlight PIN_DISP_BL HIGH

        Serial.println(F("[DISPLAY] TFT LCD (RAK14014) initialised"));
        _ready = true;
        return true;
    }

    void showBootScreen(const SystemHealth& health) override {
        if (!_ready) return;
        // TFT boot screen: colourful, animated progress bars
        // Can update much more frequently than E-Ink
        //
        // Similar layout to E-Ink but with:
        // - Colour-coded status (green=ready, amber=warming, red=error)
        // - Animated progress bars that fill smoothly
        // - Darker background for better contrast
        //
        // TODO: Implement with Adafruit GFX on ILI9341
        Serial.println(F("[DISPLAY] TFT boot screen rendered"));
    }

    void showDashboard(const LiveData& data, const SystemHealth& health) override {
        if (!_ready) return;
        // TFT dashboard: full-colour with real-time updates
        //
        // Since TFT can refresh quickly, this can show:
        // - Live-updating values (no full redraw needed, just value regions)
        // - Colour-coded air quality bands
        // - Battery gauge with colour gradient
        // - Touch zones for switching between views
        //
        // Touch interaction ideas:
        // - Tap temperature area → show temp trend graph
        // - Tap PM area → show particle size breakdown
        // - Swipe left/right → switch between overview and detail views
        //
        // TODO: Implement with Adafruit GFX on ILI9341
        Serial.println(F("[DISPLAY] TFT dashboard rendered"));
    }

    void showError(const char* message) override {
        if (!_ready) return;
        // Red background with white text
        // TODO: Implement
        Serial.printf("[DISPLAY] TFT error: %s\n", message);
    }

    bool isPersistent() const override { return false; }  // TFT needs constant refresh
    uint32_t minRefreshMs() const override { return 2000; } // 2s is comfortable

private:
    bool _ready = false;
    // Adafruit_ILI9341* _tft = nullptr;  // TODO: actual TFT instance
    // Adafruit_FT6206*  _touch = nullptr; // TODO: actual touch instance
};

#endif // DISPLAY_TFT

// =============================================================================
// Factory function — returns the correct display driver for the build config
// =============================================================================

inline DisplayDriver* createDisplay() {
    #if defined(DISPLAY_EINK) && DISPLAY_EINK == 1
        return new EInkDisplay();
    #elif defined(DISPLAY_TFT) && DISPLAY_TFT == 1
        return new TftDisplay();
    #else
        #error "No display type defined"
    #endif
}
