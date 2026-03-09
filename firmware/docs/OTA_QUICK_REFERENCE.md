# OTA Firmware Update - Quick Reference

## How It Works (Simple Version)

```
1. You upload firmware.bin via web dashboard
   ↓
2. Device receives file in chunks (streaming)
   ↓
3. Each chunk is written to OTA partition in flash
   ↓
4. After all chunks received, CRC32 is verified
   ↓
5. Device automatically reboots
   ↓
6. Bootloader sees OTA flag and switches to new partition
   ↓
7. Device boots new firmware
```

## Workflow: From Code → Device

```bash
# Step 1: Write code, update version
vim src/config.h          # Change FW_VERSION="0.2.0"
vim src/...               # Make your changes

# Step 2: Build firmware
cd firmware
pio run                   # Creates .pio/build/rak3112/firmware.bin

# Step 3: Test locally
pio run --target upload   # Upload via serial (first time or fallback)

# Step 4: Create release package
cp .pio/build/rak3112/firmware.bin releases/firmware-0.2.0.bin
md5sum releases/firmware-0.2.0.bin > releases/firmware-0.2.0.md5

# Step 5: Share firmware file
git add releases/firmware-0.2.0.bin
git tag v0.2.0
git push origin main --tags

# Step 6: Device owner updates
# Open http://sensorhub.local/
# Click Settings → Firmware
# Select firmware-0.2.0.bin
# Click Upload
# Watch progress 0% → 100%
# Device reboots automatically
```

## OTA Manager API

### Methods

```cpp
OTAManager ota(g_health, displayDriver);

// 1. Initialize once at startup
ota.init();

// 2. Start upload (called by web server)
bool ok = ota.startUpdate(fileSize);

// 3. Process incoming chunks
while (chunks_arriving) {
    ota.writeUpdateData(buffer, len);
}

// 4. Finalize (device reboots automatically)
ota.endUpdate();        // Returns true if reboot initiated

// 5. Or cancel if needed
ota.abortUpdate();
```

### Status Accessors

```cpp
bool isUpdating = ota.isUpdating();          // true while upload in progress
uint8_t percent = ota.getProgress();         // 0-100
size_t received = ota.getBytesReceived();    // bytes written so far
size_t total = ota.getTotalBytes();          // total file size
size_t free = OTAManager::getFreeSketchSpace();  // available flash
```

## Version Numbers (Semantic Versioning)

```
MAJOR.MINOR.PATCH

0.1.0  = Initial (alpha)
0.2.0  = Add features (beta)
0.2.1  = Bug fix (release candidate)
0.3.0  = More features
1.0.0  = Stable release
1.1.0  = New features, stable
1.1.1  = Bug fix, stable
2.0.0  = Breaking changes
```

## Safety Guarantees

✅ **Automatic Rollback**
- If upload fails → old firmware stays
- If CRC fails → old firmware boots
- If new firmware crashes → watchdog reverts to old

✅ **Data Preserved**
- FRAM sensor data untouched
- WiFi credentials untouched
- Settings preserved

✅ **Power Safe**
- If power lost during update → old firmware boots
- OTA partition is separate from main code

## Common Commands

### Build & Upload via Serial (First time / No OTA)
```bash
pio run --target upload
```

### Upload via OTA (After first serial upload)
```bash
curl -X POST -F "firmware=@firmware.bin" \
  http://sensorhub.local/api/firmware/upload
```

### Check Current Version
```bash
curl http://sensorhub.local/api/status | grep version
```

### Check Flash Size
```bash
curl http://sensorhub.local/api/status | grep -i sketch
```

### Revert to Previous Firmware
```bash
# Upload old binary same way as new one
curl -X POST -F "firmware=@firmware-0.1.0.bin" \
  http://sensorhub.local/api/firmware/upload
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| "free space: 0 bytes" | Firmware too large, reduce features |
| Update fails halfway | Network issue, retry upload |
| New firmware doesn't boot | Auto-rollback to old, check logs |
| Can't upload via OTA | Use serial: `pio run --target upload` |
| Need to know firmware size | `ls -lh .pio/build/rak3112/firmware.bin` |

## File Locations

```
firmware/
├── .pio/build/rak3112/
│   └── firmware.bin           ← This is what you upload
├── releases/
│   ├── firmware-0.1.0.bin     ← Archive old versions
│   ├── firmware-0.2.0.bin     ← New version
│   └── firmware-0.2.0.md5     ← Checksum
└── src/
    └── config.h               ← Change FW_VERSION here
```

## What Happens During Update

```
Receive→Write→Report→Loop→Verify→Reboot→Boot New

Each loop:
- Receive up to 4KB chunk
- Write to OTA partition (verify each write)
- Update progress counter
- Report progress every 10%
- Repeat until all data received

After all received:
- Verify CRC32 of entire firmware
- Set OTA partition as active
- Tell bootloader to use OTA partition
- Trigger reboot

After reboot:
- Bootloader checks OTA flag
- Copies OTA partition to active (if valid)
- Jumps to new firmware
- New firmware starts
```

## Integration Still Needed

These endpoints don't exist yet but are in `ota_manager.h`:

```
POST /api/firmware/upload
  Upload firmware binary
  Returns: {success: true/false, message: string}

GET /api/firmware/version
  Returns: {version: "0.2.0", buildDate: "...", bootloader: "..."}

GET /api/firmware/check
  Returns: {hasUpdate: true/false, available: "0.3.0"}
```

Once integrated, you'll have full UI in web dashboard for OTA updates!

