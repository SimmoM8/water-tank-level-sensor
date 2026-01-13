#pragma once

#include <Arduino.h>

// probe_reader: produces raw probe values (physical or simulation backend)

enum ReadMode
{
    READ_PROBE = 0, // read from physical probe
    READ_SIM = 1,   // read from simulation module
};

struct ProbeConfig
{
    uint8_t pin;           // Probe input pin
    uint8_t samples;       // Number of samples to average (higher = smoother/slower | lower = noisier/faster)
    uint8_t samplingDelay; // Delay between samples in milliseconds
};

// Configure probe reader (called by app)
void probe_begin(const ProbeConfig &cfg);

// get the raw value either from the probe or simulation
uint32_t probe_getRaw();

void probe_updateMode(ReadMode mode);
