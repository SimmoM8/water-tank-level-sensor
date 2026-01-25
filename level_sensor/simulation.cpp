#include <Arduino.h>
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
};

CalRange getCalibration()
{
  const int32_t dry = config_get().calDry;
  const int32_t wet = config_get().calWet;
  const int32_t diff = (dry > wet) ? (dry - wet) : (wet - dry);

  if (dry <= 0 || wet <= 0 || diff < (int32_t)CFG_CAL_MIN_DIFF)
    return {DEFAULT_CAL_DRY, DEFAULT_CAL_WET};

  return {dry, wet};
}

struct SimState
{
  uint8_t mode = 0;
  uint32_t lastUpdateMs = 0;
  uint32_t simStartMs = 0;
  uint32_t spikeStartMs = 0;
  uint32_t fluctuateStartMs = 0;
  uint32_t pauseStartMs = 0;
  uint32_t stuckStartMs = 0;
  uint32_t stuckPauseStartMs = 0;
  uint32_t rangeShiftStartMs = 0;
  uint32_t rangeShiftLastMs = 0;
  int32_t rangeShiftOffset = 0;
  int32_t lastKnownRaw = 0;
  bool probeDisconnected = false;
  bool filling = false;
  bool spikingUp = false;
  bool stuck = false;
};

static SimState s_simState;

// Forward declarations (defined below)
void simulateDisconnected(int32_t &raw, uint32_t &now);
void simulateNormalFill(int32_t &raw, uint32_t &now, int32_t range);
void simulateNormalDrain(int32_t &raw, uint32_t &now, int32_t range);
void simulateSpikes(int32_t &raw, uint32_t &now, int32_t range);

void sim_start(int32_t raw)
{
  s_simState = SimState{};

  const CalRange cal = getCalibration();
  if (raw < cal.dry || raw > cal.wet)
  {
    raw = cal.dry;
  }
  s_simState.lastKnownRaw = raw ? raw : getCalibration().dry;
  s_simState.simStartMs = millis();
}

int32_t readSimulatedRaw()
{
  uint32_t now = millis();
  int32_t raw = s_simState.lastKnownRaw;
  const CalRange cal = getCalibration();
  const int32_t range = cal.wet - cal.dry;

  switch (s_simState.mode)
  {
  case SIM_DISCONNECTED:
    simulateDisconnected(raw, now);
    break;
  case SIM_NORMAL_FILL:
    simulateNormalFill(raw, now, range);
    break;
  case SIM_NORMAL_DRAIN:
    simulateNormalDrain(raw, now, range);
    break;
  case SIM_SPIKES:
    simulateSpikes(raw, now, range);
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
    simulateNormalFill(raw, now, range);
    break;
  }
  s_simState.lastKnownRaw = raw;
  return raw;
}

void setSimulationMode(uint8_t mode)
{
  s_simState.mode = mode;
  s_simState.filling = false;
}

void simulateDisconnected(int32_t &raw, uint32_t &now)
{
  if (!s_simState.probeDisconnected)
  {
    s_simState.probeDisconnected = true;
    s_simState.lastUpdateMs = now;
  }
  else if (now - s_simState.lastUpdateMs >= DISCONNECT_DURATION_MS)
  {
    s_simState.probeDisconnected = false;
    s_simState.lastUpdateMs = now;
  }

  if (s_simState.probeDisconnected)
  {
    raw = (int32_t)CFG_PROBE_DISCONNECTED_BELOW_RAW - 10000; // below threshold
  }
}

void simulateNormalFill(int32_t &raw, uint32_t &now, int32_t range)
{
  const CalRange cal = getCalibration();

  if (range <= 0)
    return;

  if (!s_simState.filling)
  {
    const float fraction = (float)(raw - cal.dry) / (float)range; // 0.0 to 1.0 based on current level
    const uint32_t elapsed = fraction * FILL_PERIOD_MS;           // estimate elapsed time based on current level
    s_simState.simStartMs = now - elapsed;
    s_simState.filling = true;
  }

  const uint32_t elapsed = now - s_simState.simStartMs;
  const float fraction = (float)(elapsed % FILL_PERIOD_MS) / (float)FILL_PERIOD_MS;
  raw = cal.dry + (int32_t)((range)*fraction);
}

void simulateNormalDrain(int32_t &raw, uint32_t &now, int32_t range)
{
  const CalRange cal = getCalibration();

  if (s_simState.simStartMs > now)
  {
    s_simState.simStartMs = now;
  }

  if (s_simState.filling)
  {
    if (raw < cal.dry || raw > cal.wet)
    {
      raw = cal.wet;
    }

    const float fraction = 1.0f - ((float)raw / (float)range);
    const uint32_t elapsed = fraction * DRAIN_PERIOD_MS;
    s_simState.filling = false;
    return;
  }

  const uint32_t elapsed = now - s_simState.simStartMs;
  const float fraction = 1.0f - ((float)(elapsed % DRAIN_PERIOD_MS) / (float)DRAIN_PERIOD_MS);
  raw = cal.dry + (int32_t)((range)*fraction);
}

void simulateSpikes(int32_t &raw, uint32_t &now, int32_t range)
{
  const CalRange cal = getCalibration();

  if (s_simState.spikeStartMs == 0)
  {
    s_simState.spikeStartMs = now;
  }

  if (now - s_simState.spikeStartMs >= SPIKE_INTERVAL_MS)
  {
    s_simState.spikeStartMs = now;
    if (s_simState.spikingUp)
    {
      raw += (cal.wet - cal.dry) / 4;
      if (raw > cal.wet)
        raw = cal.wet;
    }
    else
    {
      raw -= (cal.wet - cal.dry) / 4;
      if (raw < cal.dry)
        raw = cal.dry;
    }
    s_simState.spikingUp = !s_simState.spikingUp;
  }
}