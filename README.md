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

- **FreeRTOS** multi-task design across dual cores with supervisory watchdog
- **SensorTask** (Core 0) — continuous sensor reading with warm-up state tracking
  - BME680 forced-mode cycle every 3s with gas heater stabilisation tracking
  - PMSA003I continuous fan operation with 30s warm-up period
  - ZOE-M8Q GNSS continuous tracking with automatic RTC time sync on first fix
  - VEML7700 instant ambient light reads
  - RV-3028 RTC for timestamps, synchronised from GNSS UTC
- **LoRaTask** (Core 0) — LoRaWAN OTAA via RadioLib with automatic backfill of missed transmissions from FRAM
- **WebServerTask** (Core 1) — async web server with REST API, SPA dashboard, and mDNS discovery
- **DisplayTask** (Core 1) — abstracted display driver (E-Ink or TFT, compile-time switch)
- **SupervisorTask** (Core 0) — heartbeat monitoring, graduated recovery, hardware watchdog feed

### Power Management

All sensors remain permanently powered (3V3_S rail always on). The PMSA003I fan motor
(~100mA), GNSS receiver (~25mA), and BME680 gas heater require continuous power for
meaningful readings. The light sensor, RTC, and FRAM draw negligible idle current
(<1uA combined), so power-cycling them would add complexity for no practical benefit.

### Thread Safety

The FRAM ring buffer uses a FreeRTOS mutex to protect all SPI access, allowing safe
concurrent reads from the web server (history API) and writes from the sensor task.

## WiFi Modes

The device operates in one of two WiFi modes, with completely separate credentials
for each to avoid any confusion or potential security issues.

### STA Mode (Station — primary, connects to existing network)

This is the normal operating mode. The device connects to your home or office WiFi
network as a client, just like a phone or laptop would. In this mode:

- The device is accessible via mDNS at `http://sensorhub.local/`
- It has full internet access (if your network provides it) for optional online services and NTP time sync
- Home Assistant on the same network can discover and poll the REST API
- All sensor data, dashboard, and configuration endpoints are available

The boot sequence tries STA mode first:

1. Check for compile-time credentials in `src/secrets.h` (development convenience)
2. Check NVS flash for previously saved credentials (set via web API)
3. If credentials exist, attempt connection with a 30-second timeout
4. If connection succeeds, start mDNS and web server on the network

### AP Mode (Access Point — fallback for setup, travel, or demo)

If STA connection fails or no credentials are available, the device creates its
own WiFi network. This is intentionally a different SSID from any network you
would connect to in STA mode — using the same name for both would be confusing
to other devices on the network and could be mistaken for a rogue access point.

In AP mode:

- The device creates network `WisBlock-SensorHub` (password: `wisblock123`)
- Connect to this network, then browse to `http://192.168.4.1/`
- The full dashboard and API are available
- Captive portal setup page is available at `/setup`
- DNS catch-all is enabled in captive mode to improve automatic portal launch
- If not configured within 30 minutes, device auto-reboots to recover cleanly
- You can configure STA credentials via `POST /api/wifi` to switch to your network
- No internet access is available (the device IS the network)

AP mode is also used as an automatic fallback:

- On first boot with no stored credentials
- When STA connection fails after 30 seconds
- When STA connection drops and 5 consecutive reconnect attempts fail
- Manually via `POST /api/wifi/ap`

### WiFi Credential Summary

| Setting        | Where Configured          | Purpose                         |
|----------------|---------------------------|---------------------------------|
| STA SSID       | `secrets.h` or web API    | Your home/office network name   |
| STA Password   | `secrets.h` or web API    | Your home/office network password|
| AP SSID        | `config.h` compile-time   | Device's own network name       |
| AP Password    | `config.h` compile-time   | Device's own network password   |

The AP credentials are separate from STA credentials by design. They identify
the device itself, not the network it connects to. Changing the AP credentials
requires a firmware rebuild (they are compile-time constants in `config.h`).

### Auto-Reconnect Behaviour

When operating in STA mode, the WiFi manager monitors the connection and handles
drops gracefully:

- Checks connection status every 5 seconds
- If disconnected, attempts reconnect every 15 seconds
- After 5 consecutive reconnect failures, falls back to AP mode
- The device is always reachable — either on your network or via its own AP

## Building

### Prerequisites

1. Install [PlatformIO Core (CLI)](https://docs.platformio.org/en/latest/core/installation.html)
2. Install CLion with the [PlatformIO for CLion](https://plugins.jetbrains.com/plugin/13922-platformio-for-clion) plugin
   — or use VSCode with the PlatformIO extension

### RAK3312 Board Definition

1. Download `RAK_PATCH_V2.zip` from the [RAKWireless WisBlock PlatformIO repo](https://github.com/RAKWireless/WisBlock/tree/master/PlatformIO)
2. Extract the `rakwireless/` folder from the zip
3. Copy it into the `firmware/` directory, keeping only the `rak3112` board and variant files:

```
firmware/
├── platformio.ini
├── rakwireless/
│   ├── boards/
│   │   └── rak3112.json
│   └── variants/
│       └── rak3112/
│           ├── pins_arduino.h
│           └── variant.h
└── src/
    └── ...
```

### WiFi Configuration

Copy the secrets template and fill in your WiFi credentials:

```bash
cp firmware/src/secrets.h.template firmware/src/secrets.h
```

Edit `firmware/src/secrets.h` with your network details:

```cpp
#define WIFI_STA_SSID       "YourNetworkName"
#define WIFI_STA_PASS       "YourPassword"
```

This file is gitignored and will never be committed. If no `secrets.h` exists,
the device checks NVS for previously saved credentials (set via the web API),
and falls back to AP mode if neither is available.

To change the AP mode credentials, edit `config.h`:

```cpp
#define WIFI_AP_SSID        "WisBlock-SensorHub"
#define WIFI_AP_PASSWORD    "wisblock123"
```

### Display Selection

In `firmware/platformio.ini`, uncomment the desired display flag:

```ini
build_flags =
    -DDISPLAY_EINK=1        ; RAK14000 E-Ink (default)
    ; -DDISPLAY_TFT=1       ; RAK14014 TFT LCD with touch
```

### Build and Upload

```bash
cd firmware

# Build firmware
pio run

# Upload firmware to device
pio run --target upload

# Upload web dashboard to LittleFS
pio run --target uploadfs

# Serial monitor
pio device monitor
```

### Opening in CLion

Open via **File -> Open**, select `firmware/platformio.ini`, and choose **Open as Project**.
Do not use the New Project wizard — the custom RAK3312 board is not in PlatformIO's
global registry and must be loaded from the project-local `rakwireless/` directory.

### Known Build Workarounds

The `platformio.ini` includes an explicit SPI include path workaround:

```ini
-I$PROJECT_PACKAGES_DIR/framework-arduinoespressif32/libraries/SPI/src
```

This is required because PlatformIO does not correctly expose framework-internal
libraries to external dependencies when using custom board definitions.
See comments in `platformio.ini` for details.

## Network Access

### mDNS Discovery

The device announces itself as `sensorhub.local` on the local network via mDNS.
Browse to `http://sensorhub.local/` to access the dashboard from any device
on the same network. mDNS works in both STA and AP modes.

### REST API

| Endpoint              | Method | Description                                |
|-----------------------|--------|--------------------------------------------|
| `/`                   | GET    | Web dashboard (SPA)                        |
| `/api/live`           | GET    | Current sensor readings (JSON)             |
| `/api/status`         | GET    | System health, task states, memory (JSON)  |
| `/api/history?hours=N`| GET    | Historical data from FRAM (JSON)           |
| `/api/config`         | GET    | Device configuration (JSON)                |
| `/api/wifi`           | GET    | WiFi connection status (JSON)              |
| `/api/wifi`           | POST   | Save WiFi credentials and connect (JSON)   |
| `/api/wifi/ap`        | POST   | Switch to AP mode                          |
| `/api/wifi/clear`     | POST   | Clear stored WiFi credentials              |

### Home Assistant Integration

The device integrates with Home Assistant using the built-in RESTful sensor platform.
No ESPHome installation is required — HA polls the REST API directly.
See `docs/homeassistant.yaml` for a complete configuration that creates sensor entities
for all readings with proper device classes, units, and state classes.

## LoRa Backfill Strategy

The FRAM ring buffer tracks which records have been successfully transmitted via LoRaWAN.
If connectivity is lost, records continue accumulating in FRAM (capacity: ~12 days at
30-second intervals). When connectivity resumes, the LoRa task transmits the current
reading plus up to 10 backfill records per transmission cycle, catching up on missed
data without flooding the network. The last successful send index is persisted to FRAM
so backfill state survives reboots.

## Project Structure

```
firmware/
├── platformio.ini              # Build configuration
├── partitions_custom.csv       # Flash partition table (16MB)
├── rakwireless/                # RAK3312 board definition (from RAK GitHub)
├── data/
│   └── index.html              # Web dashboard SPA (uploaded to LittleFS)
├── src/
│   ├── main.cpp                # Entry point, boot sequence, FreeRTOS tasks
│   ├── config.h                # Pin definitions, constants, data structures
│   ├── types.h                 # Shared types, task health, live data cache
│   ├── secrets.h.template      # WiFi/LoRa credential template (copy to secrets.h)
│   ├── sensors/
│   │   ├── sensor_manager.h    # Orchestrates all sensors, writes FRAM
│   │   ├── rtc.h               # RV-3028 RTC with GNSS time sync
│   │   ├── light.h             |
```
