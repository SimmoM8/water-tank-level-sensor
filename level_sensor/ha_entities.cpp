#include "ha_entities.h"

// Sensors (non-binary)
static const HaSensorSpec SENSORS[] = {
    {"raw", "Raw", "{{ value_json.probe.raw }}", nullptr, nullptr, "mdi:water", nullptr, nullptr},
    {"percent", "Percent", "{{ value_json.level.percent }}", "humidity", "%", nullptr, nullptr, nullptr},
    {"liters", "Liters", "{{ value_json.level.liters }}", nullptr, "L", "mdi:water", nullptr, nullptr},
    {"centimeters", "Centimeters", "{{ value_json.level.centimeters }}", nullptr, "cm", "mdi:ruler", nullptr, nullptr},
    {"calibration_state", "Calibration State", "{{ value_json.calibration.state }}", nullptr, nullptr, "mdi:tune", nullptr, nullptr},
    {"cal_dry", "Calibration Dry", "{{ value_json.calibration.dry }}", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"cal_wet", "Calibration Wet", "{{ value_json.calibration.wet }}", nullptr, nullptr, nullptr, nullptr, nullptr},
    {"quality", "Probe Quality", "{{ value_json.probe.quality }}", nullptr, nullptr, "mdi:diagnostics", nullptr, nullptr},
    {"wifi_rssi", "WiFi RSSI", "{{ value_json.wifi.rssi }}", "signal_strength", "dBm", "mdi:wifi", nullptr, nullptr},
    {"ip", "IP Address", "{{ value_json.wifi.ip }}", nullptr, nullptr, "mdi:ip-network", nullptr, nullptr},
    {"raw_valid", "Raw Valid", "{{ value_json.probe.raw_valid }}", nullptr, nullptr, nullptr, nullptr, "raw_valid_bool"},
    {"percent_valid", "Percent Valid", "{{ value_json.level.percent_valid }}", nullptr, nullptr, nullptr, nullptr, "percent_valid_bool"},
    {"liters_valid", "Liters Valid", "{{ value_json.level.liters_valid }}", nullptr, nullptr, nullptr, nullptr, "liters_valid_bool"},
    {"centimeters_valid", "Centimeters Valid", "{{ value_json.level.centimeters_valid }}", nullptr, nullptr, nullptr, nullptr, "centimeters_valid_bool"},
    {"last_cmd", "Last Command", "{{ value_json.last_cmd.type }}", nullptr, nullptr, "mdi:playlist-check", "{{ value_json.last_cmd | tojson }}", "last_cmd"},
};

// Binary sensors
static const HaBinarySensorSpec BINARY_SENSORS[] = {
    {"probe_connected", "Probe Connected", "{{ value_json.probe.connected }}", "connectivity", nullptr, nullptr},
};

// Buttons
static const HaButtonSpec BUTTONS[] = {
    {"calibrate_dry", "Calibrate Dry", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"dry\"}}", nullptr},
    {"calibrate_wet", "Calibrate Wet", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"wet\"}}", nullptr},
    {"clear_calibration", "Clear Calibration", "{\"schema\":1,\"type\":\"clear_calibration\"}", nullptr},
    {"reannounce", "Re-announce Device", "{\"schema\":1,\"type\":\"reannounce\",\"request_id\":\"{{ timestamp }}\"}", nullptr},
};

// Numbers
static const HaNumberSpec NUMBERS[] = {
    {"tank_volume_l", "Tank Volume (L)", "tank_volume_l", 0.0f, 10000.0f, 1.0f, "set_config", nullptr},
    {"rod_length_cm", "Rod Length (cm)", "rod_length_cm", 0.0f, 1000.0f, 1.0f, "set_config", nullptr},
};

// Switches
static const HaSwitchSpec SWITCHES[] = {
    {"simulation_enabled", "Simulation Enabled", "{{ value_json.config.simulation_enabled }}",
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":true}}",
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":false}}",
     nullptr},
};

// Selects
static const int SIM_OPTIONS[] = {0, 1, 2, 3, 4, 5};
static const HaSelectSpec SELECTS[] = {
    {"simulation_mode", "Simulation Mode", "{{ value_json.config.simulation_mode }}",
     "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"mode\":{{ value }}}}",
     SIM_OPTIONS, sizeof(SIM_OPTIONS) / sizeof(SIM_OPTIONS[0]), nullptr},
};

const HaSensorSpec *ha_getSensors(size_t &count)
{
    count = sizeof(SENSORS) / sizeof(SENSORS[0]);
    return SENSORS;
}

const HaBinarySensorSpec *ha_getBinarySensors(size_t &count)
{
    count = sizeof(BINARY_SENSORS) / sizeof(BINARY_SENSORS[0]);
    return BINARY_SENSORS;
}

const HaButtonSpec *ha_getButtons(size_t &count)
{
    count = sizeof(BUTTONS) / sizeof(BUTTONS[0]);
    return BUTTONS;
}

const HaNumberSpec *ha_getNumbers(size_t &count)
{
    count = sizeof(NUMBERS) / sizeof(NUMBERS[0]);
    return NUMBERS;
}

const HaSwitchSpec *ha_getSwitches(size_t &count)
{
    count = sizeof(SWITCHES) / sizeof(SWITCHES[0]);
    return SWITCHES;
}

const HaSelectSpec *ha_getSelects(size_t &count)
{
    count = sizeof(SELECTS) / sizeof(SELECTS[0]);
    return SELECTS;
}
