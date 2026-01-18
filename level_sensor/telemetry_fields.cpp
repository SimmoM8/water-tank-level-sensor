#include "telemetry_fields.h"
#include <string.h>

// Helper: navigate/create nested objects for a dotted path and return a JsonVariant to assign.
static JsonVariant ensurePath(JsonObject root, const char *path)
{
    JsonVariant current = root;
    const char *p = path;
    while (p && *p)
    {
        const char *dot = strchr(p, '.');
        char key[32];
        size_t len = dot ? (size_t)(dot - p) : strlen(p);
        if (len >= sizeof(key))
            len = sizeof(key) - 1;
        memcpy(key, p, len);
        key[len] = '\0';
        current = current.to<JsonObject>()[key];
        if (!dot)
            break;
        p = dot + 1;
    }
    return current;
}

// Writers
static void write_schema(const DeviceState &, JsonObject &root)
{
    ensurePath(root, "schema").set(STATE_SCHEMA_VERSION);
}

static void write_ts(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "ts").set(s.ts);
}

static void write_device(const DeviceState &s, JsonObject &root)
{
    JsonVariant dev = ensurePath(root, "device");
    JsonObject o = dev.to<JsonObject>();
    o["id"] = s.device.id;
    o["name"] = s.device.name;
    o["fw"] = s.device.fw;
}

static void write_wifi(const DeviceState &s, JsonObject &root)
{
    JsonVariant wifi = ensurePath(root, "wifi");
    JsonObject o = wifi.to<JsonObject>();
    o["rssi"] = s.wifi.rssi;
    o["ip"] = s.wifi.ip;
}

static void write_mqtt(const DeviceState &s, JsonObject &root)
{
    JsonVariant mqtt = ensurePath(root, "mqtt");
    JsonObject o = mqtt.to<JsonObject>();
    o["connected"] = s.mqtt.connected;
}

static void write_probe_connected(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "probe.connected").set(s.probe.connected);
}

static void write_probe_quality(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "probe.quality").set(toString(s.probe.quality));
}

static void write_probe_sense_mode(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "probe.sense_mode").set(toString(s.probe.senseMode));
}

static void write_probe_raw(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "probe.raw").set(s.probe.raw);
}

static void write_probe_raw_valid(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "probe.raw_valid").set(s.probe.rawValid);
}

static void write_cal_state(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "calibration.state").set(toString(s.calibration.state));
}

static void write_cal_dry(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "calibration.dry").set(s.calibration.dry);
}

static void write_cal_wet(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "calibration.wet").set(s.calibration.wet);
}

static void write_cal_inverted(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "calibration.inverted").set(s.calibration.inverted);
}

static void write_cal_min_diff(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "calibration.min_diff").set(s.calibration.minDiff);
}

static void write_level_percent(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.percent").set(s.level.percent);
}

static void write_level_percent_valid(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.percent_valid").set(s.level.percentValid);
}

static void write_level_liters(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.liters").set(s.level.liters);
}

static void write_level_liters_valid(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.liters_valid").set(s.level.litersValid);
}

static void write_level_cm(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.centimeters").set(s.level.centimeters);
}

static void write_level_cm_valid(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "level.centimeters_valid").set(s.level.centimetersValid);
}

static void write_config_volume(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "config.tank_volume_l").set(s.config.tankVolumeLiters);
}

static void write_config_rod(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "config.rod_length_cm").set(s.config.rodLengthCm);
}

static void write_config_sim_enabled(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "config.simulation_enabled").set(s.config.simulationEnabled);
}

static void write_config_sim_mode(const DeviceState &s, JsonObject &root)
{
    ensurePath(root, "config.simulation_mode").set(s.config.simulationMode);
}

static void write_last_cmd(const DeviceState &s, JsonObject &root)
{
    JsonVariant v = ensurePath(root, "last_cmd");
    JsonObject o = v.to<JsonObject>();
    o["request_id"] = s.lastCmd.requestId;
    o["type"] = s.lastCmd.type;
    o["status"] = toString(s.lastCmd.status);
    o["message"] = s.lastCmd.message;
    o["ts"] = s.lastCmd.ts;
}

static const TelemetryField TELEMETRY_FIELDS[] = {
    // Core/meta
    {TelemetryComponent::INTERNAL, "schema", "State Schema", "schema", nullptr, nullptr, nullptr, nullptr, nullptr, write_schema},
    {TelemetryComponent::INTERNAL, "ts", "Timestamp", "ts", nullptr, nullptr, nullptr, nullptr, nullptr, write_ts},
    {TelemetryComponent::INTERNAL, "device", "Device", "device", nullptr, nullptr, nullptr, nullptr, nullptr, write_device},
    {TelemetryComponent::INTERNAL, "wifi", "WiFi", "wifi", nullptr, nullptr, nullptr, nullptr, nullptr, write_wifi},
    {TelemetryComponent::INTERNAL, "mqtt", "MQTT", "mqtt", nullptr, nullptr, nullptr, nullptr, nullptr, write_mqtt},

    // Probe
    {TelemetryComponent::BINARY_SENSOR, "probe_connected", "Probe Connected", "probe.connected", "connectivity", nullptr, nullptr, nullptr, nullptr, write_probe_connected},
    {TelemetryComponent::SENSOR, "quality", "Probe Quality", "probe.quality", nullptr, nullptr, "mdi:diagnostics", nullptr, nullptr, write_probe_quality},
    {TelemetryComponent::SENSOR, "sense_mode", "Probe Sense Mode", "probe.sense_mode", nullptr, nullptr, nullptr, nullptr, nullptr, write_probe_sense_mode},
    {TelemetryComponent::SENSOR, "raw", "Probe Raw", "probe.raw", nullptr, nullptr, "mdi:water", nullptr, nullptr, write_probe_raw},
    {TelemetryComponent::SENSOR, "raw_valid", "Probe Raw Valid", "probe.raw_valid", nullptr, nullptr, nullptr, nullptr, "raw_valid_bool", write_probe_raw_valid},

    // Calibration
    {TelemetryComponent::SENSOR, "calibration_state", "Calibration State", "calibration.state", nullptr, nullptr, "mdi:tune", nullptr, nullptr, write_cal_state},
    {TelemetryComponent::SENSOR, "cal_dry", "Calibration Dry", "calibration.dry", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_dry},
    {TelemetryComponent::SENSOR, "cal_wet", "Calibration Wet", "calibration.wet", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_wet},
    {TelemetryComponent::SENSOR, "cal_inverted", "Calibration Inverted", "calibration.inverted", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_inverted},
    {TelemetryComponent::SENSOR, "cal_min_diff", "Calibration Min Diff", "calibration.min_diff", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_min_diff},

    // Level
    {TelemetryComponent::SENSOR, "percent", "Level Percent", "level.percent", "humidity", "%", nullptr, nullptr, nullptr, write_level_percent},
    {TelemetryComponent::SENSOR, "percent_valid", "Percent Valid", "level.percent_valid", nullptr, nullptr, nullptr, nullptr, "percent_valid_bool", write_level_percent_valid},
    {TelemetryComponent::SENSOR, "liters", "Level Liters", "level.liters", nullptr, "L", "mdi:water", nullptr, nullptr, write_level_liters},
    {TelemetryComponent::SENSOR, "liters_valid", "Liters Valid", "level.liters_valid", nullptr, nullptr, nullptr, nullptr, "liters_valid_bool", write_level_liters_valid},
    {TelemetryComponent::SENSOR, "centimeters", "Level Centimeters", "level.centimeters", nullptr, "cm", "mdi:ruler", nullptr, nullptr, write_level_cm},
    {TelemetryComponent::SENSOR, "centimeters_valid", "Centimeters Valid", "level.centimeters_valid", nullptr, nullptr, nullptr, nullptr, "centimeters_valid_bool", write_level_cm_valid},

    // WiFi telemetry exposed as sensor
    {TelemetryComponent::SENSOR, "wifi_rssi", "WiFi RSSI", "wifi.rssi", "signal_strength", "dBm", "mdi:wifi", nullptr, nullptr, write_wifi},
    {TelemetryComponent::SENSOR, "ip", "IP Address", "wifi.ip", nullptr, nullptr, "mdi:ip-network", nullptr, nullptr, write_wifi},

    // Config (internal only)
    {TelemetryComponent::INTERNAL, "tank_volume_l", "Tank Volume", "config.tank_volume_l", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_volume},
    {TelemetryComponent::INTERNAL, "rod_length_cm", "Rod Length", "config.rod_length_cm", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_rod},
    {TelemetryComponent::INTERNAL, "simulation_enabled", "Simulation Enabled", "config.simulation_enabled", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_sim_enabled},
    {TelemetryComponent::INTERNAL, "simulation_mode", "Simulation Mode", "config.simulation_mode", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_sim_mode},

    // Last command
    {TelemetryComponent::SENSOR, "last_cmd", "Last Command", "last_cmd.type", nullptr, nullptr, "mdi:playlist-check", "{{ value_json.last_cmd | tojson }}", "last_cmd", write_last_cmd},
};

const TelemetryField *telemetry_getAll(size_t &count)
{
    count = sizeof(TELEMETRY_FIELDS) / sizeof(TELEMETRY_FIELDS[0]);
    return TELEMETRY_FIELDS;
}
