#include "applied_config.h"

#include <Arduino.h>

#include "device_state.h"
#include "storage_nvs.h"

static AppliedConfig g_config = {
    NAN, // tankVolumeLiters
    NAN, // rodLengthCm
    SenseMode::TOUCH,
    0,
    0,
    0,
    false};

static bool s_dirty = false;

static void loadFromNvs()
{
    float vol = NAN;
    float rod = NAN;
    storage_loadTank(vol, rod);

    SenseMode senseMode = SenseMode::TOUCH;
    uint8_t simMode = 0;
    storage_loadSimulation(senseMode, simMode);

    if (senseMode != SenseMode::SIM)
    {
        senseMode = SenseMode::TOUCH;
    }

    int32_t dry = 0;
    int32_t wet = 0;
    bool inverted = false;
    storage_loadActiveCalibration(dry, wet, inverted);

    g_config.tankVolumeLiters = vol;
    g_config.rodLengthCm = rod;
    g_config.senseMode = senseMode;
    g_config.simulationMode = simMode;
    g_config.calDry = dry;
    g_config.calWet = wet;
    g_config.calInverted = inverted;
}

void config_begin()
{
    loadFromNvs();
    s_dirty = false;
}

void config_markDirty()
{
    s_dirty = true;
}

bool config_reloadIfDirty()
{
    if (!s_dirty)
    {
        return false;
    }

    loadFromNvs();
    s_dirty = false;
    return true;
}

const AppliedConfig &config_get()
{
    return g_config;
}
