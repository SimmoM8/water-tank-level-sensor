#include "telemetry_registry.h"
#include <string.h>
#include "domain_strings.h"

// Split dotted path into parent path and leaf key.
static void splitPath(const char *full, char *parentOut, size_t parentLen, char *leafOut, size_t leafLen)
{
    const char *dot = strrchr(full, '.');
    if (!dot)
    {
        parentOut[0] = '\0';
        strncpy(leafOut, full, leafLen);
        leafOut[leafLen - 1] = '\0';
        return;
    }

    size_t parentSize = (size_t)(dot - full);
    if (parentSize >= parentLen)
        parentSize = parentLen - 1;
    memcpy(parentOut, full, parentSize);
    parentOut[parentSize] = '\0';

    strncpy(leafOut, dot + 1, leafLen);
    leafOut[leafLen - 1] = '\0';
}

// Ensure a JsonObject exists at the given dotted path; return that object.
static JsonObject ensureObject(JsonObject root, const char *path)
{
    JsonObject obj = root;
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

        JsonVariant child = obj[key];
        if (!child.is<JsonObject>())
        {
            child = obj.createNestedObject(key);
        }
        obj = child.as<JsonObject>();

        if (!dot)
            break;
        p = dot + 1;
    }

    return obj;
}

// Writers
static void write_schema(const DeviceState &, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("schema", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    obj[leaf] = STATE_SCHEMA_VERSION;
}

static void write_ts(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ts", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    obj[leaf] = s.ts;
}

static void write_device(const DeviceState &s, JsonObject &root)
{
    JsonObject o = ensureObject(root, "device");
    o["id"] = s.device.id;
    o["name"] = s.device.name;
    o["fw"] = s.device.fw;
}

static void write_wifi(const DeviceState &s, JsonObject &root)
{
    JsonObject o = ensureObject(root, "wifi");
    o["rssi"] = s.wifi.rssi;
    o["ip"] = s.wifi.ip;
}

static void write_mqtt(const DeviceState &s, JsonObject &root)
{
    JsonObject o = ensureObject(root, "mqtt");
    o["connected"] = s.mqtt.connected;
}

static void write_probe_connected(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("probe.connected", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.probe.connected;
}

static void write_probe_quality(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("probe.quality", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = toString(s.probe.quality);
}

static void write_probe_raw(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("probe.raw", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.probe.raw;
}

static void write_probe_raw_valid(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("probe.raw_valid", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.probe.rawValid;
}

static void write_cal_state(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("calibration.state", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = toString(s.calibration.state);
}

static void write_cal_dry(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("calibration.dry", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.calibration.dry;
}

static void write_cal_wet(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("calibration.wet", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.calibration.wet;
}

static void write_cal_inverted(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("calibration.inverted", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.calibration.inverted;
}

static void write_cal_min_diff(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("calibration.min_diff", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.calibration.minDiff;
}

static void write_level_percent(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.percent", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.percent;
}

static void write_level_percent_valid(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.percent_valid", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.percentValid;
}

static void write_level_liters(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.liters", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.liters;
}

static void write_level_liters_valid(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.liters_valid", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.litersValid;
}

static void write_level_cm(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.centimeters", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.centimeters;
}

static void write_level_cm_valid(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("level.centimeters_valid", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.level.centimetersValid;
}

static void write_config_volume(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("config.tank_volume_l", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.config.tankVolumeLiters;
}

static void write_config_rod(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("config.rod_length_cm", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.config.rodLengthCm;
}

static void write_config_sense_mode(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("config.sense_mode", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = toString(s.config.senseMode);
}

static void write_config_sim_mode(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("config.simulation_mode", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = ensureObject(root, parent);
    obj[leaf] = s.config.simulationMode;
}

static const char *ota_state_label(OtaStatus st)
{
    switch (st)
    {
    case OtaStatus::IDLE:
        return "idle";
    case OtaStatus::DOWNLOADING:
        return "downloading";
    case OtaStatus::VERIFYING:
        return "verifying";
    case OtaStatus::APPLYING:
        return "applying";
    case OtaStatus::REBOOTING:
        return "applying";
    case OtaStatus::SUCCESS:
        return "success";
    case OtaStatus::ERROR:
        return "failed";
    default:
        return "idle";
    }
}

static void write_fw_version(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("fw_version", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    const char *fw = s.fw_version[0] ? s.fw_version : s.device.fw;
    obj[leaf] = fw ? fw : "";
}

static void write_installed_version(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("installed_version", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    const char *fw = s.fw_version[0] ? s.fw_version : s.device.fw;
    obj[leaf] = fw ? fw : "";
}

static void write_latest_version(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("latest_version", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    obj[leaf] = s.ota_target_version;
}

static void write_update_available(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("update_available", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    obj[leaf] = s.update_available;
}

static void write_ota_state_flat(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ota_state", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    const char *state = s.ota_state[0] ? s.ota_state : ota_state_label(s.ota.status);
    obj[leaf] = state;
}

static void write_ota_progress_flat(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ota_progress", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    obj[leaf] = s.ota.progress;
}

static void write_ota_error_flat(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ota_error", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    if (s.ota_error[0])
    {
        obj[leaf] = s.ota_error;
        return;
    }
    obj[leaf] = (s.ota.status == OtaStatus::ERROR) ? s.ota.last_message : "";
}

static void write_ota_target_version_flat(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ota_target_version", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    const char *v = s.ota_target_version[0] ? s.ota_target_version : s.ota.version;
    obj[leaf] = v ? v : "";
}

static void write_ota_last_ts_flat(const DeviceState &s, JsonObject &root)
{
    char parent[32];
    char leaf[32];
    splitPath("ota_last_ts", parent, sizeof(parent), leaf, sizeof(leaf));
    JsonObject obj = (parent[0] == '\0') ? root : ensureObject(root, parent);
    uint32_t ts = s.ota_last_ts;
    if (ts == 0)
    {
        ts = s.ota.completed_ts ? s.ota.completed_ts : s.ota.started_ts;
    }
    obj[leaf] = ts;
}

static void write_ota_status(const DeviceState &s, JsonObject &root)
{
    JsonObject ota = ensureObject(root, "ota");
    ota["status"] = toString(s.ota.status);
}

static void write_ota_progress(const DeviceState &s, JsonObject &root)
{
    JsonObject ota = ensureObject(root, "ota");
    ota["progress"] = s.ota.progress;
}

static void write_ota_active(const DeviceState &s, JsonObject &root)
{
    JsonObject active = ensureObject(root, "ota.active");

    active["request_id"] = s.ota.request_id;
    active["version"] = s.ota.version;
    active["url"] = s.ota.url;
    active["sha256"] = s.ota.sha256;
    active["started_ts"] = s.ota.started_ts;
}

static void write_ota_result(const DeviceState &s, JsonObject &root)
{
    JsonObject result = ensureObject(root, "ota.result");

    result["status"] = s.ota.last_status;
    result["message"] = s.ota.last_message;
    result["completed_ts"] = s.ota.completed_ts;
}

static void write_last_cmd(const DeviceState &s, JsonObject &root)
{
    JsonObject o = ensureObject(root, "last_cmd");
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
    {HaComponent::Sensor, "fw_version", "Firmware Version", "fw_version", nullptr, nullptr, "mdi:chip", nullptr, nullptr, write_fw_version},
    {HaComponent::Internal, "installed_version", "Installed Version", "installed_version", nullptr, nullptr, nullptr, nullptr, nullptr, write_installed_version},
    {HaComponent::Internal, "latest_version", "Latest Version", "latest_version", nullptr, nullptr, nullptr, nullptr, nullptr, write_latest_version},
    {HaComponent::Internal, "update_available", "Update Available", "update_available", nullptr, nullptr, nullptr, nullptr, nullptr, write_update_available},
    {HaComponent::Internal, "wifi", "WiFi", "wifi", nullptr, nullptr, nullptr, nullptr, nullptr, write_wifi},
    {HaComponent::Internal, "mqtt", "MQTT", "mqtt", nullptr, nullptr, nullptr, nullptr, nullptr, write_mqtt},

    // Probe
    {HaComponent::BinarySensor, "probe_connected", "Probe Connected", "probe.connected", "connectivity", nullptr, nullptr, nullptr, nullptr, write_probe_connected},
    {HaComponent::Sensor, "quality", "Probe Quality", "probe.quality", nullptr, nullptr, "mdi:diagnostics", nullptr, nullptr, write_probe_quality},
    {HaComponent::Sensor, "raw", "Probe Raw", "probe.raw", nullptr, "ticks", "mdi:water", nullptr, nullptr, write_probe_raw},
    {HaComponent::BinarySensor, "raw_valid", "Probe Raw Valid", "probe.raw_valid", nullptr, nullptr, nullptr, nullptr, nullptr, write_probe_raw_valid},

    // Calibration
    {HaComponent::Sensor, "calibration_state", "Calibration State", "calibration.state", nullptr, nullptr, "mdi:tune", nullptr, nullptr, write_cal_state},
    {HaComponent::Sensor, "cal_dry", "Calibration Dry", "calibration.dry", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_dry},
    {HaComponent::Sensor, "cal_wet", "Calibration Wet", "calibration.wet", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_wet},
    {HaComponent::Sensor, "cal_inverted", "Calibration Inverted", "calibration.inverted", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_inverted},
    {HaComponent::Sensor, "cal_min_diff", "Calibration Min Diff", "calibration.min_diff", nullptr, nullptr, nullptr, nullptr, nullptr, write_cal_min_diff},

    // Level
    {HaComponent::Sensor, "percent", "Level Percent", "level.percent", "humidity", "%", nullptr, nullptr, nullptr, write_level_percent},
    {HaComponent::Sensor, "liters", "Level Liters", "level.liters", nullptr, "L", "mdi:water", nullptr, nullptr, write_level_liters},
    {HaComponent::Sensor, "centimeters", "Level Centimeters", "level.centimeters", nullptr, "cm", "mdi:ruler", nullptr, nullptr, write_level_cm},
    {HaComponent::BinarySensor, "percent_valid", "Percent Valid", "level.percent_valid", nullptr, nullptr, nullptr, nullptr, nullptr, write_level_percent_valid},
    {HaComponent::BinarySensor, "liters_valid", "Liters Valid", "level.liters_valid", nullptr, nullptr, nullptr, nullptr, nullptr, write_level_liters_valid},
    {HaComponent::BinarySensor, "centimeters_valid", "Centimeters Valid", "level.centimeters_valid", nullptr, nullptr, nullptr, nullptr, nullptr, write_level_cm_valid},

    // WiFi telemetry exposed as sensor
    {HaComponent::Sensor, "wifi_rssi", "WiFi RSSI", "wifi.rssi", "signal_strength", "dBm", "mdi:wifi", nullptr, nullptr, write_wifi},
    {HaComponent::Sensor, "ip", "IP Address", "wifi.ip", nullptr, nullptr, "mdi:ip-network", nullptr, nullptr, write_wifi},

    // Config (internal only)
    {HaComponent::Internal, "tank_volume_l", "Tank Volume", "config.tank_volume_l", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_volume},
    {HaComponent::Internal, "rod_length_cm", "Rod Length", "config.rod_length_cm", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_rod},
    {HaComponent::Internal, "sense_mode", "Sense Mode", "config.sense_mode", nullptr, nullptr, "mdi:toggle-switch", nullptr, nullptr, write_config_sense_mode},
    {HaComponent::Internal, "simulation_mode", "Simulation Mode", "config.simulation_mode", nullptr, nullptr, nullptr, nullptr, nullptr, write_config_sim_mode},

    // OTA (flat telemetry for HA)
    {HaComponent::Sensor, "ota_state", "OTA State", "ota_state", nullptr, nullptr, "mdi:update", nullptr, nullptr, write_ota_state_flat},
    {HaComponent::Sensor, "ota_progress", "OTA Progress", "ota_progress", nullptr, "%", "mdi:progress-download", nullptr, nullptr, write_ota_progress_flat},
    {HaComponent::Sensor, "ota_error", "OTA Error", "ota_error", nullptr, nullptr, "mdi:alert-circle-outline", nullptr, nullptr, write_ota_error_flat},
    {HaComponent::Sensor, "ota_target_version", "OTA Target Version", "ota_target_version", nullptr, nullptr, "mdi:tag-outline", nullptr, nullptr, write_ota_target_version_flat},
    {HaComponent::Sensor, "ota_last_ts", "OTA Last Timestamp", "ota_last_ts", "timestamp", nullptr, "mdi:clock-outline", nullptr, nullptr, write_ota_last_ts_flat},

    // OTA (internal state)
    {HaComponent::Internal, "ota_status", "OTA Status", "ota.status", nullptr, nullptr, nullptr, nullptr, nullptr, write_ota_status},
    {HaComponent::Internal, "ota_progress", "OTA Progress", "ota.progress", nullptr, nullptr, nullptr, nullptr, nullptr, write_ota_progress},
    {HaComponent::Internal, "ota_active", "OTA Active", "ota.active", nullptr, nullptr, nullptr, nullptr, nullptr, write_ota_active},
    {HaComponent::Internal, "ota_result", "OTA Result", "ota.result", nullptr, nullptr, nullptr, nullptr, nullptr, write_ota_result},

    // Last command
    {HaComponent::Sensor, "last_cmd", "Last Command", "last_cmd.type", nullptr, nullptr, "mdi:playlist-check", "{{ value_json.last_cmd | tojson }}", "last_cmd", write_last_cmd},
};

// Controls (buttons, numbers, switch, select)
static const char *const SIM_OPTIONS[] = {"0", "1", "2", "3", "4", "5"};
static const char *const SENSE_OPTIONS[] = {"touch", "sim"};
static const ControlDef CONTROL_DEFS[] = {
    // Buttons
    {HaComponent::Button, "calibrate_dry", "Calibrate Dry", nullptr, nullptr, nullptr, nullptr, "calibrate", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"dry\"}}", nullptr},
    {HaComponent::Button, "calibrate_wet", "Calibrate Wet", nullptr, nullptr, nullptr, nullptr, "calibrate", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"wet\"}}", nullptr},
    {HaComponent::Button, "clear_calibration", "Clear Calibration", nullptr, nullptr, nullptr, nullptr, "clear_calibration", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"clear_calibration\"}", nullptr},
    {HaComponent::Button, "wipe_wifi", "Wipe WiFi Credentials", nullptr, nullptr, nullptr, "mdi:wifi-remove", "wipe_wifi", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"wipe_wifi\",\"request_id\":\"{{ timestamp }}\"}", nullptr},
    {HaComponent::Button, "reannounce", "Re-announce Device", nullptr, nullptr, nullptr, nullptr, "reannounce", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"reannounce\",\"request_id\":\"{{ timestamp }}\"}", nullptr},
    {HaComponent::Button, "ota_pull", "OTA Pull", nullptr, nullptr, nullptr, "mdi:update", "ota_pull", nullptr, 0, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, "{\"schema\":1,\"type\":\"ota_pull\",\"request_id\":\"{{ timestamp }}\",\"data\":{}}", "ota_pull"},

    // Numbers
    {HaComponent::Number, "tank_volume_l", "Tank Volume (L)", "config.tank_volume_l", nullptr, nullptr, nullptr, "set_config", "tank_volume_l", 0.0f, 10000000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HaComponent::Number, "rod_length_cm", "Rod Length (cm)", "config.rod_length_cm", nullptr, nullptr, nullptr, "set_config", "rod_length_cm", 0.0f, 10000000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HaComponent::Number, "cal_dry_set", "Set Calibration Dry", "calibration.dry", nullptr, nullptr, nullptr, "set_calibration", "cal_dry_set", 0.0f, 10000000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},
    {HaComponent::Number, "cal_wet_set", "Set Calibration Wet", "calibration.wet", nullptr, nullptr, nullptr, "set_calibration", "cal_wet_set", 0.0f, 10000000.0f, 1.0f, nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr},

    // Selects
    {HaComponent::Select, "sense_mode", "Sense Mode", "config.sense_mode", nullptr, nullptr, nullptr, "set_simulation", "sense_mode", 0, 0, 0,
     SENSE_OPTIONS, sizeof(SENSE_OPTIONS) / sizeof(SENSE_OPTIONS[0]), nullptr, nullptr,
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"sense_mode\":\"{{ value }}\"}}", nullptr, nullptr},
    {HaComponent::Select, "simulation_mode", "Simulation Mode", "config.simulation_mode", nullptr, nullptr, nullptr, "set_simulation", "mode", 0, 0, 0,
     SIM_OPTIONS, sizeof(SIM_OPTIONS) / sizeof(SIM_OPTIONS[0]), nullptr, nullptr,
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"mode\":{{ value | int }}}}", nullptr, nullptr},
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
