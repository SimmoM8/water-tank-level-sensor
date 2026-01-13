#include "probe_reader.h"

#include <Arduino.h>
#include "simulation.h"

static struct
{
    ProbeConfig cfg = {0, 1, 5}; // default pin 0, 1 sample, 5ms delay
    ReadMode mode = READ_PROBE;  // default mode
} probe;

void probe_begin(const ProbeConfig &config)
{
    probe.cfg = config;
    if (probe.cfg.samples == 0)
    {
        probe.cfg.samples = 1; // ensure at least 1 sample
    }
}

// set read mode based on applied truth in preferences
void probe_updateMode(ReadMode mode)
{
    probe.mode = mode;
}

// Read raw probe value using touchRead averaged over N samples
static uint32_t readProbe(uint8_t pin, uint8_t samples)
{
    uint32_t averageRaw = 0;
    for (uint8_t i = 0; i < samples; i++)
    {
        averageRaw += (uint32_t)touchRead(pin);
        delay(probe.cfg.samplingDelay); // delay between samples
    }
    return averageRaw / samples;
}

// get the raw value either from the probe or simulation
uint32_t probe_getRaw()
{
    if (probe.mode == READ_SIM)
    {
        return readSimulatedRaw(); // Simulation module is a backend provider for raw probe values.
    }
    return readProbe(probe.cfg.pin, probe.cfg.samples);
}