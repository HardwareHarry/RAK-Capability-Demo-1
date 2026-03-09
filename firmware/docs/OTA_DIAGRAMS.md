I'# OTA System Diagrams & Visual Guides

## 1. High-Level OTA Flow

```
    USER                          DEVICE                       FIRMWARE
     │                              │                              │
     │                              │                              │
     ├─ Open http://sensorhub.local/
     │                              │                              │
     ├─ Settings → Firmware         │                              │
     │                              │                              │
     ├─ Select firmware-0.2.0.bin   │                              │
     │                              │                              │
     ├─ Click Upload               │                              │
     │                              │                              │
     │  ┌─ HTTP POST ──────────────>│                              │
     │  │ /api/firmware/upload      │                              │
     │  │ (multipart, 1MB)          │  ┌─ OTA Manager ─────────────┤
     │  │                           │  │ startUpdate()             │
     │  │                           │  │ allocate OTA partition    │
     │  │                           │  │                           │
     │  ├─ Chunk 1 (4KB) ──────────>│─ writeUpdateData()          │
     │  │                           │  write to flash             │
     │  │                           │  progress: 0%               │
     │  │                           │                             │
     │  ├─ Chunk 2 (4KB) ──────────>│─ writeUpdateData()          │
     │  │                           │  write to flash             │
     │  │                           │  progress: 1%               │
     │  │                           │                             │
     │  ├─ [...]                   │  [repeats ...]              │
     │  │                           │                             │
     │  ├─ Final chunk (2KB) ──────>│─ writeUpdateData()          │
     │  │                           │  write to flash             │
     │  │                           │  progress: 100%             │
     │  │                           │                             │
     │  │ ← 200 OK ─────────────────├─ endUpdate()                │
     │  │ "Rebooting..."            │  verify CRC32               │
     │  │                           │  set OTA flag               │
     │  │                           │  [REBOOT]                  │
     │  │                           │                              V
     │  │                           │  ┌──────────────────────────┐
     │  │                           │  │ Bootloader (ROM Code)    │
     │  │                           │  │ - Check OTA flag         │
     │  │                           │  │ - Switch partition       │
     │  │                           │  │ - Validate firmware      │
     │  │                           │  │ - Jump to new code       │
     │  │                           │  └──────────────────────────┘
     │  │                           │                              │
     │  │                           │  New firmware boots here     V
     │  │                           │  (v0.2.0)                   ✅
     │  │                           │                              │
     │  └─ Page auto-refreshes ─────┼─ mDNS/WiFi comes online     │
     │                              │                              │
     └─ Dashboard loads             │                              │
        Shows v0.2.0!               │                              │
```

---

## 2. Flash Memory Layout (Before & After)

### Before Update
```
┌─────────────────────────────────────────────┐
│          ESP32-S3 Flash (16 MB)             │
├─────────────────────────────────────────────┤
│ Bootloader (64 KB)                          │
├─────────────────────────────────────────────┤
│ Partition Table (8 KB)                      │
├─────────────────────────────────────────────┤
│ ACTIVE FIRMWARE (4 MB)                      │
│ (v0.1.0 - currently running)                │
├─────────────────────────────────────────────┤
│ OTA PARTITION (4 MB) [EMPTY]                │
│ (ready to receive new firmware)             │
├─────────────────────────────────────────────┤
│ FRAM (1 MB) - Sensor data (untouched)       │
├─────────────────────────────────────────────┤
│ LittleFS (4 MB) - Web files (untouched)     │
├─────────────────────────────────────────────┤
│ Free Space (remaining ~4 MB)                │
└─────────────────────────────────────────────┘

Active: Partition 0 (running v0.1.0)
```

### During Update
```
┌─────────────────────────────────────────────┐
│          ESP32-S3 Flash (16 MB)             │
├─────────────────────────────────────────────┤
│ Bootloader (64 KB)                          │
├─────────────────────────────────────────────┤
│ Partition Table (8 KB)                      │
├─────────────────────────────────────────────┤
│ ACTIVE FIRMWARE (4 MB)                      │
│ (v0.1.0 - still running, untouched)         │
├─────────────────────────────────────────────┤
│ OTA PARTITION (4 MB) [WRITING...]           │
│ ████████░░░░░░░░░░░░░░░░░░░░░░░░  50%      │
│ (new firmware 0.2.0 being written)          │
├─────────────────────────────────────────────┤
│ FRAM (1 MB) - Sensor data (untouched)       │
├─────────────────────────────────────────────┤
│ LittleFS (4 MB) - Web files (untouched)     │
├─────────────────────────────────────────────┤
│ Free Space                                  │
└─────────────────────────────────────────────┘

Active: Still Partition 0 (v0.1.0 running)
Writing to: Partition 1 (OTA)
```

### After Update (After Reboot)
```
┌─────────────────────────────────────────────┐
│          ESP32-S3 Flash (16 MB)             │
├─────────────────────────────────────────────┤
│ Bootloader (64 KB)                          │
├─────────────────────────────────────────────┤
│ Partition Table (8 KB)                      │
├─────────────────────────────────────────────┤
│ ACTIVE FIRMWARE (4 MB)                      │
│ (v0.1.0 - now backup, not running)          │
├─────────────────────────────────────────────┤
│ OTA PARTITION (4 MB) [ACTIVE NOW]           │
│ ████████████████████████████████████████    │
│ (v0.2.0 - currently running)                │
├─────────────────────────────────────────────┤
│ FRAM (1 MB) - Sensor data (UNTOUCHED! ✅)   │
│ - All readings preserved                    │
│ - History intact                            │
├─────────────────────────────────────────────┤
│ LittleFS (4 MB) - Web files (UNTOUCHED! ✅) │
│ - Dashboard UI                              │
│ - Config files                              │
├─────────────────────────────────────────────┤
│ Free Space                                  │
└─────────────────────────────────────────────┘

Active: Partition 1 (v0.2.0 running) ✅
Backup: Partition 0 (v0.1.0 available for rollback)
```

---

## 3. State Machine: OTA Manager

```
            ┌──────────────┐
            │   IDLE       │ (startup)
            │              │
            └──────┬───────┘
                   │
                   │ startUpdate(size)
                   │ Update.begin(size) succeeds
                   ↓
            ┌──────────────┐
            │  UPLOADING   │ (receiving chunks)
            │              │
            │ writeUpdateData(buf, len)
            │ → write to flash
            │ → track progress
            │ → report status
            │ [0% → 100%]
            └──────┬───────┘
                   │
        ┌──────────┴──────────┐
        │                     │
        │ error during write  │ endUpdate()
        │                     │ Update.end(true)
        ↓                     ↓
    ┌─────────────┐    ┌─────────────────┐
    │   ERROR     │    │ FINALIZING      │
    │             │    │                 │
    │ abortUpdate │    │ verify CRC32    │
    │ or fail     │    │ set OTA flag    │
    └──────┬──────┘    │ trigger reboot  │
           │           └────────┬────────┘
           │                    │
           └────────┬───────────┘
                    │
                    ↓
            ┌──────────────┐
            │   IDLE       │ (after reboot or abort)
            │ (or new fw)  │
            └──────────────┘
```

---

## 4. Version Release Timeline

```
Time ──────────────────────────────────────────────────────→

v0.1.0
  │
  ├─ Development (weeks 1-4)
  │  └─ Feature work
  │
  ├─ Testing (day 5)
  │  └─ QA on hardware
  │
  ├─ Release (day 6)
  │  ├─ Tag: git tag v0.1.0
  │  ├─ Binary: firmware-0.1.0.bin
  │  └─ Users can download & flash
  │
  └─ Deployed (day 7)
     └─ Devices now running 0.1.0


v0.1.1  (bugfix)
  │
  ├─ Bug fix (day 8)
  │  └─ Fix WiFi timeout issue
  │
  ├─ Quick release (day 9)
  │  └─ Tag v0.1.1
  │
  └─ Users OTA update from 0.1.0 → 0.1.1
     └─ No data loss, instant upgrade


v0.2.0  (major features)
  │
  ├─ Development (weeks 10-16)
  │  ├─ Display rendering
  │  ├─ LoRaWAN transmission
  │  ├─ OTA system
  │  └─ Geocoding
  │
  ├─ Testing (week 17)
  │  ├─ E-Ink display tests
  │  ├─ TFT display tests
  │  ├─ LoRa TX tests
  │  ├─ OTA update tests
  │  └─ Full regression
  │
  ├─ Release (day 1)
  │  ├─ FW_VERSION = 0.2.0
  │  ├─ pio run --target clean && pio run
  │  ├─ cp firmware.bin releases/firmware-0.2.0.bin
  │  ├─ git tag v0.2.0
  │  └─ Deploy to GitHub Releases
  │
  └─ Users can now:
     ├─ OTA update: 0.1.1 → 0.2.0
     ├─ See new displays
     ├─ Send via LoRaWAN
     └─ Update via OTA itself!


v0.3.0  (next iteration)
  │
  └─ Cycle repeats...
```

---

## 5. OTA Partition Switching Logic

```
Device Powers On
    │
    ├─ ROM Code (Bootloader) loads
    │
    ├─ Check OTA Partition Table
    │
    ├─ Look for OTA flag in NVS (non-volatile storage)
    │
    ├─ Decision Point:
    │
    ├─ IF OTA_FLAG_SET and NEW_FIRMWARE_VALID:
    │  │
    │  ├─ Copy OTA partition → Active partition
    │  ├─ Mark as current in partition table
    │  ├─ Jump to new firmware code
    │  │
    │  └─ NEW FIRMWARE RUNS ✅
    │     Device is now on v0.2.0
    │
    └─ ELSE (OTA flag not set or invalid):
       │
       ├─ Boot from Active partition as normal
       │
       └─ OLD FIRMWARE RUNS (fallback)
          Device stays on v0.1.0
```

---

## 6. Update Progress Display (Web Dashboard)

```
Uploading firmware-0.2.0.bin

Progress:  0% ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 0 / 1024 KB
          10% █░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 102 / 1024 KB
          20% ██░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 204 / 1024 KB
          30% ███░░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 307 / 1024 KB
          40% ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░ 409 / 1024 KB
          50% █████░░░░░░░░░░░░░░░░░░░░░░░░░░░ 512 / 1024 KB  ← Halfway!
          60% ██████░░░░░░░░░░░░░░░░░░░░░░░░░░ 614 / 1024 KB
          70% ███████░░░░░░░░░░░░░░░░░░░░░░░░░ 717 / 1024 KB
          80% ████████░░░░░░░░░░░░░░░░░░░░░░░░ 819 / 1024 KB
          90% █████████░░░░░░░░░░░░░░░░░░░░░░░ 922 / 1024 KB
         100% ██████████████████████████████████ 1024 / 1024 KB ✅

Status: Verifying firmware...
        (CRC32 check, should take 2-5 seconds)

Status: Device rebooting...
        (Will be back online in ~10 seconds)

[Auto-refresh in 3 seconds...]

✅ Update Complete!
   Firmware v0.2.0 installed successfully
   Device is online and running new firmware
```

---

## 7. Failure Scenarios & Recovery

### Scenario 1: Network Drops During Upload

```
Progress 0%   → 50%   [NETWORK DROPS]   ❌
    │
    ├─ OTA manager detects write failure
    ├─ abortUpdate() called
    ├─ OTA partition marked invalid
    ├─ HTTP connection closes with error
    │
    └─ OLD FIRMWARE STILL RUNNING ✅
       (nothing was damaged)

User's action:
    └─ Retry upload (will restart from 0%)
```

### Scenario 2: Corrupted File Upload

```
Upload completes (100%)
    │
    ├─ OTA manager calls endUpdate()
    ├─ CRC32 check FAILS ❌
    ├─ OTA.end() returns failure
    │
    └─ Bootloader skips OTA partition
       OLD FIRMWARE BOOTS AUTOMATICALLY ✅

User's action:
    └─ Re-download firmware binary
       └─ Retry upload
```

### Scenario 3: Device Loses Power During Update

```
Power on during upload (e.g., at 75%)
    │
    ├─ OTA flag is NOT set (update not complete)
    ├─ Bootloader sees: incomplete update
    ├─ Skips OTA partition
    │
    └─ OLD FIRMWARE BOOTS ✅
       (partial OTA data on flash is harmless)

User's action:
    └─ Retry upload once device is stable
```

### Scenario 4: New Firmware Has Bug & Crashes

```
New firmware (v0.2.0) boots
    │
    ├─ Sensor reads fail
    ├─ Watchdog timer expires (no heartbeat)
    ├─ Hardware watchdog triggers reboot
    │
    ├─ Bootloader checks OTA flag
    ├─ Sees OTA was enabled for v0.2.0
    ├─ (may have internal fallback logic)
    │
    └─ DEVICE STAYS IN CRASH LOOP ⚠️

Mitigation:
    ├─ Serial upload fallback firmware (older version)
    │  └─ pio run --target upload
    │
    └─ Device back to stable v0.1.0
```

---

## 8. Decision Tree: Should I Update?

```
                    Device has v0.1.0
                          │
                          ├─ Check: Is there v0.2.0?
                          │
                 ┌────────┴────────┐
                 │                 │
            No Updates         Updates Available
                 │              (v0.2.0)
                 │                 │
            Stay on v0.1.0     ┌───┴────┐
                               │        │
                        Read Release Notes
                               │
                    ┌──────────┴──────────┐
                    │                     │
            Bug fixes only?        New features?
                    │                     │
            Optional update        Recommended?
                    │                     │
             ┌──────┴──────┐      ┌──────┴──────┐
             │             │      │             │
         Not urgent   Want new   Critical    Nice to have
         v0.2.0       features?      │           │
         doesn't       Decide      Update!   Optional
         affect me      based on    ASAP      (your call)
                        needs
                         │
                         ├─ If WiFi issue: UPDATE
                         ├─ If crash bug: UPDATE
                         ├─ If security: UPDATE
                         ├─ If new UI: nice-to-have
                         └─ If just tweaks: skip
```

---

## 9. Version Release Calendar Template

```
┌────────────────────────────────────────────────────────────┐
│              Firmware Release Calendar 2026               │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  JAN        FEB        MAR        APR        MAY      JUN │
│
│  v0.1.0   v0.1.1    v0.2.0     v0.3.0     v0.4.0   v1.0.0
│  (init)   (bugfix)   (display)  (enhance) (refine)  (prod)
│
│  ├─ Core  ├─ WiFi   ├─ E-Ink   ├─ Multi-  ├─ Perf  ├─ Full
│  │  deps  │  timeout│  TFT     │  lang    │  opt   │  test
│  │        │         │  LoRaWAN │         │         │
│  │        │         │  OTA     │         │         │
│  │        │         │  Geocode │         │         │
│  │        │         │          │         │         │
│  └─ 1.0MB└─ 1.0MB  └─ 1.1MB   └─ 1.2MB  └─ 1.2MB└─ 1.2MB
│
└────────────────────────────────────────────────────────────┘

Schedule:
- Releases: First Friday of each month (approx)
- Development: 3 weeks per release
- Testing: 1 week per release
- User update time: 1-2 weeks adoption
```

