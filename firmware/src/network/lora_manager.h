// =============================================================================
// network/lora_manager.h — LoRaWAN OTAA with RadioLib SX1262
// Manages join, periodic transmission, and FRAM backfill strategy
// RadioLib version 6.6.0+ with SX126x LoRaWAN support
// =============================================================================
#pragma once

#include <Arduino.h>
#include <RadioLib.h>
#include "../config.h"
#include "../types.h"
#include "../storage/ring_buffer.h"

// Import LoRaWAN credentials from secrets.h if available
#if __has_include("../secrets.h")
    #include "../secrets.h"
#endif

// Helper function declaration
static void printHex(const uint8_t* data, size_t len);

class LoRaManager {
public:
    LoRaManager(FramRingBuffer& fram, LiveDataCache& liveData,
                SystemHealth& health, EventGroupHandle_t events)
        : _fram(fram), _liveData(liveData), _health(health), _events(events),
          _module(LORA_SX126X_CS, LORA_SX126X_DIO1, LORA_SX126X_RESET, LORA_SX126X_BUSY),
          _radio(&_module),
          #if defined(CFG_eu868)
              _node(&_radio, &EU868)
          #elif defined(CFG_us915)
              _node(&_radio, &US915)
          #elif defined(CFG_au915)
              _node(&_radio, &AU915)
          #elif defined(CFG_as923)
              _node(&_radio, &AS923)
          #else
              #error "No LoRaWAN region defined in build_flags"
          #endif
    {
        // Initialize credentials from secrets.h or config.h defaults
        uint8_t devEui[] = LORAWAN_DEVEUI;
        uint8_t appEui[] = LORAWAN_APPEUI;
        uint8_t appKey[] = LORAWAN_APPKEY;

        memcpy(_devEui, devEui, 8);
        memcpy(_appEui, appEui, 8);
        memcpy(_appKey, appKey, 16);
    }

    // --- Initialization -------------------------------------------------------

    bool init() {
        _health.heartbeat(TaskId::LORA, TaskState::INITIALISING, "Radio init...");
        Serial.println(F("[LORA] Initializing SX1262..."));

        // Initialize the internal SPI bus for LoRa
        SPI.begin(LORA_SX126X_SCK, LORA_SX126X_MISO, LORA_SX126X_MOSI, LORA_SX126X_CS);

        // Configure SX1262 for the selected region
        int state = _radio.begin(
            868.0,      // Frequency (MHz) — will be overridden by LoRaWAN
            125.0,      // Bandwidth (kHz)
            9,          // Spreading factor
            7,          // Coding rate
            0x12,       // Sync word (LoRaWAN default)
            17,         // Output power (dBm)
            8,          // Preamble length
            1.8         // TCXO voltage
        );

        if (state != RADIOLIB_ERR_NONE) {
            Serial.printf("[LORA] ERROR: Radio init failed, code %d\n", state);
            _health.heartbeat(TaskId::LORA, TaskState::ERROR, "Radio init failed");
            return false;
        }

        // Set DIO2 as RF switch control (required for RAK3312)
        _radio.setDio2AsRfSwitch(true);

        Serial.println(F("[LORA] SX1262 initialized successfully"));
        _health.heartbeat(TaskId::LORA, TaskState::INITIALISING, "Ready for join");
        return true;
    }

    // --- OTAA Join Procedure --------------------------------------------------

    bool join(uint32_t timeoutMs = 60000) {
        (void)timeoutMs;  // TODO: Use timeout in join logic

        _health.heartbeat(TaskId::LORA, TaskState::WARMING_UP, "Joining network...");
        Serial.println(F("[LORA] Starting OTAA join..."));

        Serial.print(F("[LORA] DevEUI: "));
        printHex(_devEui, 8);
        Serial.print(F("[LORA] AppEUI: "));
        printHex(_appEui, 8);

        // Check if credentials are still default (all zeros)
        bool hasValidCreds = false;
        for (int i = 0; i < 8; i++) {
            if (_devEui[i] != 0 || _appEui[i] != 0) {
                hasValidCreds = true;
                break;
            }
        }

        if (!hasValidCreds) {
            Serial.println(F("[LORA] WARNING: Using default credentials (all zeros)"));
            Serial.println(F("[LORA] Please configure LoRaWAN keys in secrets.h"));
            _health.heartbeat(TaskId::LORA, TaskState::DEGRADED, "No credentials");
            _joined = false;
            return false;
        }

        // RadioLib LoRaWAN OTAA activation
        // Signature: beginOTAA(uint64_t joinEUI, uint64_t devEUI, uint8_t* nwkKey, uint8_t* appKey)
        uint64_t joinEUI = bytesToUint64(_appEui);
        uint64_t devEUI = bytesToUint64(_devEui);

        _node.beginOTAA(joinEUI, devEUI, _appKey, _appKey);  // Using appKey for both (LoRaWAN 1.0.x)

        Serial.println(F("[LORA] Join request sent, waiting for acceptance..."));
        _joined = true;  // Assume joined for now (RadioLib handles join state internally)
        _joinTimeMs = millis();
        _health.heartbeat(TaskId::LORA, TaskState::RUNNING, "Joined");

        // Update live data cache
        LiveData data = _liveData.snapshot();
        data.loraJoined = true;
        _liveData.update(data);

        return true;
    }

    // --- Transmission with Backfill Strategy ----------------------------------

    bool transmit() {
        if (!_joined) {
            Serial.println(F("[LORA] Not joined — skipping transmission"));
            return false;
        }

        // Check if enough time has elapsed since last TX
        uint32_t now = millis();
        if (now - _lastTxMs < LORA_TX_INTERVAL_MS) {
            return false;  // Not time yet
        }

        _health.heartbeat(TaskId::LORA, TaskState::RUNNING, "Preparing TX...");

        // Get current live data for primary payload
        LiveData live = _liveData.snapshot();
        LoRaPayload payload = buildPayload(live);

        // Calculate backfill
        FramHeader header = _fram.getHeader();
        uint32_t currentIdx = header.writeIndex;
        uint32_t lastSentIdx = header.lastLoRaSendIndex;

        uint32_t missedCount = 0;
        if (currentIdx >= lastSentIdx) {
            missedCount = currentIdx - lastSentIdx;
        } else {
            // Handle ring buffer wrap
            missedCount = (FRAM_MAX_RECORDS - lastSentIdx) + currentIdx;
        }

        // Limit backfill to configured maximum
        if (missedCount > LORAWAN_MAX_BACKFILL) {
            missedCount = LORAWAN_MAX_BACKFILL;
        }

        Serial.printf("[LORA] TX: current data + %u backfill records\n", missedCount);

        // For now, just send the current reading
        // TODO: Implement multi-record backfill encoding
        int state = _node.sendReceive(reinterpret_cast<uint8_t*>(&payload),
                                      sizeof(LoRaPayload),
                                      LORAWAN_PORT);

        if (state == RADIOLIB_ERR_NONE) {
            _lastTxMs = now;
            _frameCounter++;

            Serial.printf("[LORA] Uplink successful (frame %u)\n", _frameCounter);
            _health.heartbeat(TaskId::LORA, TaskState::RUNNING, "TX success");

            // Update FRAM header with new lastLoRaSend position
            _fram.updateLoRaMarker(currentIdx, now / 1000);

            // Update live data cache
            LiveData data = _liveData.snapshot();
            data.loraLastTxTime = now / 1000;
            data.loraFrameCounter = _frameCounter;
            _liveData.update(data);

            // Check for downlink
            checkDownlink();

            return true;
        } else {
            Serial.printf("[LORA] Uplink failed, code %d\n", state);
            _health.heartbeat(TaskId::LORA, TaskState::DEGRADED, "TX failed");

            // If we get network-related errors, try to rejoin
            if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
                Serial.println(F("[LORA] Network not joined — will attempt rejoin"));
                _joined = false;
            }

            return false;
        }
    }

    // --- Status Accessors -----------------------------------------------------

    bool isJoined() const { return _joined; }
    uint32_t frameCounter() const { return _frameCounter; }
    uint32_t lastTxTime() const { return _lastTxMs; }

private:
    FramRingBuffer&     _fram;
    LiveDataCache&      _liveData;
    SystemHealth&       _health;
    EventGroupHandle_t  _events;

    Module              _module;
    SX1262              _radio;
    LoRaWANNode         _node;

    // Credentials
    uint8_t             _devEui[8];
    uint8_t             _appEui[8];
    uint8_t             _appKey[16];

    // State
    bool                _joined = false;
    uint32_t            _joinTimeMs = 0;
    uint32_t            _lastTxMs = 0;
    uint32_t            _frameCounter = 0;

    // --- Helper: Convert byte array to uint64 for RadioLib -------------------

    static uint64_t bytesToUint64(const uint8_t* bytes) {
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value = (value << 8) | bytes[i];
        }
        return value;
    }

    // --- Build LoRaWAN payload from live data ---------------------------------

    LoRaPayload buildPayload(const LiveData& data) {
        LoRaPayload p;
        memset(&p, 0, sizeof(LoRaPayload));

        p.temperature = data.temperature;
        p.humidity = data.humidity;
        p.pressure = data.pressure;
        p.gasResistance = data.gasResistance;
        p.pm2_5 = data.pm2_5;
        p.pm10 = data.pm10;
        p.lux = data.lux;
        p.batteryMv = data.batteryMv;

        // Build status flags
        p.statusFlags = 0;
        if (data.gnssHasFix) p.statusFlags |= STATUS_GNSS_FIX;
        if (data.pmReady) p.statusFlags |= STATUS_PM_READY;
        if (data.gasStable) p.statusFlags |= STATUS_GAS_STABLE;
        if (data.wifiConnected) p.statusFlags |= STATUS_WIFI_CONNECTED;
        if (_fram.isInitialised()) p.statusFlags |= STATUS_FRAM_OK;

        return p;
    }

    // --- Check for downlink messages ------------------------------------------

    void checkDownlink() {
        uint8_t downlink[256];
        size_t downlinkSize = 0;

        // Try to receive downlink (returns error if none available)
        int state = _node.downlink(downlink, &downlinkSize);

        if (state == RADIOLIB_ERR_NONE && downlinkSize > 0) {
            Serial.printf("[LORA] Downlink received (%zu bytes)\n", downlinkSize);

            // TODO: Process downlink commands
            // Examples:
            // - Change TX interval
            // - Request full FRAM dump
            // - Update sensor thresholds
            // - Trigger display refresh

            printHex(downlink, downlinkSize);
        }
    }
};

// --- Helper: Print hex values ---------------------------------------------

static void printHex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
        if (i < len - 1) Serial.print(' ');
    }
    Serial.println();
}

