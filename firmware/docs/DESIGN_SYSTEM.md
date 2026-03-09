# WisBlock Sensor Hub - Unified Design System

**Date**: March 9, 2026  
**Goal**: Consistent, polished, professional aesthetic across Web UI, E-Ink, and TFT displays

---

## Design Philosophy

**Industrial Monitoring Aesthetic**
- Mission control / SCADA interface inspiration
- Data-dense but organized
- Dark theme with electric accents
- Precision and clarity prioritized
- Professional typography

**Core Principles**
1. **Hierarchy** - Clear visual importance (primary values → secondary → tertiary)
2. **Semantic Color** - Green (ok), Amber (warning), Red (error), Blue (info)
3. **Consistency** - Same card layout, same metric groupings across all platforms
4. **Accessibility** - High contrast, legible fonts, clear indicators

---

## Color Palette

### Base Colors (Web UI / TFT)
```
Background Primary:  #0a0e1a  (deep navy)
Background Card:     #111827  (dark slate)
Background Inset:    #0d1220  (darker navy)
Border:              #1e293b  (slate)
Border Accent:       #334155  (lighter slate)
```

### Text Hierarchy
```
Primary:    #f1f5f9  (near white)
Secondary:  #94a3b8  (slate gray)
Muted:      #64748b  (muted slate)
Value:      #e2e8f0  (bright white)
```

### Semantic Colors
```
Green:      #22c55e  (success, healthy)
Green Dim:  #166534  (border/background)
Green Glow: rgba(34, 197, 94, 0.15)

Amber:      #f59e0b  (warning, caution)
Amber Dim:  #92400e
Amber Glow: rgba(245, 158, 11, 0.15)

Red:        #ef4444  (error, critical)
Red Dim:    #991b1b
Red Glow:   rgba(239, 68, 68, 0.15)

Blue:       #3b82f6  (info, data)
Blue Dim:   #1e3a5f
Blue Glow:  rgba(59, 130, 246, 0.12)

Cyan:       #06b6d4  (location, GPS)
Purple:     #a855f7  (system, hardware)
```

### E-Ink Adaptation (3-color: Black, White, Red)
```
Background:  White
Foreground:  Black
Accent:      Red (used sparingly for warnings/errors)

Color Mapping:
- Green/Blue/Cyan → Black (normal text)
- Amber/Red → Red (warnings/errors)
- Backgrounds → White with black borders
- Glows → Removed (not possible on E-Ink)
```

### TFT Adaptation (16-bit color: RGB565)
```
Use full color palette with slight adjustments:
- Gradients simplified (performance)
- Glow effects reduced opacity
- Maintain semantic meaning
```

---

## Typography

### Web UI
**Primary Font**: Outfit (sans-serif)
- Weights: 300, 400, 500, 600, 700
- Usage: Headers, labels, body text

**Data Font**: JetBrains Mono (monospace)
- Weights: 300, 400, 500, 600
- Usage: Numeric values, coordinates, technical data

### E-Ink Display
**Font**: Built-in Adafruit GFX fonts
- Small: 6pt (labels, units)
- Medium: 9pt (normal text)
- Large: 12pt (primary values)
- Extra Large: 18pt/24pt (hero numbers)

**Mapping**:
- Outfit → FreeSans (or default sans)
- JetBrains Mono → FreeMono (monospaced)

### TFT Display
**Font**: Adafruit GFX with custom fonts if space permits
- Use same size hierarchy as E-Ink
- Consider loading TrueType fonts for polish

---

## Component Library

### 1. Status Pill (System State)
**Web UI**:
- Rounded rectangle with border
- Pulsing dot indicator
- Text label
- Variants: ok (green), warning (amber), error (red)

**E-Ink**:
```
┌─────────────────┐
│ ● READY         │  (● is filled circle, black or red)
└─────────────────┘
```

**TFT**:
- Same as Web UI with color fill
- Static dot (no pulse animation)

---

### 2. Card Layout

**Web UI**:
```
┌──────────────────────────────────────┐
│ ICON CARD TITLE              BADGE   │
├──────────────────────────────────────┤
│ PRIMARY VALUE 23.7 °C                │
│                                      │
│ Label 1: Value 1    Label 2: Value 2│
│ Label 3: Value 3    Label 4: Value 4│
├──────────────────────────────────────┤
│ Footer info          Last updated    │
└──────────────────────────────────────┘
```

**E-Ink** (simplified):
```
┌────────────────────────────┐
│ CARD TITLE          ✓/⚠    │
├────────────────────────────┤
│       23.7 C               │
│                            │
│ Hum: 64.9%  Press: 1012hPa│
└────────────────────────────┘
```

**TFT** (color-coded):
```
┌────────────────────────────┐
│ ■ CARD TITLE        [OK]   │ (■ colored square)
├────────────────────────────┤
│       23.7 °C              │
│                            │
│ Hum  64.9%  |  Press 1012  │
└────────────────────────────┘
```

---

### 3. Status Bar (Connectivity)

**Web UI**:
- Horizontal row of status items
- Each item: icon + label + value
- Color-coded borders and backgrounds

**E-Ink**:
```
WiFi: ▂▄▆█ -61dBm  |  LoRa: ✓  |  GPS: 8 SVs  |  FRAM: 2%
```

**TFT**:
- Same as Web UI but compact
- Icons with color fills
- Values in monospace

---

### 4. Metric Display Patterns

#### Primary Value (Large)
**Web UI**: `36px JetBrains Mono, weight 600`
**E-Ink**: `24pt, bold`
**TFT**: `18pt, bold with color`

#### Secondary Value (Medium)
**Web UI**: `20px JetBrains Mono, weight 500`
**E-Ink**: `12pt`
**TFT**: `12pt with color`

#### Label (Small)
**Web UI**: `11px Outfit, uppercase, letter-spacing 0.8px`
**E-Ink**: `6pt, uppercase`
**TFT**: `6pt, color-coded`

---

### 5. Progress Bars

**Web UI**:
```css
Height: 6px
Border-radius: 3px
Border: 1px solid
Fill: Animated transition, color-coded (green/amber/red)
```

**E-Ink**:
```
[████████░░░░░░░░░░░░] 40%
(ASCII art using █ and ░ characters)
```

**TFT**:
```
Same as Web UI but with solid color fills
```

---

### 6. Badges & Pills

**Web UI**:
```
Font: 10-12px JetBrains Mono
Padding: 2-6px 8-12px
Border-radius: 10-20px
Border: 1px solid (semantic color dim)
Background: Glow color
```

**E-Ink**:
```
[OK]  [WARN]  [ERR]
(Bordered rectangles, red for warnings/errors)
```

**TFT**:
```
Filled rectangles with rounded corners
Color-coded background
White or black text
```

---

## Screen Layouts

### Web UI Dashboard (Desktop)

**Header** (sticky, 60px):
- Logo + Title + Version (left)
- System status pill (center-right)
- Uptime (right)

**Status Bar** (horizontal scroll, 50px):
- WiFi | LoRa | GNSS | FRAM | Sensors

**Main Grid** (3 columns, responsive):
1. Environment (temp, humidity, pressure, VOC)
2. Particulate Matter (PM1.0, PM2.5, PM10, AQI)
3. Light (lux, sparkline)
4. GNSS (lat, lon, satellites, HDOP)
5. System (FRAM, heap, battery, LoRa backlog)

---

### E-Ink Display (360×240, 3-color)

#### Boot Screen
```
┌─────────────────────────────────────────────────┐
│          WISBLOCK SENSOR HUB v0.1.0             │
│                                                 │
│ System Initialization:                         │
│                                                 │
│ BME680        [████████████░░░░░░] Stabilizing │
│ PMSA003I      [████████████████████] Ready ✓   │
│ GNSS          [████░░░░░░░░░░░░░░░] Searching  │
│ WiFi          [████████████████████] OK ✓      │
│ LoRaWAN       [████████████████████] Joined ✓  │
│                                                 │
│ Status: WARMING UP                              │
└─────────────────────────────────────────────────┘
```

#### Dashboard Screen
```
┌─────────────────────────────────────────────────┐
│ 23.7°C   64.9%   1012hPa        LoRa: ✓  WiFi:✓│
├─────────────────────────────────────────────────┤
│ PM2.5: 11 µg/m³   PM10: 18     [  GOOD  ]      │
│ Light: 542 lux    VOC: 218 kΩ  (Good)          │
├─────────────────────────────────────────────────┤
│ GNSS: 52.2297°N, 21.0122°E     8 SVs  HDOP 1.1│
│ Location: Warsaw, Poland                        │
├─────────────────────────────────────────────────┤
│ FRAM: 712/34900 (2%)   Battery: 3.82V          │
│ Uptime: 4h 23m         Last TX: 4m ago          │
└─────────────────────────────────────────────────┘
```

---

### TFT Display (240×320, 16-bit color)

#### Boot Screen (Color-Coded)
```
┌───────────────────────────┐
│   WISBLOCK SENSOR HUB     │ (gradient header)
│        v0.1.0             │
│                           │
│ ● BME680       [75%]  ⌛  │ (amber dot + bar)
│ ● PMSA003I    [100%]  ✓   │ (green dot + bar)
│ ● GNSS         [30%]  ⌛  │ (amber dot + bar)
│ ● WiFi        [100%]  ✓   │ (green dot + bar)
│ ● LoRaWAN     [100%]  ✓   │ (green dot + bar)
│                           │
│ Status: WARMING UP        │
│ Please wait...            │
└───────────────────────────┘
```

#### Dashboard Screen (Card-Based)
```
┌───────────────────────────┐
│ WiFi ▂▄▆█  LoRa✓  GPS 8  │ (status bar, colored)
├───────────────────────────┤
│ ■ ENVIRONMENT      [OK]   │ (blue card)
│      23.7 °C              │
│  Hum 64.9%  | Press 1012  │
├───────────────────────────┤
│ ■ AIR QUALITY     [GOOD]  │ (green card)
│  PM2.5: 11  PM10: 18      │
│  VOC: Good (218 kΩ)       │
├───────────────────────────┤
│ ■ LOCATION          [8]   │ (cyan card)
│  Warsaw, Poland           │
│  52.23°N, 21.01°E         │
├───────────────────────────┤
│ ■ SYSTEM           [2%]   │ (purple card)
│  FRAM 2%  Batt 3.82V      │
│  Uptime: 4h 23m           │
└───────────────────────────┘
```

---

## Animation & Interaction

### Web UI
- ✅ Pulsing status dots (2s cycle)
- ✅ Card hover effects (border highlight)
- ✅ Smooth value transitions (0.3-0.5s)
- ✅ Sparkline bar animations
- ✅ Progress bar fill animations
- ✅ Loading shimmer effects

### E-Ink
- ❌ No animations (refresh rate too slow)
- ✅ Static indicators (✓, ⚠, ⚠)
- ✅ Progress bars update on refresh
- ✅ 5-minute refresh cycle (persistent display)

### TFT
- ✅ Static colored indicators (no pulse)
- ✅ Instant value updates
- ✅ 2-second refresh cycle
- ✅ Color transitions on state changes
- ❌ No complex animations (performance)

---

## Responsive Breakpoints

### Web UI
- Desktop: 3-column grid (min 320px per card)
- Tablet: 2-column grid (768px breakpoint)
- Mobile: 1-column stack (< 768px)

### E-Ink
- Fixed 360×240 landscape
- No responsive needed

### TFT
- Fixed 240×320 portrait
- No responsive needed

---

## Icon System

### Web UI
- SVG icons (16-24px)
- Stroke width: 2px
- Rounded line caps

### E-Ink
- Unicode symbols: ✓ ⚠ ● ◐ ▂▄▆█
- Adafruit GFX shapes (circles, rectangles)

### TFT
- Filled geometric shapes (squares, circles)
- Color-coded (semantic meaning)
- 12-16px size

---

## Data Formatting Rules

### Temperature
- Web: `23.7 °C` (1 decimal, with degree symbol)
- E-Ink: `23.7°C` (compact, 1 decimal)
- TFT: `23.7 °C` (same as web)

### Humidity
- Web: `64.9 %` (1 decimal, with space)
- E-Ink: `64.9%` (compact)
- TFT: `64.9%` (compact)

### Pressure
- Web: `1012.8 hPa` (1 decimal)
- E-Ink: `1012 hPa` (integer, space constraint)
- TFT: `1012 hPa` (integer)

### Coordinates
- Web: `52.2297000` (7 decimals)
- E-Ink: `52.2297` (4 decimals, compact)
- TFT: `52.23°N` (2 decimals with cardinal)

### Time
- Web: `14:32:45` or `4h 23m` (depending on context)
- E-Ink: `14:32` or `4h23m` (compact)
- TFT: `14:32` or `4h23m` (compact)

---

## Implementation Checklist

### Phase 1: E-Ink Polish (Current Priority)
- [ ] Update boot screen with progress bars (ASCII art)
- [ ] Update dashboard with card-style layout
- [ ] Add status indicators (✓, ⚠, ●)
- [ ] Improve typography hierarchy (bold for values)
- [ ] Add borders around card sections
- [ ] Optimize text positioning for balance

### Phase 2: TFT Color Enhancement
- [ ] Implement colored card headers
- [ ] Add semantic color coding (green/amber/red)
- [ ] Improve status bar with icons
- [ ] Add colored progress bars
- [ ] Polish typography (size/weight)
- [ ] Add card backgrounds with color fills

### Phase 3: Web UI Refinement
- [ ] Ensure mock matches actual implementation
- [ ] Add missing sparkline functionality
- [ ] Implement AQI visualization
- [ ] Add resource bar animations
- [ ] Polish responsive breakpoints
- [ ] Add loading states

---

## File Organization

```
firmware/
├── data/
│   └── index.html                    # Web UI (production)
├── docs/
│   ├── original-web-ui-mock.html     # Reference design
│   ├── DESIGN_SYSTEM.md              # This file
│   └── DISPLAY_DESIGN.md             # Display-specific notes
└── src/
    └── display/
        └── display_factory.h         # E-Ink + TFT implementations
```

---

## Design Principles Summary

1. **Consistency**: Same card structure, same metrics, same semantic colors
2. **Adaptation**: Each platform gets optimized layout (not 1:1 copy)
3. **Hierarchy**: Primary values stand out, secondary data organized clearly
4. **Polish**: Every detail considered (spacing, alignment, typography)
5. **Professionalism**: Industrial aesthetic maintained across all interfaces

---

**Next Steps**: Update `display_factory.h` with polished E-Ink and TFT implementations following this design system.

