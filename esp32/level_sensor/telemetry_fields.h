#pragma once
#include <stddef.h>
#include <ArduinoJson.h>
#include "device_state.h"

enum class TelemetryComponent : uint8_t
{
    INTERNAL = 0,
    SENSOR,
    BINARY_SENSOR
};

struct TelemetryField
{
    TelemetryComponent component;
    const char *objectId;       // for HA discovery
    const char *name;           // friendly name for discovery
    const char *jsonPath;       // dot path inside state JSON
    const char *deviceClass;    // optional
    const char *unit;           // optional
    const char *icon;           // optional
    const char *attrTemplate;   // optional JSON attributes template
    const char *uniqIdOverride; // optional stable unique_id suffix
    void (*writeFn)(const DeviceState &, JsonObject &root);
};

// Returns pointer to static table of telemetry fields and count via out param.
const TelemetryField *telemetry_getAll(size_t &count);
