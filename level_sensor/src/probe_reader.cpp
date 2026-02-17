#include "probe_reader.h"

#include <Arduino.h>
#include "simulation.h"

static struct
{
    ProbeConfig cfg = {0, 1, 5}; // default pin 0, 1 sample, 5ms delay
    ReadMode mode = READ_PROBE;  // default mode
} probe;

static constexpr uint16_t kMinSamples = 1;

static ProbeConfig normalizeConfig(ProbeConfig cfg)
{
    if (cfg.samples < kMinSamples)
    {
        cfg.samples = kMinSamples;
    }
    return cfg;
}

// Contract: config.samples must be >= 1 (clamped here); config.pin is used as-is.
void probe_begin(const ProbeConfig &config)
{
    probe.cfg = normalizeConfig(config);
}

// Contract: mode selects between physical probe and simulation backend.
void probe_updateMode(ReadMode mode)
{
    probe.mode = mode;
}

// Read raw probe value using touchRead averaged over N samples.
static uint32_t readProbe(uint8_t pin, uint16_t samples)
{
    uint32_t averageRaw = 0;
    for (uint16_t i = 0; i < samples; i++)
    {
        averageRaw += (uint32_t)touchRead(pin);
        delay(probe.cfg.samplingDelay); // delay between samples
    }
    return averageRaw / samples;
}

// Contract: returns a raw probe value from the active backend.
uint32_t probe_getRaw()
{
    if (probe.mode == READ_SIM)
    {
        return readSimulatedRaw(); // Simulation module is a backend provider for raw probe values.
    }
    return readProbe(probe.cfg.pin, probe.cfg.samples);
}

