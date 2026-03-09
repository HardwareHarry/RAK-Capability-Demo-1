# Display Design Specification

## Overview

The WisBlock Sensor Hub supports two display modes selected at compile-time:
- **E-Ink (RAK14000)**: 360×240px, 3-color (black/white/red), low power, persistent
- **TFT (RAK14014)**: 240×320px (portrait), 16-bit color (RGB565), full power, constant refresh

Both displays share a unified design language inspired by the web dashboard's industrial monitoring aesthetic.

## Design Philosophy

- **Data-dense**: Show maximum sensor information at a glance
- **Accessible**: High contrast, clear hierarchy, readable from distance
- **Responsive**: Boot screen shows real-time progress; dashboard updates live
- **Consistent**: Design elements echo the web UI's dark theme, monospace data, accent colors

---

## Boot Screen (Warm-up Phase)

### Purpose
Track system initialization and sensor warm-up progress as the device powers on.

### E-Ink Layout (360×240, landscape)

```
┌─────────────────────────────────────────────────────────┐
│  ⚙ WisBlock Sensor Hub v0.1.0                          │
│  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━   │
│                                                         │
│  BME680 Env      [■■■□□] Heater stabilising (280s)  │
│  PM2.5 Air       [■■□□□] Fan warming up (18s left)  │
│  GNSS Position   [■□□□□] Searching (4 satellites)    │
│  Light Level     [■■■■■] Ready (243 lux)             │
│  RTC Time        [■■■■■] Sync'd 2026-03-07 14:23    │
│                                                         │
│  WiFi Conn       [■■■□□] Connecting to home...        │
│  LoRaWAN Join    [□□□□□] Joining network...           │
│  Display Ready   [■■■■■] Ready                        │
│                                                         │
│  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━   │
│  System: WARMING UP  |  0:02:17 elapsed                │
└─────────────────────────────────────────────────────────┘
```

### TFT Layout (240×320, portrait)

Similar structure but with color coding:
- **Green**: Ready
- **Amber**: Warming/In Progress  
- **Red**: Failed/Error
- Animated progress bars that fill smoothly

### Elements

**Header**
- App name & version in corner
- Horizontal rule separator

**Task List**
- One row per major subsystem (8 rows)
- Format: `Name  [Progress Bar] Status message`
- Progress bar: 5-character width, filled/unfilled blocks

**Footer**
- System state label: "BOOTING", "WARMING UP", "READY"
- Elapsed time / or countdown for long operations
- Subtitle: Task count: "7/8 systems ready"

### State Display Logic

Each subsystem has a "warmup phase" tracker:
- **BME680**: Gas heater stabilization (5 minutes) — progress based on elapsed time
- **PM2.5**: Fan spin-up (30 seconds) — shows countdown timer
- **GNSS**: Waiting for first fix (max 3 minutes) — shows satellite count
- **WiFi**: Connection attempt (30 second timeout) — shows "Connecting..." state
- **LoRa**: OTAA join (varies by region) — shows join state
- **Others**: Binary ready/not-ready

---

## Dashboard (Live Monitoring Phase)

### Purpose
Comprehensive real-time sensor overview with system health status, mirroring the web UI layout.

### E-Ink Dashboard (360×240, landscape)

```
┌─────────────────────────────────────────────────────────┐
│ 23.4°C  65.5%  1013.2 hPa      🔼 LoRa TX'd (5min ago)│
│ ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━   │
│                                                         │
│ 📦 Particulate         ☀️ Light           🌍 GNSS       │
│ PM2.5: 12 µg/m³        Lux: 543           50.9°N        │
│ PM10:  18 µg/m³        Vis: Bright        1.4°W         │
│ PM1.0: 8  µg/m³                          7 satellites   │
│                                                         │
│ 💨 VOC Air Quality      🔋 System Health                │
│ Resistance: 42.3 kΩ     FRAM: 2.1% full               │
│ Rating: Excellent       WiFi: Connected    Uptime: 4h   │
│                        LoRa: Joined        Battery: 3.8V│
│ ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━   │
│ Last update: 23:45:32 local time                       │
└─────────────────────────────────────────────────────────┘
```

### TFT Dashboard (240×320, portrait)

Richer with color-coding and vertical stack. Can be redrawn live more frequently than E-Ink.

```
┌──────────────────────────────┐
│  ⚙ WISBLOCK HUB              │ ← Header with status dot
│  🟢 READY  •  Uptime 4h 23m  │
├──────────────────────────────┤
│  ENVIRONMENT                 │
│  23.4°C │ 65.5% │ 1013 hPa  │
│  VOC: 42.3kΩ (Excellent) ✓   │
├──────────────────────────────┤
│  PARTICULATE MATTER          │
│  PM2.5: 12  PM10: 18         │
│  PM1.0: 8   (Good)           │
├──────────────────────────────┤
│  LIGHT                       │
│  543 lux  │  Bright          │
├──────────────────────────────┤
│  LOCATION (GNSS)             │
│  50.9°N, 1.4°W │ 7 SVs      │
│  Altitude: 12m               │
├──────────────────────────────┤
│  SYSTEM                      │
│  WiFi: Connected -62dBm     │
│  LoRa: Joined, TX 5min ago  │
│  FRAM: 2.1% (712/34900)     │
│  Battery: 3.82V              │
└──────────────────────────────┘
```

### Dashboard Features

**E-Ink**
- Static, persistent display — minimal refreshes (every 5 minutes)
- Monochrome with strategic red accents for warnings
- Data remains visible even if power is briefly interrupted
- No animations, no gradients

**TFT**
- Full-color with semantic highlighting
- Can update more frequently (every 2-5 seconds)
- Animated status indicators (pulsing "connected" dot)
- Touch zones for future swipe navigation
- Gradient bars for battery, FRAM usage
- Color-coded air quality (green=good, amber=fair, red=poor)

### Section Details

#### Environment Card
- Primary: Temperature (large, bold)
- Secondary: Humidity & Pressure (side-by-side)
- Status: VOC rating with progress bar or rating label

#### Particulate Matter Card
- Three values: PM1.0, PM2.5, PM10
- EPA/AQI color coding based on PM2.5:
  - Green: 0–12 µg/m³ (Good)
  - Amber: 12–35 µg/m³ (Moderate)
  - Red: 35+ µg/m³ (Poor)

#### Light Card
- Lux value (large)
- Optional: Qualitative label (Dark, Bright, Saturated)

#### GNSS Card
- Latitude, Longitude
- Satellite count (with visual indicator)
- Optional: Altitude, fix age

#### System Health Card
- WiFi status (connected/disconnected, signal strength)
- LoRa status (joined/not-joined, last TX time)
- FRAM usage (percentage + bar chart)
- Battery voltage
- Uptime
- Last sync/update timestamp

---

## Color Palette

### E-Ink (3-color: black, white, red)
- **Black**: Primary text, icons, progress bar fills
- **White**: Background, unfilled areas
- **Red**: Error states, critical warnings, accent highlights

### TFT (16-bit RGB565)
- **Background**: #0a0e1a (deep navy)
- **Card**: #111827 (slightly lighter navy)
- **Text Primary**: #f1f5f9 (off-white)
- **Text Secondary**: #94a3b8 (slate gray)
- **Accent Green**: #22c55e (healthy/ready)
- **Accent Amber**: #f59e0b (warning/warming)
- **Accent Red**: #ef4444 (error/critical)
- **Accent Blue**: #3b82f6 (neutral info)
- **Accent Cyan**: #06b6d4 (highlight)

---

## Fonts & Typography

### E-Ink
- **Header/Title**: Monospace 12-14pt bold
- **Body**: Monospace 10pt regular
- **Data**: Monospace 16-20pt bold (for primary values)
- No anti-aliasing (bitmap fonts for clarity)

### TFT
- **Header**: Roboto/Arial 18pt bold
- **Card Title**: Roboto 12pt (all-caps)
- **Data Value**: Monospace 32pt bold
- **Data Label**: Roboto 11pt
- **Status**: Roboto 10pt regular
- High quality anti-aliased rendering

---

## Refresh Timing

### E-Ink
- Boot screen: Every 2 seconds (or event-driven on system state change)
- Dashboard: Every 5 minutes (configurable, low power priority)
- Transitions: Full redraw with slight delay before next update
- Rationale: E-Ink panels have slow refresh rates and limited endurance

### TFT
- Boot screen: Every 1 second (smooth animation feel)
- Dashboard: Every 2–5 seconds (balance between responsiveness and power)
- Transitions: Immediate, no special effects
- Rationale: TFT can handle rapid updates; should feel responsive

---

## Interaction (Future)

### TFT Touch Zones (Future Enhancement)
- Tap top status bar → Extended system info
- Tap sensor cards → Trend graph (if data available)
- Swipe left/right → Switch between overview & detail views
- Double-tap → Refresh immediately

### E-Ink Navigation (Future)
- External button on base → Cycle between screens (manual)

---

## Implementation Notes

- **Memory**: Both drivers use Adafruit_GFX abstraction for hardware independence
- **Threading**: Display task runs on Core 1, calls driver methods with snapshot data
- **Error Handling**: If any sensor is unavailable, show "–" or skip that section
- **Robustness**: Drivers should handle missing/stale data gracefully (no crashes)
- **Partial Refresh**: E-Ink driver should use partial refresh if available to reduce ghosting

---

End of specification.

