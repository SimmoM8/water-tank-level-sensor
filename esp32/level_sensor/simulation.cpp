#include <Arduino.h>
#include <math.h>
#include "simulation.h"
#include "applied_config.h"
#include "probe_reader.h"

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef CFG_SPIKE_DELTA
#define CFG_SPIKE_DELTA 1000000u
#endif
#ifndef CFG_PROBE_DISCONNECTED_BELOW_RAW
#define CFG_PROBE_DISCONNECTED_BELOW_RAW 10000u
#endif
#ifndef CFG_CAL_MIN_DIFF
#define CFG_CAL_MIN_DIFF 20u
#endif

// --- Simulation modes ---
enum SimMode
{
  SIM_DISCONNECTED = 0,
  SIM_NORMAL_FILL = 1,
  SIM_NORMAL_DRAIN = 2,
  SIM_SPIKES = 3,
  SIM_RAPID_FLUCTUATION = 4,
  SIM_STUCK = 5,
  SIM_RANGE_SHIFT = 6
};

const int32_t DEFAULT_CAL_DRY = 32000;
const int32_t DEFAULT_CAL_WET = 45000;

const uint32_t DISCONNECT_INTERVAL_MS = 10000;
const uint32_t DISCONNECT_DURATION_MS = 3000;

const uint32_t FILL_PERIOD_MS = 120000;
const uint32_t DRAIN_PERIOD_MS = 120000;

const uint32_t SPIKE_INTERVAL_MS = 1500;

const uint32_t FLUCTUATE_MS = 4000;
const uint32_t PAUSE_MS = 6000;
const uint32_t STUCK_MS = 8000;
const uint32_t STUCK_PAUSE_MS = 6000;
const uint32_t RANGE_SHIFT_PERIOD_MS = 120000;
const uint32_t RANGE_SHIFT_INTERVAL_MS = 20000;
const uint32_t RANGE_SHIFT_AMOUNT = 500;
const uint32_t RANGE_SHIFT_RATE_PER_S = 25;

struct CalRange
{
  int32_t dry;
  int32_t wet;
  int32_t range;
};

CalRange getCalibration()
{
  const int32_t dry = config_get().calDry;
  const int32_t wet = config_get().calWet;
  const int32_t diff = (dry < wet) ? (wet - dry) : 0;

  if (dry <= 0 || wet <= 0 || diff < (int32_t)CFG_CAL_MIN_DIFF)
    return {DEFAULT_CAL_DRY, DEFAULT_CAL_WET, DEFAULT_CAL_WET - DEFAULT_CAL_DRY};

  return {dry, wet, diff};
}

enum SpikePhase
{
  SPIKE_IDLE = 0,
  SPIKE_ACTIVE,
  SPIKE_HOLD,
  SPIKE_RETURN
};

struct SimState
{
  uint8_t mode = 0;
  uint32_t lastUpdateMs = 0;
  uint32_t simStartMs = 0;
  uint32_t spikeMode = 5;
  uint32_t spikePhase = SPIKE_IDLE;
  uint32_t spikeCount = 0;
  int32_t rangeShiftOffset = 0;
  int32_t lastKnownRaw = 0;
  int32_t simBaselineRaw = 0; // fixed baseline for oscillation
  bool probeDisconnected = false;
  bool filling = false;
  bool spikingUp = false;
  bool stuck = false;
  // Spike scheduling state (moved from static locals)
  uint32_t nextSpikeMs = 0;
  int spikeType = 0;
  int spikeDirection = 1;
  uint32_t holdMs = 0;
  uint32_t gradMs = 0;
  uint32_t retMs = 0;
  uint32_t burstCount = 0;
  uint32_t burstInterval = 0;
};

static SimState s_simState;

// Forward declarations (defined below)
void simulateDisconnected(int32_t &raw, uint32_t &now);
void simulateNormalFill(int32_t &raw, uint32_t &now, const CalRange &cal);
void simulateNormalDrain(int32_t &raw, uint32_t &now, const CalRange &cal);
void simulateSpikes(int32_t &raw, uint32_t &now, const CalRange &cal);

void sim_start(int32_t raw)
{
  // reset simulation state
  s_simState = SimState{};

  // get current calibration
  const CalRange cal = getCalibration();

  // ensure raw is within calibration range
  if (raw < cal.dry || raw > cal.wet)
  {
    raw = cal.dry;
  }

  // initialize last known raw
  s_simState.lastKnownRaw = raw ? raw : getCalibration().dry; // default to dry if raw is invalid
  s_simState.simBaselineRaw = s_simState.lastKnownRaw;        // store fixed baseline for sim
  s_simState.simStartMs = millis();                           // initialize start time
}

int32_t readSimulatedRaw()
{
  uint32_t now = millis();
  int32_t raw = s_simState.lastKnownRaw;
  const CalRange cal = getCalibration();

  switch (s_simState.mode)
  {
  case SIM_DISCONNECTED:
    simulateDisconnected(raw, now);
    break;
  case SIM_NORMAL_FILL:
    simulateNormalFill(raw, now, cal);
    break;
  case SIM_NORMAL_DRAIN:
    simulateNormalDrain(raw, now, cal);
    break;
  case SIM_SPIKES:
    simulateSpikes(raw, now, cal);
    break;
  case SIM_RAPID_FLUCTUATION:
    // implement rapid fluctuation
    break;
  case SIM_STUCK:
    // implement stuck
    break;
  case SIM_RANGE_SHIFT:
    // implement range shift
    break;
  default:
    // default to normal fill
    simulateNormalFill(raw, now, cal);
    break;
  }
  s_simState.lastKnownRaw = raw;
  return raw;
}

void setSimulationMode(uint8_t mode)
{
  s_simState.mode = mode;
  uint32_t now = millis();
  s_simState.simStartMs = now;
  s_simState.filling = false;
  s_simState.probeDisconnected = false;
  s_simState.lastUpdateMs = now;
  // Reset spike mode state
  s_simState.spikePhase = SPIKE_IDLE;
  s_simState.spikeCount = 0;
  s_simState.nextSpikeMs = 0;
}

void simulateDisconnected(int32_t &raw, uint32_t &now)
{
  // Repeating schedule: connected for DISCONNECT_INTERVAL_MS, then disconnected for DISCONNECT_DURATION_MS
  uint32_t elapsed = (uint32_t)(now - s_simState.lastUpdateMs);

  if (!s_simState.probeDisconnected)
  {
    // Currently connected phase
    if (elapsed >= DISCONNECT_INTERVAL_MS)
    {
      s_simState.probeDisconnected = true;
      s_simState.lastUpdateMs = now;
    }
  }
  else
  {
    // Currently disconnected phase
    if (elapsed >= DISCONNECT_DURATION_MS)
    {
      s_simState.probeDisconnected = false;
      s_simState.lastUpdateMs = now;
    }
  }

  if (s_simState.probeDisconnected)
  {
    raw = (int32_t)CFG_PROBE_DISCONNECTED_BELOW_RAW - 10000; // below threshold
  }
}

void simulateNormalFill(int32_t &raw, uint32_t &now, const CalRange &cal)
{
  /*
  initiate fill cycle by calculating where in the
  simulated fill cycle we are based on current level
  */
  if (!s_simState.filling)
  {
    const float fraction = (float)(raw - cal.dry) / (float)cal.range; // 0.0 to 1.0 based on current level
    const uint32_t elapsed = fraction * FILL_PERIOD_MS;               // estimate elapsed time based on current level
    s_simState.simStartMs = now - elapsed;                            // set start time according to elapsed
    s_simState.filling = true;                                        // mark as filling
  }

  // calculate new raw based on elapsed time
  const uint32_t elapsed = now - s_simState.simStartMs;
  const float fraction = (float)(elapsed % FILL_PERIOD_MS) / (float)FILL_PERIOD_MS;
  raw = cal.dry + (int32_t)((cal.range) * fraction);
}

void simulateNormalDrain(int32_t &raw, uint32_t &now, const CalRange &cal)
{
  /*
  initiate drain cycle by calculating where in the
  simulated drain cycle we are based on current level
  */
  if (!s_simState.filling)
  {
    // Clamp current level fraction to [0..1]
    float fraction = (float)(raw - cal.dry) / (float)cal.range;
    if (fraction < 0.0f)
      fraction = 0.0f;
    if (fraction > 1.0f)
      fraction = 1.0f;
    // Drain goes from 1.0 (wet) to 0.0 (dry), so invert
    fraction = 1.0f - fraction;
    const uint32_t elapsed = (uint32_t)(fraction * DRAIN_PERIOD_MS);
    s_simState.simStartMs = now - elapsed;
    s_simState.filling = true;
  }

  // calculate new raw based on elapsed time
  const uint32_t elapsed = now - s_simState.simStartMs;
  const float fraction = 1.0f - ((float)(elapsed % DRAIN_PERIOD_MS) / (float)DRAIN_PERIOD_MS);
  raw = cal.dry + (int32_t)((cal.range) * fraction);
}

void simulateSpikes(int32_t &raw, uint32_t &now, const CalRange &cal)
{
  // Parameters for spike simulation
  constexpr uint32_t SPIKE_HOLD_MS = 1500;        // hold duration for hold modes
  constexpr uint32_t SPIKE_GRADUAL_MS = 1500;     // duration for gradual spike
  constexpr uint32_t SPIKE_RETURN_MS = 500;       // return duration for gradual return
  constexpr uint32_t SPIKE_BURST_COUNT = 3;       // number of spikes in burst mode
  constexpr uint32_t SPIKE_BURST_INTERVAL = 5000; // interval between burst spikes
  const int32_t spikeDelta = (int32_t)(cal.range / 5);

  // Helper: reset to idle and always set nextSpikeMs to a future time
  auto reset_spike = [&]()
  {
    s_simState.spikePhase = SPIKE_IDLE;
    s_simState.simStartMs = now;
    s_simState.spikeCount = 0;
    raw = s_simState.lastKnownRaw;
    // Always set nextSpikeMs to a future time to avoid getting stuck
    s_simState.nextSpikeMs = now + random(2000, 10000);
  };

  if (s_simState.spikePhase == SPIKE_IDLE)
  {
    if (s_simState.nextSpikeMs == 0 || (uint32_t)(now - s_simState.nextSpikeMs) < 0x80000000u)
    {
      // Randomly choose a spike type and parameters
      s_simState.spikeType = random(0, 6);               // 0-5, matches the 6 spike types
      s_simState.spikeDirection = random(0, 2) ? 1 : -1; // up or down
      s_simState.holdMs = random(SPIKE_HOLD_MS, SPIKE_HOLD_MS + 1000);
      s_simState.gradMs = random(SPIKE_GRADUAL_MS, SPIKE_GRADUAL_MS + 10000);
      s_simState.retMs = random(SPIKE_RETURN_MS, SPIKE_RETURN_MS + 5000);
      s_simState.burstCount = random(2, SPIKE_BURST_COUNT + 2);
      s_simState.burstInterval = random(SPIKE_BURST_INTERVAL, SPIKE_BURST_INTERVAL + 5000);
      s_simState.lastKnownRaw = raw;
      s_simState.spikePhase = SPIKE_ACTIVE;
      s_simState.simStartMs = now;
      s_simState.spikeCount = 0;
      // Next spike will be after a random interval (2-10s)
      s_simState.nextSpikeMs = now + random(2000, 10000);
    }
    else
    {
      // No spike: gently oscillate raw within a small range around simBaselineRaw, no drift
      // Tighter sublinear scaling: oscRange = max(1, round(0.5 * pow(cal.range, 0.6)))
      float base = (float)cal.range;
      int32_t oscRange = (int32_t)(0.5f * powf(base, 0.6f));
      if (oscRange < 1)
        oscRange = 1;
      float period = 10000.0f; // ms
      float angle = (2.0f * 3.1415926f * (now % (uint32_t)period)) / period;
      int32_t offset = (int32_t)(oscRange * sinf(angle));
      raw = s_simState.simBaselineRaw + offset;
      return;
    }
  }

  switch (s_simState.spikeType)
  {
  case 0: // spike once, return immediately
    if (s_simState.spikePhase == SPIKE_ACTIVE)
    {
      raw = s_simState.lastKnownRaw + spikeDelta * s_simState.spikeDirection;
      s_simState.spikePhase = SPIKE_RETURN;
      s_simState.simStartMs = now;
    }
    else if (s_simState.spikePhase == SPIKE_RETURN)
    {
      raw = s_simState.lastKnownRaw;
      if ((uint32_t)(now - s_simState.simStartMs) > SPIKE_INTERVAL_MS)
      {
        reset_spike();
      }
    }
    break;
  case 1: // spike, hold, return
    if (s_simState.spikePhase == SPIKE_ACTIVE)
    {
      raw = s_simState.lastKnownRaw + spikeDelta * s_simState.spikeDirection;
      s_simState.spikePhase = SPIKE_HOLD;
      s_simState.simStartMs = now;
    }
    else if (s_simState.spikePhase == SPIKE_HOLD)
    {
      raw = s_simState.lastKnownRaw + spikeDelta * s_simState.spikeDirection;
      if ((uint32_t)(now - s_simState.simStartMs) > s_simState.holdMs)
      {
        s_simState.spikePhase = SPIKE_RETURN;
        s_simState.simStartMs = now;
      }
    }
    else if (s_simState.spikePhase == SPIKE_RETURN)
    {
      raw = s_simState.lastKnownRaw;
      if ((uint32_t)(now - s_simState.simStartMs) > SPIKE_INTERVAL_MS)
      {
        reset_spike();
      }
    }
    break;
  case 2: // gradual increase in spike magnitude, then return
    if (s_simState.spikePhase == SPIKE_ACTIVE)
    {
      uint32_t elapsed = now - s_simState.simStartMs;
      if ((uint32_t)elapsed < s_simState.gradMs)
      {
        float frac = (float)elapsed / (float)s_simState.gradMs;
        raw = s_simState.lastKnownRaw + (int32_t)(spikeDelta * frac * s_simState.spikeDirection);
      }
      else
      {
        s_simState.spikePhase = SPIKE_RETURN;
        s_simState.simStartMs = now;
      }
    }
    else if (s_simState.spikePhase == SPIKE_RETURN)
    {
      uint32_t elapsed = now - s_simState.simStartMs;
      if ((uint32_t)elapsed < s_simState.retMs)
      {
        float frac = 1.0f - (float)elapsed / (float)s_simState.retMs;
        raw = s_simState.lastKnownRaw + (int32_t)(spikeDelta * frac * s_simState.spikeDirection);
      }
      else
      {
        reset_spike();
      }
    }
    break;
  case 3: // burst: certain number of spikes in a time frame
    if (s_simState.spikePhase == SPIKE_ACTIVE)
    {
      raw = s_simState.lastKnownRaw + ((s_simState.spikeCount % 2 == 0) ? spikeDelta : -spikeDelta);
      if ((uint32_t)(now - s_simState.simStartMs) > s_simState.burstInterval)
      {
        s_simState.spikeCount++;
        s_simState.simStartMs = now;
      }
      if (s_simState.spikeCount >= s_simState.burstCount * 2)
      {
        s_simState.spikePhase = SPIKE_RETURN;
        s_simState.simStartMs = now;
      }
    }
    else if (s_simState.spikePhase == SPIKE_RETURN)
    {
      raw = s_simState.lastKnownRaw;
      if ((uint32_t)(now - s_simState.simStartMs) > SPIKE_INTERVAL_MS)
      {
        reset_spike();
      }
    }
    break;
  default:
    // fallback: no spike, just baseline
    raw = s_simState.lastKnownRaw;
    break;
  }
}
