#include "ha_entities.h"

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
