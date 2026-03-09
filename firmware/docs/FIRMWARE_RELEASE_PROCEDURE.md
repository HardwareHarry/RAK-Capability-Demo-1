# Firmware Release Procedure

## Step-by-Step: Release Version 0.2.0

This guide walks through a complete firmware release from code changes to device update.

---

## Phase 1: Development & Testing

### 1.1 Make Code Changes

```bash
cd firmware
vim src/sensors/environment.h        # Fix bug X
vim src/network/lora_manager.h       # Add feature Y
vim data/index.html                  # Update dashboard
```

### 1.2 Build & Test Locally

```bash
# Build firmware
pio run

# Test via serial (uploads .bin to device via UART)
pio run --target upload

# Wait for device to reboot
# Check serial output for any errors
pio device monitor --port /dev/ttyUSB0 --baud 115200
```

### 1.3 Verify Functionality

- Open http://sensorhub.local/ in browser
- Check that new features work
- Verify sensors are reading correctly
- Test WiFi connection
- Confirm LoRaWAN transmission

---

## Phase 2: Prepare Release

### 2.1 Update Version Number

Edit `src/config.h`:

```cpp
// Before:
#define FW_VERSION          "0.1.0"

// After:
#define FW_VERSION          "0.2.0"
```

### 2.2 Clean Build (Fresh)

```bash
# Remove old build artifacts
pio run --target clean

# Build fresh
pio run

# Verify binary was created
ls -lh .pio/build/rak3112/firmware.bin
# Should show: 995 KB (approximately)
```

### 2.3 Calculate Checksum

```bash
# Generate MD5 hash for integrity verification
md5sum .pio/build/rak3112/firmware.bin > firmware-0.2.0.md5

# Verify the file
cat firmware-0.2.0.md5
# Output: a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6  .pio/build/rak3112/firmware.bin
```

### 2.4 Create Release Directory

```bash
# Create releases folder if it doesn't exist
mkdir -p releases

# Copy firmware and checksum
cp .pio/build/rak3112/firmware.bin releases/firmware-0.2.0.bin
cp firmware-0.2.0.md5 releases/firmware-0.2.0.md5

# List what we're releasing
ls -lh releases/
# Output:
# firmware-0.1.0.bin      995 KB  (previous version - keep as backup)
# firmware-0.1.0.md5      64 B
# firmware-0.2.0.bin      1.0 MB  (new version - ready to release)
# firmware-0.2.0.md5      64 B
```

### 2.5 Create Release Notes

Create `releases/RELEASE-0.2.0.md`:

```markdown
# WisBlock Sensor Hub v0.2.0

**Release Date**: March 9, 2026

## What's New

### Features
- ✨ E-Ink and TFT display support with beautiful UI
- ✨ LoRaWAN OTAA transmission with automatic backfill
- ✨ System thermal monitoring with protection
- ✨ Reverse geocoding (offline + online hybrid)
- ✨ OTA firmware updates via web interface
- ✨ Captive portal for WiFi setup in AP mode

### Bug Fixes
- 🐛 Fixed WiFi reconnection timeout issue
- 🐛 Fixed GNSS fix detection logic
- 🐛 Fixed FRAM ring buffer wrap-around

### Hardware
- RAK3312 (ESP32-S3 + SX1262)
- RAK14000 E-Ink or RAK14014 TFT Display
- 5 sensor inputs (BME680, PMSA003I, VEML7700, RV-3028, ZOE-M8Q)
- 1MB FRAM storage (~12 days of data)

## Installation

### First Time (Serial Upload)
```bash
pio run --target upload
```

### OTA Update (Via Dashboard)
1. Open http://sensorhub.local/
2. Go to Settings → Firmware
3. Click "Choose File" → select `firmware-0.2.0.bin`
4. Click "Upload"
5. Watch progress bar 0% → 100%
6. Device reboots automatically

### Via Command Line
```bash
curl -X POST -F "firmware=@firmware-0.2.0.bin" \
  http://sensorhub.local/api/firmware/upload
```

## Verification

After update completes:
- Device reboots automatically
- Check `/api/status` → firmware version should be "0.2.0"
- LED indicators confirm system health
- LoRaWAN transmits data to server

## Rollback

If issues occur:
```bash
curl -X POST -F "firmware=@firmware-0.1.0.bin" \
  http://sensorhub.local/api/firmware/upload
```

## Known Issues

- None known (please report at https://github.com/...)

## Credits

Built with ❤️ for IoT enthusiasts everywhere
```

---

## Phase 3: Version Control & Release

### 3.1 Commit Changes

```bash
# Stage all changes
git add src/config.h
git add src/...
git add data/index.html
git add releases/firmware-0.2.0.*
git add releases/RELEASE-0.2.0.md

# Commit with clear message
git commit -m "Release v0.2.0: Display rendering, LoRaWAN, OTA updates

- Implement E-Ink and TFT display drivers
- Add LoRaWAN OTAA transmission with backfill
- Implement OTA firmware update system
- Add thermal monitoring and protection
- Add reverse geocoding (offline/online)
- Add captive portal for AP mode WiFi setup

This release includes major UI improvements and production-ready
wireless capabilities."

# Verify commit
git log -1 --stat
```

### 3.2 Create Git Tag

```bash
# Create annotated tag (includes tagger name, date, message)
git tag -a v0.2.0 -m "Release 0.2.0 - Display rendering and wireless features"

# Verify tag was created
git tag -l v0.2.0
git show v0.2.0

# Alternative: lightweight tag (just a pointer)
# git tag v0.2.0
```

### 3.3 Push to Repository

```bash
# Push commits
git push origin main

# Push tags (required for release page)
git push origin main --tags

# Verify remote has tag
git ls-remote --tags origin | grep v0.2.0
```

---

## Phase 4: Create GitHub Release (Optional)

If using GitHub, create a release page:

```bash
# Option 1: Via GitHub CLI (if installed)
gh release create v0.2.0 \
  --title "WisBlock Sensor Hub v0.2.0" \
  --notes-file releases/RELEASE-0.2.0.md \
  releases/firmware-0.2.0.bin

# Option 2: Via Web Browser
# 1. Go to https://github.com/YOUR_REPO/releases
# 2. Click "Draft a new release"
# 3. Choose tag: v0.2.0
# 4. Release title: "WisBlock Sensor Hub v0.2.0"
# 5. Copy RELEASE-0.2.0.md content into description
# 6. Attach firmware-0.2.0.bin as binary
# 7. Click "Publish release"
```

---

## Phase 5: End-User Update

### Scenario: User has v0.1.0, wants to update to v0.2.0

```bash
# User downloads firmware from GitHub Releases page
# Or from your server

# Option 1: Web Dashboard Update
1. Open http://sensorhub.local/ in browser
2. Go to Settings → Firmware
3. Click "Choose File"
4. Select firmware-0.2.0.bin (downloaded from releases page)
5. Click "Upload"
6. Watch progress:
   Progress: 0%   ████░░░░░░░░░░░░░░░░░ 
   Progress: 25%  ███████░░░░░░░░░░░░░░░
   Progress: 50%  ██████████░░░░░░░░░░░░
   Progress: 75%  ███████████████░░░░░░░
   Progress: 100% ████████████████████░░
   Success! Rebooting...
7. Dashboard redirects to home page
8. New version running!

# Option 2: Command Line
curl -X POST -F "firmware=@firmware-0.2.0.bin" \
  http://sensorhub.local/api/firmware/upload
# Returns: {"success": true, "message": "Firmware uploaded. Rebooting..."}

# Verify update successful
curl http://sensorhub.local/api/status | grep -i version
# Returns: "version": "0.2.0"
```

---

## Phase 6: Verification Checklist

After release and end-user update:

- [ ] Device boots with new firmware
- [ ] Serial output shows v0.2.0 on startup
- [ ] `/api/status` endpoint returns version 0.2.0
- [ ] All sensors read correctly
- [ ] WiFi connects successfully
- [ ] LoRaWAN transmits data
- [ ] Display shows boot screen correctly
- [ ] Dashboard loads in browser
- [ ] No error messages in console

---

## Future Release (0.3.0)

For the **next** release:

```bash
# 1. Update version
vim src/config.h
# Change: FW_VERSION = "0.3.0"

# 2. Make changes, test locally
pio run
pio run --target upload
# ... test features ...

# 3. Clean build
pio run --target clean
pio run

# 4. Release
cp .pio/build/rak3112/firmware.bin releases/firmware-0.3.0.bin
md5sum releases/firmware-0.3.0.bin > releases/firmware-0.3.0.md5
echo "# v0.3.0 Release Notes" > releases/RELEASE-0.3.0.md

# 5. Version control
git add releases/firmware-0.3.0.*
git commit -m "Release v0.3.0: [features here]"
git tag -a v0.3.0 -m "Release 0.3.0"
git push origin main --tags

# Done!
```

---

## Troubleshooting Release Issues

| Issue | Solution |
|-------|----------|
| "Device stuck at 50%" during OTA | Network dropped, retry upload from browser |
| "Upload says success but v0.1.0 still running" | Check `/api/status` to confirm, device may not have rebooted yet |
| "Want to rollback to 0.1.0" | Upload firmware-0.1.0.bin same way as new version |
| "Forgot to update FW_VERSION" | New firmware will show old version, increment and rebuild |
| "Binary is too large" | Remove debug symbols: `pio run -e rak3112 -t upload --silent` |

---

## Summary

```
Code Changes → Build → Test → Version Bump → Tag → Release → Done!

For each release:
1. Make code changes (feature branch recommended)
2. Build locally: pio run
3. Test on device: pio run --target upload
4. Update FW_VERSION in config.h
5. Clean build: pio run --target clean && pio run
6. Copy to releases/: firmware-X.Y.Z.bin
7. Create release notes: RELEASE-X.Y.Z.md
8. Commit and tag: git commit, git tag, git push --tags
9. Create GitHub Release (optional)
10. Users can now update via web dashboard!
```

---

## Version History Example

```
releases/
├── firmware-0.1.0.bin   (initial release)
├── firmware-0.1.1.bin   (bug fix)
├── firmware-0.2.0.bin   (new features - current)
├── firmware-0.3.0.bin   (next release - when ready)
└── RELEASE-*.md files   (release notes)

Current device: 0.2.0
Available to download: firmware-0.2.0.bin (and older versions for rollback)
```

