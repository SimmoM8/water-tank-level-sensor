#include <Arduino.h>
#include "simulation.h"

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef CFG_PROBE_DISCONNECTED_BELOW_RAW
#define CFG_PROBE_DISCONNECTED_BELOW_RAW 30000u
#endif
#ifndef CFG_SPIKE_DELTA
#define CFG_SPIKE_DELTA 10000u
#endif
#ifndef CFG_RAPID_FLUCTUATION_DELTA
#define CFG_RAPID_FLUCTUATION_DELTA 5000u
#endif

// --- Simulation modes ---
enum SimMode
{
  SIM_DISCONNECTED = 0,
  SIM_NORMAL_FILL = 1,
  SIM_NORMAL_DRAIN = 2,
  SIM_SPIKES = 3,
  SIM_RAPID_FLUCTUATION = 4,
  SIM_STUCK = 5
};

// --- Internal state ---
static uint8_t currentMode = SIM_DISCONNECTED;
static int32_t simValue = CFG_PROBE_DISCONNECTED_BELOW_RAW - 500;
static uint32_t lastUpdateMs = 0;
static bool initialized = false;

// --- Per-mode initialization ---
static void initForMode(uint8_t mode)
{
  switch (mode)
  {
  case SIM_NORMAL_FILL:
    simValue = 32000;
    break;
  case SIM_NORMAL_DRAIN:
    simValue = 45000;
    break;
  case SIM_SPIKES:
    simValue = 36000;
    break;
  case SIM_RAPID_FLUCTUATION:
    simValue = 38000;
    break;
  case SIM_STUCK:
    simValue = 37000;
    break;
  case SIM_DISCONNECTED:
  default:
    simValue = CFG_PROBE_DISCONNECTED_BELOW_RAW - 500;
    break;
  }
}

static void ensureInitialized()
{
  if (initialized)
    return;

  initForMode(currentMode);
  initialized = true;
  lastUpdateMs = millis();
}

// --- Per-mode simulation update helpers ---
static void simulateNormalFill(uint32_t elapsedMs)
{
  // Smoothly increases raw value up to a cap.
  const float rate = 0.6f; // raw units per ms
  const int32_t delta = (int32_t)(elapsedMs * rate);
  uint32_t next = (uint32_t)simValue + (uint32_t)max<int32_t>(0, delta);
  if (next > 45000u)
    next = 45000u;
  simValue = (int32_t)next;
}

static void simulateNormalDrain(uint32_t elapsedMs)
{
  // Smoothly decreases raw value down to a floor.
  const float rate = 0.6f; // raw units per ms
  const int32_t delta = (int32_t)(elapsedMs * rate);
  if (delta > simValue)
  {
    simValue = 32000;
  }
  else
  {
    simValue = (int32_t)max<int32_t>(32000, (int32_t)simValue - delta);
  }
}

static void simulateSpikes(uint32_t nowMs)
{
  // Stable baseline with periodic spikes up/down and a gentle return to baseline.
  if ((nowMs / 1200) % 5 == 0)
  {
    const int32_t spike = ((nowMs / 300) % 2 == 0) ? (int32_t)CFG_SPIKE_DELTA : -(int32_t)CFG_SPIKE_DELTA;
    int32_t next = (int32_t)simValue + spike;
    if (next < 32000)
      next = 32000;
    if (next > 48000)
      next = 48000;
    simValue = (int32_t)next;
  }
  else
  {
    // drift back to baseline
    if (simValue > 36000)
    {
      const int32_t d = (int32_t)(simValue - 36000);
      simValue -= (d < 200 ? d : 200);
    }
    else if (simValue < 36000)
    {
      const int32_t d = (int32_t)(36000 - simValue);
      simValue += (d < 200 ? d : 200);
    }
  }
}

static void simulateRapidFluctuation(uint32_t nowMs)
{
  // Fast alternating high/low readings.
  const bool high = ((nowMs / 400) % 2) == 0;
  const int32_t base = 36000;
  const int32_t delta = CFG_RAPID_FLUCTUATION_DELTA + 1000;
  simValue = high ? base + delta : base - delta;
}

static void simulateStuck()
{
  // No change; remains constant.
}

static void simulateDisconnected()
{
  // Always below the "probe disconnected" threshold.
  simValue = CFG_PROBE_DISCONNECTED_BELOW_RAW - 500;
}

void setSimulationMode(uint8_t mode)
{
  currentMode = mode;
  initialized = false;
  lastUpdateMs = millis();
}

int32_t readSimulatedRaw()
{
  ensureInitialized();

  const uint32_t now = millis();
  const uint32_t elapsed = now - lastUpdateMs;
  lastUpdateMs = now;

  switch (currentMode)
  {
  case SIM_NORMAL_FILL:
    simulateNormalFill(elapsed);
    break;
  case SIM_NORMAL_DRAIN:
    simulateNormalDrain(elapsed);
    break;
  case SIM_SPIKES:
    simulateSpikes(now);
    break;
  case SIM_RAPID_FLUCTUATION:
    simulateRapidFluctuation(now);
    break;
  case SIM_STUCK:
    simulateStuck();
    break;
  case SIM_DISCONNECTED:
  default:
    simulateDisconnected();
    break;
  }

  return simValue;
}
