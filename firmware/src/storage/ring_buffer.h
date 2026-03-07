// =============================================================================
// storage/ring_buffer.h — FRAM-backed circular buffer with LoRa backfill
// =============================================================================
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "../config.h"

// =============================================================================
// Low-level FRAM SPI commands (Cypress CY15B108QN / compatible)
// =============================================================================
namespace FramCmd {
    constexpr uint8_t WREN  = 0x06;  // Write enable
    constexpr uint8_t WRDI  = 0x04;  // Write disable
    constexpr uint8_t READ  = 0x03;  // Read memory
    constexpr uint8_t WRITE = 0x02;  // Write memory
    constexpr uint8_t RDSR  = 0x05;  // Read status register
    constexpr uint8_t WRSR  = 0x01;  // Write status register
    constexpr uint8_t RDID  = 0x9F;  // Read device ID
}

// =============================================================================
// FRAM Ring Buffer
// =============================================================================

class FramRingBuffer {
public:
    FramRingBuffer(uint8_t csPin, SPIClass& spi = SPI)
        : _csPin(csPin), _spi(spi), _initialised(false) {}

    // --- Lifecycle -----------------------------------------------------------

    bool init() {
        pinMode(_csPin, OUTPUT);
        digitalWrite(_csPin, HIGH);

        // Verify FRAM is present by reading device ID
        uint8_t mfgId[3];
        _readId(mfgId);
        Serial.printf("[FRAM] Device ID: %02X %02X %02X\n",
                      mfgId[0], mfgId[1], mfgId[2]);

        // Read existing header
        _readBytes(0, reinterpret_cast<uint8_t*>(&_header), sizeof(FramHeader));

        if (_header.magic != FRAM_MAGIC) {
            Serial.println(F("[FRAM] No valid header found — formatting"));
            format();
        } else {
            Serial.printf("[FRAM] Valid header: %lu records, writeIdx=%lu, "
                          "lastLoRaSend=%lu\n",
                          _header.recordCount, _header.writeIndex,
                          _header.lastLoRaSendIndex);
        }

        _initialised = true;
        return true;
    }

    void format() {
        memset(&_header, 0, sizeof(FramHeader));
        _header.magic = FRAM_MAGIC;
        _header.writeIndex = 0;
        _header.recordCount = 0;
        _header.totalWrites = 0;
        _header.lastLoRaSendIndex = 0;
        _header.lastLoRaSendTime = 0;
        _writeHeader();
        Serial.println(F("[FRAM] Formatted"));
    }

    // --- Record Operations ---------------------------------------------------

    bool writeRecord(const SensorRecord& record) {
        if (!_initialised) return false;

        uint32_t addr = _recordAddress(_header.writeIndex);
        _writeBytes(addr, reinterpret_cast<const uint8_t*>(&record),
                    sizeof(SensorRecord));

        _header.writeIndex = (_header.writeIndex + 1) % FRAM_MAX_RECORDS;
        if (_header.recordCount < FRAM_MAX_RECORDS) {
            _header.recordCount++;
        }
        _header.totalWrites++;
        _writeHeader();

        return true;
    }

    bool readRecord(uint32_t index, SensorRecord& record) const {
        if (!_initialised || index >= FRAM_MAX_RECORDS) return false;

        uint32_t addr = _recordAddress(index);
        _readBytes(addr, reinterpret_cast<uint8_t*>(&record),
                   sizeof(SensorRecord));
        return true;
    }

    // --- Query Operations ----------------------------------------------------

    // Get the N most recent records (oldest first)
    uint32_t getLatestRecords(SensorRecord* buffer, uint32_t maxCount) const {
        if (!_initialised || _header.recordCount == 0) return 0;

        uint32_t count = min(maxCount, _header.recordCount);
        uint32_t startIdx;

        if (_header.recordCount <= count) {
            // Buffer hasn't wrapped or we want everything
            startIdx = (_header.writeIndex + FRAM_MAX_RECORDS - _header.recordCount)
                       % FRAM_MAX_RECORDS;
        } else {
            startIdx = (_header.writeIndex + FRAM_MAX_RECORDS - count)
                       % FRAM_MAX_RECORDS;
        }

        for (uint32_t i = 0; i < count; i++) {
            uint32_t idx = (startIdx + i) % FRAM_MAX_RECORDS;
            readRecord(idx, buffer[i]);
        }

        return count;
    }

    // Get records since a specific ring buffer index (for LoRa backfill)
    uint32_t getRecordsSince(uint32_t sinceIndex, SensorRecord* buffer,
                             uint32_t maxCount) const {
        if (!_initialised || _header.recordCount == 0) return 0;

        // Calculate how many records have been written since sinceIndex
        uint32_t pending = pendingLoRaRecords();
        uint32_t count = min(maxCount, pending);

        for (uint32_t i = 0; i < count; i++) {
            uint32_t idx = (sinceIndex + i) % FRAM_MAX_RECORDS;
            readRecord(idx, buffer[i]);
        }

        return count;
    }

    // --- LoRa Backfill Management --------------------------------------------

    uint32_t pendingLoRaRecords() const {
        if (_header.recordCount == 0) return 0;

        // If the write index has wrapped past the send index, calculate
        // correctly using modular arithmetic
        if (_header.writeIndex >= _header.lastLoRaSendIndex) {
            return _header.writeIndex - _header.lastLoRaSendIndex;
        } else {
            return (FRAM_MAX_RECORDS - _header.lastLoRaSendIndex)
                   + _header.writeIndex;
        }
    }

    void advanceLoRaSendIndex(uint32_t count) {
        _header.lastLoRaSendIndex =
            (_header.lastLoRaSendIndex + count) % FRAM_MAX_RECORDS;
        _header.lastLoRaSendTime = millis() / 1000;  // seconds for compactness
        _writeHeader();
    }

    uint32_t getLastLoRaSendIndex() const { return _header.lastLoRaSendIndex; }
    uint32_t getLastLoRaSendTime() const  { return _header.lastLoRaSendTime; }

    // --- Status --------------------------------------------------------------

    uint32_t recordCount() const     { return _header.recordCount; }
    uint32_t totalWrites() const     { return _header.totalWrites; }
    uint32_t capacity() const        { return FRAM_MAX_RECORDS; }
    float    usagePercent() const {
        return (_header.recordCount * 100.0f) / FRAM_MAX_RECORDS;
    }
    bool     isInitialised() const   { return _initialised; }

private:
    uint8_t         _csPin;
    SPIClass&       _spi;
    bool            _initialised;
    FramHeader      _header;

    uint32_t _recordAddress(uint32_t index) const {
        return FRAM_HEADER_SIZE + (index * FRAM_RECORD_SIZE);
    }

    void _writeHeader() {
        _writeBytes(0, reinterpret_cast<const uint8_t*>(&_header),
                    sizeof(FramHeader));
    }

    // --- Low-level SPI FRAM access -------------------------------------------

    void _select() const   { digitalWrite(_csPin, LOW); }
    void _deselect() const { digitalWrite(_csPin, HIGH); }

    void _writeEnable() const {
        _select();
        _spi.transfer(FramCmd::WREN);
        _deselect();
    }

    void _readBytes(uint32_t addr, uint8_t* buffer, size_t len) const {
        _select();
        _spi.transfer(FramCmd::READ);
        _spi.transfer((addr >> 16) & 0xFF);  // 3-byte address for 1MB
        _spi.transfer((addr >> 8) & 0xFF);
        _spi.transfer(addr & 0xFF);
        for (size_t i = 0; i < len; i++) {
            buffer[i] = _spi.transfer(0x00);
        }
        _deselect();
    }

    void _writeBytes(uint32_t addr, const uint8_t* data, size_t len) {
        _writeEnable();
        _select();
        _spi.transfer(FramCmd::WRITE);
        _spi.transfer((addr >> 16) & 0xFF);
        _spi.transfer((addr >> 8) & 0xFF);
        _spi.transfer(addr & 0xFF);
        for (size_t i = 0; i < len; i++) {
            _spi.transfer(data[i]);
        }
        _deselect();
    }

    void _readId(uint8_t* id) const {
        _select();
        _spi.transfer(FramCmd::RDID);
        id[0] = _spi.transfer(0x00);
        id[1] = _spi.transfer(0x00);
        id[2] = _spi.transfer(0x00);
        _deselect();
    }
};
