# WisBlock Sensor Hub

A modular IoT sensor platform built on the RAK WisBlock ecosystem, demonstrating
the full flexibility of the system with multi-sensor data collection, a local web
dashboard, LoRaWAN telemetry with backfill, and FRAM-based historical storage.

## Hardware

| Position   | Module    | Function                                    |
|------------|-----------|---------------------------------------------|
| Base       | RAK19011  | Dual IO Base Board with Power Slot          |
| Power      | RAK19012  | USB-C, LiPo battery, solar panel            |
| Core       | RAK3312   | ESP32-S3 + SX1262 (WiFi / BLE / LoRa)      |
| Slot A     | RAK1906   | BME680 — temp, humidity, pressure, VOC      |
| Slot B     | RAK12010  | VEML7700 — ambient light (lux)              |
| Slot C     | RAK12002  | RV-3028 — RTC with supercap backup          |
| Slot D     | RAK12500  | ZOE-M8Q — GNSS (GPS/GLONASS/BeiDou)        |
| Slot E     | RAK12039  | PMSA003I — particulate matter (PM2.5/PM10)  |
| Slot F     | RAK15007  | 1MB FRAM — circular buffer storage          |
| IO Slot 1  | RAK14000 *or* RAK14014 | E-Ink *or* TFT display        |
| IO Slot 2  | RAK13003  | MCP23017 — 16-ch IO expander                |

## Software Architecture

- **FreeRTOS** multi-task design with supervisory watchdog
- **SensorTask** — continuous sensor reading with warm-up state tracking
- **LoRaTask** — LoRaWAN OTAA with automatic backfill of missed transmissions
- **WebServerTask** — async web server with REST API and SPA frontend
- **DisplayTask** — abstracted display driver (E-Ink or TFT, compile-time switch)
- **SupervisorTask** — health monitoring, heartbeat validation, graduated recovery

## Building

### Prerequisites

1. Install [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html)
2. Install CLion with the [PlatformIO for CLion](https://plugins.jetbrains.com/plugin/13922-platformio-for-clion) plugin
   — or use VSCode with the PlatformIO extension

### RAK3312 Board Definition

Download the `rakwireless` folder from the
[RAK3312 Quick Start Guide](https://docs.rakwireless.com/product-categories/wisblock/rak3312/quickstart/)
GitHub repository and place it in the project root so the structure is:

```
wisblock-sensor-hub/
├── rakwireless/
│   ├── boards/
│   │   └── rak3112.json
│   └── variants/
│       └── rak3112/
│           ├── pins_arduino.h
│           └── variant.h
├── platformio.ini
└── src/
    └── ...
```

### Display Selection

In `platformio.ini`, uncomment the desired display flag:

```ini
build_flags =
    -DDISPLAY_EINK=1        ; RAK14000 E-Ink (default)
    ; -DDISPLAY_TFT=1       ; RAK14014 TFT LCD
```

### LoRaWAN Region

Uncomment the correct region flag for your location:

```ini
    -DCFG_eu868=1           ; Europe (default)
    ; -DCFG_us915=1         ; North America
    ; -DCFG_au915=1         ; Australia
    ; -DCFG_as923=1         ; Asia
```

### Build & Upload

```bash
# Build
pio run

# Upload firmware
pio run --target upload

# Upload web assets to LittleFS
pio run --target uploadfs

# Serial monitor
pio device monitor
```

## LoRa Backfill Strategy

The system tracks which FRAM records have been successfully transmitted via LoRaWAN.
If connectivity is lost, records continue accumulating in FRAM. When connectivity
resumes, the LoRa task transmits the current reading plus up to 10 backfill records
per transmission cycle, catching up on missed data without flooding the network.

## Project Status

- [x] Project scaffolding and build system
- [x] Configuration and pin mapping
- [x] FreeRTOS task architecture
- [x] FRAM ring buffer with backfill tracking
- [x] Display abstraction (E-Ink / TFT)
- [x] Supervisor / watchdog system
- [ ] Sensor driver implementations
- [ ] Web server and REST API
- [ ] LoRaWAN integration
- [ ] Frontend SPA (HTML/JS/CSS)
- [ ] E-Ink display rendering
- [ ] TFT display rendering
- [ ] OTA firmware update

## License

MIT
