# OTA Firmware Update Guide

## How OTA Updates Work

The WisBlock Sensor Hub uses ESP32-S3's built-in OTA (Over-The-Air) update system. Here's the complete flow:

### 1. **User Initiates Update**
- User accesses the web dashboard and uploads new firmware file
- Or calls `/api/firmware/upload` endpoint with binary via HTTP POST

### 2. **Web Server Receives Firmware**
- Web server receives multipart form data containing `.bin` file
- Extracts Content-Length header (total firmware size)
- Calls `ota_manager.startUpdate(contentLength)`

### 3. **OTA Manager Prepares**
```cpp
bool startUpdate(size_t contentLength) {
    // Checks if another update is in progress
    // Verifies there's enough flash space
    // Calls ESP32 Update.begin(contentLength)
    // Allocates OTA partition and prepares for data
}
```

### 4. **Streaming Upload**
- Web server streams firmware data in chunks (e.g., 4KB at a time)
- Each chunk calls `ota_manager.writeUpdateData(buffer, len)`
- Manager writes to OTA partition while calculating CRC32 checksum
- Progress is tracked and reported (every 10%)

### 5. **Finalization & Reboot**
```cpp
bool endUpdate() {
    // Calls Update.end(true)  // true = reboot automatically
    // ESP32 validates firmware integrity
    // If valid: bootloader switches OTA partition and reboots
    // If invalid: reboot to previous firmware (automatic rollback)
}
```

### 6. **After Reboot**
- Device boots new firmware
- New version runs normally
- Old firmware remains in second partition as fallback

---

## Current Status

### What's Implemented
✅ Core OTA manager class (`src/ota_manager.h`)
- `startUpdate()` - Begin upload
- `writeUpdateData()` - Process chunks
- `endUpdate()` - Finalize and reboot
- `abortUpdate()` - Cancel in progress
- Progress tracking (0-100%)
- Health heartbeat updates
- Display progress integration
- Error handling with rollback

### What Still Needs Integration
⏳ Web server endpoints:
- [ ] `POST /api/firmware/upload` - Handle multipart file upload
- [ ] `GET /api/firmware/check` - Check for available updates (server-side)
- [ ] `GET /api/firmware/version` - Get current firmware version

⏳ Version management:
- [ ] Add `FW_VERSION` constant to config.h
- [ ] Link it to OTA manager for reporting
- [ ] Add version to `/api/status` endpoint

---

## How to Release a New Version

### Step 1: Update Version Number

Edit `src/config.h` and increment the firmware version:

```cpp
// At the top of main.cpp or config.h, add:
#define FW_VERSION      "0.2.0"
#define FW_BUILD_DATE   __DATE__
#define FW_BUILD_TIME   __TIME__
```

Then link it in OTA manager:

```cpp
// In ota_manager.h getFirmwareVersion():
static const char* getFirmwareVersion() {
    return FW_VERSION;  // Read from config.h
}
```

### Step 2: Build the Firmware Binary

```bash
cd firmware
pio run
```

This creates the binary at:
```
.pio/build/rak3112/firmware.bin
```

### Step 3: Create Release Package

Option A: **Manual distribution** (for now)
```bash
cp .pio/build/rak3112/firmware.bin releases/firmware-0.2.0.bin
md5sum releases/firmware-0.2.0.bin > releases/firmware-0.2.0.md5
```

Option B: **Automated CI/CD** (future)
- Use GitHub Actions to auto-build on tag
- Upload to releases page
- Generate changelog

### Step 4: Upload to Device

**Method 1: Web Dashboard** (Once endpoints are integrated)
```
1. Open http://sensorhub.local/ in browser
2. Go to Settings → Firmware
3. Click "Choose File" and select firmware-0.2.0.bin
4. Click "Upload"
5. Watch progress bar
6. Device reboots automatically
```

**Method 2: Direct API Call**
```bash
curl -X POST \
  -F "firmware=@releases/firmware-0.2.0.bin" \
  http://sensorhub.local/api/firmware/upload
```

**Method 3: Manual via Serial** (Fallback)
```bash
pio run --target upload
```

---

## OTA Update Flow (Detailed)

### Request → Response Cycle

```
Browser/Client                Web Server              OTA Manager         ESP32
       │                          │                        │                │
       ├─ POST /api/firmware/upload ────────────────────────┤
       │  (multipart/form-data)    │                        │                │
       │                           │                        │                │
       │                           ├─ Check file size       │                │
       │                           ├─ Get Content-Length    │                │
       │                           │                        │                │
       │                           ├─────────startUpdate()──┤                │
       │                           │                        ├─ Update.begin()─┤
       │                           │                        │    [alloc OTA]  │
       │                           │        ACK             │                │
       │                           │<──────────────────────┤                │
       │                           │                        │                │
       ├─ Firmware binary (streaming chunks) ──────────────┤
       │  [0-4KB]                  │                        │                │
       │                           ├──writeUpdateData()────┤                │
       │                           │                        ├─ Update.write()─┤
       │                           │                        │    [flash OTA]  │
       │                           │                        │                │
       │  [4-8KB]                  │                        │                │
       │                           ├──writeUpdateData()────┤                │
       │                           │                        │                │
       ├─ [continues...] ──────────┤                        │                │
       │                           │                        │                │
       │  [final chunk]            │                        │                │
       │                           ├──writeUpdateData()────┤                │
       │                           │                        │                │
       │                           ├────endUpdate()────────┤                │
       │                           │                        ├─ Update.end()──┤
       │                           │                        │  [verify/CRC]  │
       │                           │                        │  [set OTA flag] │
       │                           │                        │                │
       │                           │      Success           │                │
       │                           │<──────────────────────┤                │
       │    200 OK + "Rebooting"   │                        │                │
       │<──────────────────────────┤                        │   [reboot]     ├──┐
       │                           │                        │                │  │
       │                           │                        │                │  │ CPU resets
       │                           │                        │                │  │
       │                           │                        │                │  │
       │                    Device comes back online        │                │
       │  Auto-connects WiFi       │                        │                │
       │                           │  New firmware running  │                │
       │◄──────────────────────────────────────────────────────────────────┤
```

---

## Rollback & Safety

### Automatic Rollback
If update fails at any point:
- **During upload**: Update aborted, old firmware intact
- **During verification**: CRC check fails, bootloader reverts to previous partition
- **During boot**: New firmware crashes, watchdog triggers reboot to old version

### Manual Rollback
If you need to revert to previous firmware:
```bash
# Option 1: Upload previous binary as if it were new firmware
curl -X POST -F "firmware=@releases/firmware-0.1.0.bin" \
  http://sensorhub.local/api/firmware/upload

# Option 2: Serial terminal (if device doesn't boot)
pio run --target upload    # Uploads most recent built firmware
```

### Checking Firmware Integrity
```cpp
// In supervisor task or API endpoint:
Serial.printf("Flash MD5: %s\n", ESP.getSketchMD5().c_str());
Serial.printf("Free sketch space: %u bytes\n", ESP.getFreeSketchSpace());
```

---

## Versioning Strategy

### Semantic Versioning: MAJOR.MINOR.PATCH

- **MAJOR** (0.x.x → 1.x.x): Breaking changes (new hardware, incompatible protocol)
- **MINOR** (x.1.x → x.2.x): New features, backward compatible
- **PATCH** (x.x.0 → x.x.1): Bug fixes, no new features

**Example Release History:**
```
0.1.0 - Initial release (all core features)
0.2.0 - Add display rendering, OTA updates, geocoding
0.2.1 - Fix WiFi reconnection bug
0.3.0 - Add multi-language support
1.0.0 - Production release
```

---

## Release Checklist

Before releasing a new firmware version:

- [ ] Update `FW_VERSION` in config.h
- [ ] Update CHANGELOG.md with new features/fixes
- [ ] Run full test suite (if available)
- [ ] Test OTA update on physical hardware
- [ ] Verify rollback works
- [ ] Build clean binary: `pio run`
- [ ] Calculate MD5: `md5sum .pio/build/rak3112/firmware.bin`
- [ ] Tag in git: `git tag v0.2.0`
- [ ] Push to repository: `git push origin main --tags`
- [ ] Create GitHub Release with binary attachment
- [ ] Announce in release notes

---

## Future Enhancements

### Phase 1: Basic OTA (Current)
- [x] Core OTA manager
- [ ] Web endpoints integration
- [ ] Version tracking

### Phase 2: Smart Updates (Next)
- [ ] Check for updates from remote server
- [ ] Delta updates (only changed files)
- [ ] Signed firmware verification
- [ ] Automatic update scheduling

### Phase 3: Advanced (Future)
- [ ] Multiple release channels (stable/beta/dev)
- [ ] Rollback confirmation
- [ ] Update statistics/telemetry
- [ ] Group device management

---

## Troubleshooting

### "Cannot start OTA, free space: 0 bytes"
- Device has insufficient flash space for second firmware
- Solution: Reduce firmware size by disabling unused sensors/features
- Check: `ESP.getFreeSketchSpace()`

### Update fails halfway through
- Network connectivity issue
- Solution: Retry from browser (will restart upload)
- Check WiFi signal strength with `/api/live`

### Device reboots after update but new firmware doesn't run
- Firmware corruption or CRC failure
- Automatic rollback to previous version will occur
- Check serial logs during boot

### Want to verify firmware before upload
```bash
# Check binary size
ls -lh .pio/build/rak3112/firmware.bin

# Calculate checksum
md5sum .pio/build/rak3112/firmware.bin

# Compare with previous release
diff <(strings previous-firmware.bin) <(strings new-firmware.bin)
```

---

## Integration Tasks

The following tasks need to be completed to fully enable OTA updates:

1. **Web Server Integration** (`src/network/web_server.h`)
   - Add `POST /api/firmware/upload` endpoint
   - Handle multipart form data
   - Stream to OTA manager
   - Return progress/status

2. **Version Display** (`src/config.h`, `src/types.h`)
   - Define `FW_VERSION` constant
   - Add to SystemStatus struct
   - Expose via `/api/status` endpoint

3. **Dashboard UI** (`data/index.html`)
   - Add "Settings" → "Firmware" section
   - File upload form
   - Progress bar
   - Auto-refresh on reboot

4. **Build Script** (CI/CD)
   - Auto-build on git tag
   - Generate release notes
   - Upload to GitHub Releases

---

## Reference

- [ESP32-S3 OTA Programming](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html)
- [Arduino Update Library](https://github.com/espressif/arduino-esp32/tree/master/libraries/Update)
- [PlatformIO OTA Guide](https://docs.platformio.org/en/latest/platforms/espressif32.html#over-the-air-ota-update)

