// =============================================================================
// location/geocoder.h — Reverse geocoding via KD-tree (offline only)
// Converts lat/lon coordinates to city/location names using pre-built KD-tree
// =============================================================================
#pragma once

#include <Arduino.h>
#include <cmath>
#include "../config.h"
#include "towns-and-cities.h"

enum class GeocodeSource : uint8_t {
    NONE = 0,
    OFFLINE_KD_TREE,
    COORDINATE_FALLBACK,
};

struct GeocodeMeta {
    GeocodeSource source = GeocodeSource::NONE;
    uint32_t cacheAgeMs = 0;
    uint32_t movementSinceLookupM = 0;
    uint32_t lastLookupMs = 0;
    bool usedCache = false;
};


class Geocoder {
public:
    Geocoder()
        : _lastLookupLat(0), _lastLookupLon(0), _lastLookupMs(0) {
        memset(_cachedName, 0, sizeof(_cachedName));
    }

    // KD-tree based lookup with caching and movement threshold
    bool getLocationName(int32_t lat, int32_t lon, char* nameBuf, size_t nameBufLen,
                         bool allowOnlineQuery = true) {
        (void)allowOnlineQuery;  // Not used with KD-tree, kept for API compatibility

        _lastMeta = GeocodeMeta{};

        // 1) Prefer cached value while movement is below threshold
        if (_cachedName[0] != '\0') {
            const uint32_t moveM = distanceMeters(_lastLookupLat, _lastLookupLon, lat, lon);
            const uint32_t ageMs = millis() - _lastLookupMs;
            _lastMeta.cacheAgeMs = ageMs;
            _lastMeta.movementSinceLookupM = moveM;
            _lastMeta.lastLookupMs = _lastLookupMs;

            if (moveM < GEOCODER_MOVE_THRESHOLD_M) {
                strncpy(nameBuf, _cachedName, nameBufLen - 1);
                nameBuf[nameBufLen - 1] = '\0';
                _lastMeta.source = _cachedSource;
                _lastMeta.usedCache = true;
                return true;
            }
        }

        // 2) KD-tree iterative search for nearest city
        int16_t bestNodeIdx = findNearestCityKDTree(lat, lon);

        if (bestNodeIdx >= 0 && bestNodeIdx < 31099) {
            const CityNode& node = OFFLINE_CITIES[bestNodeIdx];

            // Format: "CityName, CountryName"
            const char* countryName = getCountryName(node.iso);
            snprintf(_cachedName, sizeof(_cachedName), "%s, %s", node.name, countryName ? countryName : node.iso);

            cacheResult(_cachedName, GeocodeSource::OFFLINE_KD_TREE, lat, lon);
            strncpy(nameBuf, _cachedName, nameBufLen - 1);
            nameBuf[nameBufLen - 1] = '\0';
            _lastMeta.source = GeocodeSource::OFFLINE_KD_TREE;
            _lastMeta.lastLookupMs = _lastLookupMs;
            return true;
        }

        // 3) Fallback to coordinates
        snprintf(nameBuf, nameBufLen, "%.5f, %.5f", lat / 1e7, lon / 1e7);
        _lastMeta.source = GeocodeSource::COORDINATE_FALLBACK;
        _lastMeta.lastLookupMs = _lastLookupMs;
        return false;
    }

    GeocodeMeta lastMeta() const {
        return _lastMeta;
    }

    static const char* sourceName(GeocodeSource source) {
        switch (source) {
            case GeocodeSource::OFFLINE_KD_TREE: return "offline";
            case GeocodeSource::COORDINATE_FALLBACK: return "coords";
            default: return "none";
        }
    }

private:
    int32_t _lastLookupLat;
    int32_t _lastLookupLon;
    uint32_t _lastLookupMs;
    char _cachedName[96];
    GeocodeSource _cachedSource = GeocodeSource::NONE;
    GeocodeMeta _lastMeta;

    // Helper: look up country display name from ISO code
    static const char* getCountryName(const char* iso) {
        if (!iso || !iso[0]) return nullptr;
        for (size_t i = 0; i < 249; i++) {
            if (strncmp(COUNTRY_LOOKUP[i].iso, iso, 3) == 0) {
                return COUNTRY_LOOKUP[i].display;
            }
        }
        return nullptr;
    }

    // Calculate distance in meters between two lat/lon points (scaled by 1e7)
    static uint32_t distanceMeters(int32_t latA, int32_t lonA, int32_t latB, int32_t lonB) {
        const double lat1 = (latA / 1e7) * (M_PI / 180.0);
        const double lon1 = (lonA / 1e7) * (M_PI / 180.0);
        const double lat2 = (latB / 1e7) * (M_PI / 180.0);
        const double lon2 = (lonB / 1e7) * (M_PI / 180.0);

        const double x = (lon2 - lon1) * cos((lat1 + lat2) * 0.5);
        const double y = (lat2 - lat1);
        const double earthR = 6371000.0;
        return static_cast<uint32_t>(sqrt(x * x + y * y) * earthR);
    }

    // Iterative KD-tree nearest neighbor search using manual stack
    // Root is at index (OFFLINE_CITIES array size - 1)
    int16_t findNearestCityKDTree(int32_t targetLat, int32_t targetLon) {
        constexpr int CITY_COUNT = 31099;
        constexpr int MAX_DEPTH = 64;

        int16_t bestIdx = -1;
        double bestDistSq = 1e18;  // Very large initial value

        // Manual stack for iterative DFS
        struct StackNode {
            int16_t idx;
            int depth;
        };
        StackNode stack[MAX_DEPTH];
        int stackPtr = 0;

        // Root is the last element (as per the reference code)
        stack[stackPtr++] = { static_cast<int16_t>(CITY_COUNT - 1), 0 };

        while (stackPtr > 0) {
            StackNode current = stack[--stackPtr];

            if (current.idx < 0 || current.idx >= CITY_COUNT) {
                continue;
            }

            const CityNode& node = OFFLINE_CITIES[current.idx];

            // 1. Calculate squared distance
            double dLat = static_cast<double>(targetLat - node.lat);
            double dLon = static_cast<double>(targetLon - node.lon);
            double distSq = dLat * dLat + dLon * dLon;

            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                bestIdx = current.idx;
            }

            // 2. Determine split axis based on depth (lat on even, lon on odd)
            int32_t targetVal = (current.depth % 2 == 0) ? targetLat : targetLon;
            int32_t nodeVal = (current.depth % 2 == 0) ? node.lat : node.lon;

            // 3. Decide which side is closer
            int16_t nextIdx = (targetVal < nodeVal) ? node.left : node.right;
            int16_t otherIdx = (targetVal < nodeVal) ? node.right : node.left;

            // 4. Only explore "other" side if it could contain a closer point
            double splitDistSq = static_cast<double>(targetVal - nodeVal);
            splitDistSq = splitDistSq * splitDistSq;

            if (splitDistSq < bestDistSq && otherIdx >= 0) {
                if (stackPtr < MAX_DEPTH) {
                    stack[stackPtr++] = { otherIdx, current.depth + 1 };
                }
            }

            // 5. Push closer side last so it's processed first (LIFO)
            if (nextIdx >= 0) {
                if (stackPtr < MAX_DEPTH) {
                    stack[stackPtr++] = { nextIdx, current.depth + 1 };
                }
            }
        }

        return bestIdx;
    }

    void cacheResult(const char* name, GeocodeSource source, int32_t lat, int32_t lon) {
        strncpy(_cachedName, name, sizeof(_cachedName) - 1);
        _cachedName[sizeof(_cachedName) - 1] = '\0';
        _cachedSource = source;
        _lastLookupLat = lat;
        _lastLookupLon = lon;
        _lastLookupMs = millis();
    }
};
