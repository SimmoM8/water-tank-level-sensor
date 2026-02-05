#pragma once

#include <stdint.h>

#include "device_state.h"

struct AppliedConfig
{
    float tankVolumeLiters;
    float rodLengthCm;
    SenseMode senseMode;
    uint8_t simulationMode;

    uint32_t calDry;
    uint32_t calWet;
    bool calInverted;
};

// Load config from NVS at boot; marks dirty=false after initial load.
void config_begin();

// Mark configuration dirty after a successful NVS write.
void config_markDirty();

// If dirty, reload from NVS and clear dirty; returns true if reloaded.
bool config_reloadIfDirty();

// Access the cached applied config (authoritative in RAM after last reload).
const AppliedConfig &config_get();
