#pragma once
#include <stddef.h>
#include <ArduinoJson.h>
#include "device_state.h"

enum class HaComponent : uint8_t
{
    Internal = 0,
    Sensor,
    BinarySensor,
    Number,
    Button,
    Switch,
    Select
};

struct TelemetryFieldDef
{
    HaComponent component;
    const char *objectId;
    const char *name;
    const char *jsonPath;       // dot path inside state JSON
    const char *deviceClass;    // optional
    const char *unit;           // optional
    const char *icon;           // optional
    const char *attrTemplate;   // optional JSON attributes template
    const char *uniqIdOverride; // optional stable unique_id suffix
    void (*writeFn)(const DeviceState &, JsonObject &root);
};

struct ControlDef
{
    HaComponent component; // Number, Button, Switch, Select
    const char *objectId;
    const char *name;
    const char *statePath;   // dot path for val_tpl when applicable
    const char *deviceClass; // optional
    const char *unit;        // optional
    const char *icon;        // optional
    const char *cmdType;     // e.g., "set_config", "set_simulation", "calibrate"
    const char *dataKey;     // primary data key for commands
    float min;
    float max;
    float step;
    const char *const *options;
    size_t optionCount;
    const char *payloadOnJson;   // for switch
    const char *payloadOffJson;  // for switch
    const char *cmdTemplateJson; // for select/custom
    const char *payloadJson;     // for button
    const char *uniqIdOverride;  // optional stable unique_id suffix
};

// Accessors to registry tables.
const TelemetryFieldDef *telemetry_registry_fields(size_t &count);
const ControlDef *telemetry_registry_controls(size_t &count);
