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

enum SimMode
{
  SIM_DISCONNECTED = 0,
  SIM_NORMAL_FILL = 1,
  SIM_NORMAL_DRAIN = 2,
  SIM_SPIKES = 3,
  SIM_RAPID_FLUCTUATION = 4,
  SIM_STUCK = 5
};

static uint8_t currentMode = SIM_DISCONNECTED;
static int32_t simValue = CFG_PROBE_DISCONNECTED_BELOW_RAW - 500;
static uint32_t lastUpdateMs = 0;
static bool initialized = false;

void setSimulationMode(uint8_t mode)
{
  currentMode = mode;
  initialized = false;
  lastUpdateMs = millis();
}

static void ensureInitialized()
{
  if (initialized)
    return;

  switch (currentMode)
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

  initialized = true;
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
  {
    const float rate = 0.6f; // raw units per ms
    const int32_t delta = (int32_t)(elapsed * rate);
    uint32_t next = (uint32_t)simValue + (uint32_t)max<int32_t>(0, delta);
    if (next > 45000u)
      next = 45000u;
    simValue = (int32_t)next;
    break;
  }
  case SIM_NORMAL_DRAIN:
  {
    const float rate = 0.6f;
    const int32_t delta = (int32_t)(elapsed * rate);
    if (delta > simValue)
    {
      simValue = 32000;
    }
    else
    {
      simValue = (int32_t)max<int32_t>(32000, (int32_t)simValue - delta);
    }
    break;
  }
  case SIM_SPIKES:
  {
    // Stable value with periodic spikes upward or downward
    if ((now / 1200) % 5 == 0)
    {
      const int32_t spike = ((now / 300) % 2 == 0) ? (int32_t)CFG_SPIKE_DELTA : -(int32_t)CFG_SPIKE_DELTA;
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
        {
          const int32_t d = (int32_t)(simValue - 36000);
          simValue -= (d < 200 ? d : 200);
        }
      }
      else if (simValue < 36000)
      {
        {
          const int32_t d = (int32_t)(36000 - simValue);
          simValue += (d < 200 ? d : 200);
        }
      }
    }
    break;
  }
  case SIM_RAPID_FLUCTUATION:
  {
    const bool high = ((now / 400) % 2) == 0;
    const int32_t base = 36000;
    const int32_t delta = CFG_RAPID_FLUCTUATION_DELTA + 1000;
    simValue = high ? base + delta : base - delta;
    break;
  }
  case SIM_STUCK:
    // no change; remains constant
    break;
  case SIM_DISCONNECTED:
  default:
    simValue = CFG_PROBE_DISCONNECTED_BELOW_RAW - 500;
    break;
  }

  return simValue;
}
