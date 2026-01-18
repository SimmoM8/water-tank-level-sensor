#include "telemetry_registry.h"
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

// Local string helpers for enums to avoid cross-file deps.
static const char *toString(SenseMode v)
{
    switch (v)
    {
    case SenseMode::TOUCH:
        return "touch";
    case SenseMode::SIM:
        return "simulation";
    default:
        return "unknown";
    }
}

static const char *toString(CalibrationState v)
{
    switch (v)
    {
    case CalibrationState::NEEDS:
        return "needs_calibration";
    case CalibrationState::CALIBRATING:
        return "calibrating";
    case CalibrationState::CALIBRATED:
        return "calibrated";
    default:
        return "unknown";
    }
}

static const char *toString(ProbeQualityReason v)
{
    switch (v)
    {
    case ProbeQualityReason::OK:
        return "ok";
    case ProbeQualityReason::DISCONNECTED_LOW_RAW:
        return "disconnected_low_raw";
    case ProbeQualityReason::UNRELIABLE_SPIKES:
        return "unreliable_spikes";
    case ProbeQualityReason::UNRELIABLE_RAPID:
        return "unreliable_rapid_fluctuation";
    case ProbeQualityReason::UNRELIABLE_STUCK:
        return "unreliable_stuck";
    case ProbeQualityReason::OUT_OF_BOUNDS:
        return "out_of_bounds";
    case ProbeQualityReason::CALIBRATION_RECOMMENDED:
        return "calibration_recommended";
    case ProbeQualityReason::ZERO_HITS:
        return "zero_hits";
    case ProbeQualityReason::UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

static const char *toString(CmdStatus v)
{
    switch (v)
    {
    case CmdStatus::RECEIVED:
        return "received";
    case CmdStatus::APPLIED:
        return "applied";
    case CmdStatus::REJECTED:
        return "rejected";
    case CmdStatus::ERROR:
        return "error";
    default:
        return "unknown";
    }
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

// Telemetry fields (sensors + internal-only writers)
static const TelemetryFieldDef TELEMETRY_FIELDS[] = {
    // Core/meta
    {HaComponent::Internal, "schema", "State Schema", "schema", nullptr, nullptr, nullptr, nullptr, nullptr, write_schema},
    {HaComponent::Internal, "ts", "Timestamp", "ts", nullptr, nullptr, nullptr, nullptr, nullptr, write_ts},
    {HaComponent::Internal, "device", "Device", "device", nullptr, nullptr, nullptr, nullptr, nullptr, write_device},
    {HaComponent::Internal, "wifi", "WiFi", "wifi", nullptr, nullptr, nullptr, nullptr, nullptr, write_wifi},
    {HaComponent::Internal, "mqtt", "MQTT", "mqtt", nullptr, nullptr, nullptr, nullptr, nullptr, write_mqtt},

    // Probe
    {HaComponent::BinarySensor, "probe_connected", "Probe Connected", "probe.connected", "connectivity", nullptr, nullptr, nullptr, nullptr, write_probe_connected},
    {HaComponent::Sensor, "quality", "Probe Quality", "probe.quality", nullptr, nullptr, "mdi:diagnostics", nullptr, nullptr, write_probe_quality},
    {HaComponent::Sensor, "sense_mode", "Probe Sense Mode", "probe.sense_mode", nullptr, nullptr, nullptr, nullptr, nullptr, write_probe_sense_mode},
    {HaComponent::Sensor, "raw", "Probe Raw", "probe.raw", nullptr, nullptr, "mdi:water", nullptr, nullptr, write_probe_raw},
    {HaComponent::Sensor, "raw_valid", "Probe Raw Valid", "probe.raw_valid", nullptr, nullptr, nullptr, nullptr, "raw_valid_bool", write_probe_raw_valid},

    // Calibration
    {HaComponent::Sensor, "calibration_state", "Calibration State", "calibration.state", nullptr, nullptr, "mdi:tune", nullptr, nullptr, write_cal_state},
    {HaComponent::Sensor, "cal_dry", "Calibration Dry", "calibration.dry", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_dry},
    {HaComponent::Sensor, "cal_wet", "Calibration Wet", "calibration.wet", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_wet},
    {HaComponent::Sensor, "cal_inverted", "Calibration Inverted", "calibration.inverted", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_inverted},
    {HaComponent::Sensor, "cal_min_diff", "Calibration Min Diff", "calibration.min_diff", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_min_diff},

    // Level
    {HaComponent::Sensor, "percent", "Level Percent", "level.percent", "humidity", "%", nullptr, nullptr, nullptr, write_level_percent},
    {HaComponent::Sensor, "percent_valid", "Percent Valid", "level.percent_valid", nullptr, nullptr, nullptr, nullptr, "percent_valid_bool", write_level_percent_valid},
    {HaComponent::Sensor, "liters", "Level Liters", "level.liters", nullptr, "L", "mdi:water", nullptr, nullptr, write_level_liters},
    {HaComponent::Sensor, "liters_valid", "Liters Valid", "level.liters_valid", nullptr, nullptr, nullptr, nullptr, "liters_valid_bool", write_level_liters_valid},
    {HaComponent::Sensor, "centimeters", "Level Centimeters", "level.centimeters", nullptr, "cm", "mdi:ruler", nullptr, nullptr, write_level_cm},
    {HaComponent::Sensor, "centimeters_valid", "Centimeters Valid", "level.centimeters_valid", nullptr, nullptr, nullptr, nullptr, "centimeters_valid_bool", write_level_cm_valid},

    // WiFi telemetry exposed as sensor
    {HaComponent::Sensor, "wifi_rssi", "WiFi RSSI", "wifi.rssi", "signal_strength", "dBm", "mdi:wifi", nullptr, nullptr, write_wifi},
    {HaComponent::Sensor, "ip", "IP Address", "wifi.ip", nullptr, nullptr, "mdi:ip-network", nullptr, nullptr, write_wifi},

    // Config (internal only)
    {HaComponent::Internal, "tank_volume_l", "Tank Volume", "config.tank_volume_l", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_volume},
    {HaComponent::Internal, "rod_length_cm", "Rod Length", "config.rod_length_cm", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_rod},
    {HaComponent::Internal, "simulation_enabled", "Simulation Enabled", "config.simulation_enabled", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_sim_enabled},
    {HaComponent::Internal, "simulation_mode", "Simulation Mode", "config.simulation_mode", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_sim_mode},

    // Last command
    {HaComponent::Sensor, "last_cmd", "Last Command", "last_cmd.type", nullptr, nullptr, "mdi:playlist-check", "{{ value_json.last_cmd | tojson }}", "last_cmd", write_last_cmd},
};

// Controls (buttons, numbers, switch, select)
static const int SIM_OPTIONS[] = {0, 1, 2, 3, 4, 5};
static const ControlDef CONTROL_DEFS[] = {
    // Buttons
    {HaComponent::Button, "calibrate_dry", "Calibrate Dry", nullptr, nullptr, nullptr, nullptr, "calibrate", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"dry\"}}", nullptr},
    {HaComponent::Button, "calibrate_wet", "Calibrate Wet", nullptr, nullptr, nullptr, nullptr, "calibrate", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"wet\"}}", nullptr},
    {HaComponent::Button, "clear_calibration", "Clear Calibration", nullptr, nullptr, nullptr, nullptr, "clear_calibration", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"clear_calibration\"}", nullptr},
    {HaComponent::Button, "reannounce", "Re-announce Device", nullptr, nullptr, nullptr, nullptr, "reannounce", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"reannounce\",\"request_id\":\"{{ timestamp }}\"}", nullptr},

    // Numbers
    {HaComponent::Number, "tank_volume_l", "Tank Volume (L)", "config.tank_volume_l", nullptr, nullptr, nullptr, "set_config", "tank_volume_l", 0.0f, 10000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HaComponent::Number, "rod_length_cm", "Rod Length (cm)", "config.rod_length_cm", nullptr, nullptr, nullptr, "set_config", "rod_length_cm", 0.0f, 1000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Switches
    {HaComponent::Switch, "simulation_enabled", "Simulation Enabled", "config.simulation_enabled", nullptr, nullptr, nullptr, "set_simulation", "enabled", 0, 0, 0, nullptr, 0,
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":true}}",
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":false}}",
     nullptr, nullptr, nullptr},

    // Selects
    {HaComponent::Select, "simulation_mode", "Simulation Mode", "config.simulation_mode", nullptr, nullptr, nullptr, "set_simulation", "mode", 0, 0, 0,
     SIM_OPTIONS, sizeof(SIM_OPTIONS) / sizeof(SIM_OPTIONS[0]), nullptr, nullptr,
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"mode\":{{ value }}}}", nullptr, nullptr},
};

const TelemetryFieldDef *telemetry_registry_fields(size_t &count)
{
    count = sizeof(TELEMETRY_FIELDS) / sizeof(TELEMETRY_FIELDS[0]);
    return TELEMETRY_FIELDS;
}

const ControlDef *telemetry_registry_controls(size_t &count)
{
    count = sizeof(CONTROL_DEFS) / sizeof(CONTROL_DEFS[0]);
    return CONTROL_DEFS;
}
