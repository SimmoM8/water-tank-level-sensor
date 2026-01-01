#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
#include <cstring>
#include "simulation.h"

// Optional config overrides (see water_level_config.h)
#ifdef __has_include
#if __has_include("water_level_config.h")
#include "water_level_config.h"
#endif
#endif

#ifndef CFG_PROBE_DISCONNECTED_BELOW_RAW
#define CFG_PROBE_DISCONNECTED_BELOW_RAW 30000u
#endif
#ifndef CFG_CAL_MIN_DIFF
#define CFG_CAL_MIN_DIFF 20u
#endif
#ifndef CFG_SPIKE_DELTA
#define CFG_SPIKE_DELTA 10000u
#endif
#ifndef CFG_RAPID_FLUCTUATION_DELTA
#define CFG_RAPID_FLUCTUATION_DELTA 5000u
#endif
#ifndef CFG_SPIKE_COUNT_THRESHOLD
#define CFG_SPIKE_COUNT_THRESHOLD 3u
#endif
#ifndef CFG_SPIKE_WINDOW_MS
#define CFG_SPIKE_WINDOW_MS 5000u
#endif
#ifndef CFG_STUCK_EPS
#define CFG_STUCK_EPS 2u
#endif
#ifndef CFG_STUCK_MS
#define CFG_STUCK_MS 8000u
#endif
#ifndef CFG_PROBE_MIN_RAW
#define CFG_PROBE_MIN_RAW 0u
#endif
#ifndef CFG_PROBE_MAX_RAW
#define CFG_PROBE_MAX_RAW 65535u
#endif
#ifndef CFG_TANK_VOLUME_UNIT
#define CFG_TANK_VOLUME_UNIT "L"
#endif
#ifndef CFG_TANK_VOLUME_MAX
#define CFG_TANK_VOLUME_MAX 30000.0f
#endif
#ifndef CFG_TANK_VOLUME_STEP
#define CFG_TANK_VOLUME_STEP 1.0f
#endif
#ifndef CFG_ROD_LENGTH_UNIT
#define CFG_ROD_LENGTH_UNIT "cm"
#endif
#ifndef CFG_ROD_LENGTH_MAX
#define CFG_ROD_LENGTH_MAX 300.0f
#endif
#ifndef CFG_ROD_LENGTH_STEP
#define CFG_ROD_LENGTH_STEP 1.0f
#endif

// =============================================================================
// Dad's Smart Home — Water Level Sensor (ESP32 touch) → MQTT → Home Assistant
//
// What this sketch does:
// 1) Connects to Wi‑Fi
// 2) Connects to your MQTT broker (Home Assistant / Mosquitto)
// 3) Reads a capacitive "touch" value from the probe on A6 (GPIO13)
// 4) Publishes the raw value to an MQTT topic once per second
// 5) Optionally publishes a smoothed percent value and HA discovery payloads
// 6) Allows calibration via the Serial Monitor (no code edits needed)
// =============================================================================

// --------- Wi‑Fi / MQTT credentials (secrets.h) ---------
#include "secrets.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif
// -------------------------------------------

// ===== MQTT / Home Assistant =====
static const char *MQTT_HOST = "homeassistant.local";
static const int MQTT_PORT = 1883;

// Use a stable client id (unique per device). If you add more ESP32 devices later,
// change this string so they don't clash.
static const char *MQTT_CLIENT_ID = "water-tank-esp32";

// MQTT Topics for publishing to Home Assistant
static const char *TOPIC_TANK_RAW = "home/water/tank/raw";
static const char *TOPIC_TANK_PERCENT = "home/water/tank/percent";
static const char *TOPIC_TANK_STATUS = "home/water/tank/status";
static const char *TOPIC_TANK_PROBE = "home/water/tank/probe_connected";
static const char *TOPIC_TANK_CAL_STATE = "home/water/tank/calibration_state";
static const char *TOPIC_TANK_QUALITY = "home/water/tank/quality_reason";
static const char *TOPIC_TANK_RAW_VALID = "home/water/tank/raw_valid";
static const char *TOPIC_TANK_PERCENT_VALID = "home/water/tank/percent_valid";
static const char *TOPIC_TANK_LITERS_VALID = "home/water/tank/liters_valid";
static const char *TOPIC_TANK_CM_VALID = "home/water/tank/centimeters_valid";
static const char *TOPIC_TANK_LITERS = "home/water/tank/liters";
static const char *TOPIC_TANK_CM = "home/water/tank/centimeters";
static const char *TOPIC_CFG_TANK_VOLUME = "home/water/tank/cfg/tank_volume_l";
static const char *TOPIC_CFG_ROD_LENGTH = "home/water/tank/cfg/rod_length_cm";
static const char *TOPIC_CMD_TANK_VOLUME = "home/water/tank/cmd/tank_volume_l";
static const char *TOPIC_CMD_ROD_LENGTH = "home/water/tank/cmd/rod_length_cm";
static const char *TOPIC_CFG_SIM_ENABLED = "home/water/tank/cfg/simulation_enabled";
static const char *TOPIC_CFG_SIM_MODE = "home/water/tank/cfg/simulation_mode";
static const char *TOPIC_CMD_SIM_ENABLED = "home/water/tank/cmd/simulation_enabled";
static const char *TOPIC_CMD_SIM_MODE = "home/water/tank/cmd/simulation_mode";
static const char *TOPIC_CMD_CAL_DRY = "home/water/tank/cmd/calibrate_dry";
static const char *TOPIC_CMD_CAL_WET = "home/water/tank/cmd/calibrate_wet";
static const char *TOPIC_CMD_CLEAR_CAL = "home/water/tank/cmd/clear_calibration";

static const char *STATUS_ONLINE = "online";
static const char *STATUS_OFFLINE = "offline";
static const char *STATUS_CALIBRATING = "calibrating";
static const char *STATUS_OK = "ok";
static const char *STATUS_NEEDS_CAL = "needs_calibration";

// ===== Sensor / Sampling =====
// Arduino Nano ESP32: A6 is GPIO13 and is touch-capable.
static const int TOUCH_PIN = A7;

static const uint8_t TOUCH_SAMPLES = 16;               // average N reads to reduce jitter
static const uint32_t RAW_PUBLISH_MS = 1000;           // publish the raw value once per second
static const uint32_t PERCENT_PUBLISH_MS = 3000;       // publish the percent value less often
static const float PERCENT_EMA_ALPHA = 0.2f;           // EMA smoothing factor
static const uint16_t CAL_MIN_DIFF = CFG_CAL_MIN_DIFF; // minimum delta between dry/wet to accept calibration

// ===== Network timeouts =====
static const uint32_t WIFI_TIMEOUT_MS = 20000;

// ===== Preferences (NVS) =====
static const char *PREF_NAMESPACE = "water_level";
static const char *PREF_KEY_DRY = "dry";
static const char *PREF_KEY_WET = "wet";
static const char *PREF_KEY_INV = "inv";
static const char *PREF_KEY_TANK_VOL = "tank_vol";
static const char *PREF_KEY_ROD_LEN = "rod_len";
static const char *PREF_KEY_SIM_ENABLED = "sim_en";
static const char *PREF_KEY_SIM_MODE = "sim_mode";

// ===== MQTT Discovery =====
static const bool ENABLE_DISCOVERY = true;
static const char *DISCOVERY_PREFIX = "homeassistant";
static const char *DEVICE_ID = "water_tank_esp32";
static const char *DEVICE_NAME = "Water Tank Sensor";
static const char *DEVICE_MANUFACTURER = "DIY";
static const char *DEVICE_MODEL = "Nano ESP32";
static const char *DEVICE_SW_VERSION = "1.3"; // update whenever pushing to main branch

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

uint16_t calDry = 0;
uint16_t calWet = 0;
bool calInverted = false; // Whether higher raw means "more water"

uint16_t lastRawValue = 0;
static float percentEma = NAN; // Smoothed percent (EMA)
bool discoverySent = false;
String lastStatus = "";
bool probeConnected = false;
bool rawValid = false;
bool percentValid = false;
bool litersValid = false;
bool centimetersValid = false;
float tankVolumeLiters = NAN;
float rodLengthCm = NAN;
float lastLiters = NAN;
float lastCentimeters = NAN;
bool simulationEnabled = false;
uint8_t simulationMode = 0;

enum CalibrationState
{
  CAL_STATE_NEEDS,
  CAL_STATE_CALIBRATING,
  CAL_STATE_CALIBRATED
};

CalibrationState calibrationState = CAL_STATE_NEEDS;
CalibrationState lastPublishedCalState = CAL_STATE_NEEDS;
bool calibrationInProgress = false;
bool publishedCalState = false;

enum ProbeQualityReason
{
  QUALITY_OK,
  QUALITY_DISCONNECTED_LOW_RAW,
  QUALITY_UNRELIABLE_SPIKES,
  QUALITY_UNRELIABLE_RAPID,
  QUALITY_UNRELIABLE_STUCK,
  QUALITY_OUT_OF_BOUNDS,
  QUALITY_UNKNOWN
};

ProbeQualityReason probeQualityReason = QUALITY_UNKNOWN;
ProbeQualityReason lastPublishedQualityReason = QUALITY_UNKNOWN;
bool publishedQualityReason = false;

bool lastPublishedProbeConnected = false;
bool publishedProbeConnected = false;
bool lastPublishedRawValid = false;
bool publishedRawValid = false;
bool lastPublishedPercentValid = false;
bool publishedPercentValid = false;
bool lastPublishedLitersValid = false;
bool publishedLitersValid = false;
bool lastPublishedCmValid = false;
bool publishedCmValid = false;

// ----------------- Helpers -----------------

static void publishStatus(const char *status, bool retained = true, bool force = false);
static void refreshValidityFlags(float currentPercent, bool forcePublish = false);
static uint16_t readRawValue();

static void logLine(const char *msg)
{
  Serial.println(msg);
}

static void printHelpMenu()
{
  Serial.println("\n[CAL] Serial commands:");
  Serial.println("  dry   -> capture current averaged raw as dry, save to NVS");
  Serial.println("  wet   -> capture current averaged raw as wet, save to NVS");
  Serial.println("  show  -> print current calibration values");
  Serial.println("  clear -> clear stored calibration");
  Serial.println("  invert-> toggle inverted flag and save");
  Serial.println("  help  -> show this menu");
}

static bool hasCalibrationValues()
{
  return calDry > 0 && calWet > 0 && (uint16_t)abs((int)calWet - (int)calDry) >= CAL_MIN_DIFF;
}

static const char *calibrationStateToString(CalibrationState state)
{
  switch (state)
  {
  case CAL_STATE_CALIBRATING:
    return STATUS_CALIBRATING;
  case CAL_STATE_CALIBRATED:
    return "calibrated";
  case CAL_STATE_NEEDS:
  default:
    return STATUS_NEEDS_CAL;
  }
}

static void publishBoolState(const char *topic, bool value, bool retained, bool &publishedFlag, bool &lastValue, bool force = false)
{
  if (!mqtt.connected())
    return;

  if (force || !publishedFlag || lastValue != value)
  {
    mqtt.publish(topic, value ? "1" : "0", retained);
    publishedFlag = true;
    lastValue = value;
  }
}

static void publishCalibrationState(bool force = false)
{
  if (!mqtt.connected())
    return;

  if (force || !publishedCalState || lastPublishedCalState != calibrationState)
  {
    mqtt.publish(TOPIC_TANK_CAL_STATE, calibrationStateToString(calibrationState), true);
    publishedCalState = true;
    lastPublishedCalState = calibrationState;
  }
}

static void setCalibrationState(CalibrationState state, bool forcePublish = false)
{
  calibrationState = state;
  publishCalibrationState(forcePublish);
}

static void refreshStatus(bool force = false)
{
  if (calibrationState == CAL_STATE_CALIBRATING)
  {
    publishStatus(STATUS_CALIBRATING, true, force);
    return;
  }

  if (!probeConnected)
  {
    publishStatus(STATUS_ONLINE, true, force);
    return;
  }

  if (calibrationState == CAL_STATE_CALIBRATED)
  {
    publishStatus(STATUS_OK, true, force);
  }
  else
  {
    publishStatus(STATUS_NEEDS_CAL, true, force);
  }
}

static float clampNonNegative(float value)
{
  return value < 0.0f ? 0.0f : value;
}

static float tankExternalFromLiters(float liters)
{
  if (strcmp(CFG_TANK_VOLUME_UNIT, "mL") == 0)
  {
    return liters * 1000.0f;
  }
  return liters;
}

static float tankLitersFromExternal(float external)
{
  if (strcmp(CFG_TANK_VOLUME_UNIT, "mL") == 0)
  {
    return external / 1000.0f;
  }
  return external;
}

static float rodExternalFromCm(float cm)
{
  if (strcmp(CFG_ROD_LENGTH_UNIT, "m") == 0)
  {
    return cm / 100.0f;
  }
  return cm;
}

static float rodCmFromExternal(float external)
{
  if (strcmp(CFG_ROD_LENGTH_UNIT, "m") == 0)
  {
    return external * 100.0f;
  }
  return external;
}

static const char *qualityReasonToString(ProbeQualityReason reason)
{
  switch (reason)
  {
  case QUALITY_OK:
    return "ok";
  case QUALITY_DISCONNECTED_LOW_RAW:
    return "disconnected_low_raw";
  case QUALITY_UNRELIABLE_SPIKES:
    return "unreliable_spikes";
  case QUALITY_UNRELIABLE_RAPID:
    return "unreliable_rapid_fluctuation";
  case QUALITY_UNRELIABLE_STUCK:
    return "unreliable_stuck";
  case QUALITY_OUT_OF_BOUNDS:
    return "out_of_bounds";
  case QUALITY_UNKNOWN:
  default:
    return "unknown";
  }
}

static void publishQualityReason(bool force = false)
{
  if (!mqtt.connected())
    return;

  if (force || !publishedQualityReason || probeQualityReason != lastPublishedQualityReason)
  {
    mqtt.publish(TOPIC_TANK_QUALITY, qualityReasonToString(probeQualityReason), true);
    publishedQualityReason = true;
    lastPublishedQualityReason = probeQualityReason;
  }
}

static bool evaluateProbeConnected(uint16_t raw)
{
  static bool hasLast = false;
  static uint16_t lastRaw = 0;
  static uint8_t spikeCount = 0;
  static uint32_t spikeWindowStart = 0;
  static uint32_t stuckStartMs = 0;

  const uint32_t now = millis();
  if (spikeWindowStart == 0)
  {
    spikeWindowStart = now;
  }
  if (stuckStartMs == 0)
  {
    stuckStartMs = now;
  }

  ProbeQualityReason reason = QUALITY_OK;
  if (raw < CFG_PROBE_DISCONNECTED_BELOW_RAW)
  {
    reason = QUALITY_DISCONNECTED_LOW_RAW;
  }
  else if (raw < CFG_PROBE_MIN_RAW || raw > CFG_PROBE_MAX_RAW)
  {
    reason = QUALITY_OUT_OF_BOUNDS;
  }
  else
  {
    uint32_t delta = 0;
    if (hasLast)
    {
      delta = (uint32_t)abs((int32_t)raw - (int32_t)lastRaw);
    }

    if (delta >= CFG_RAPID_FLUCTUATION_DELTA)
    {
      reason = QUALITY_UNRELIABLE_RAPID;
    }
    else
    {
      if (now - spikeWindowStart > CFG_SPIKE_WINDOW_MS)
      {
        spikeWindowStart = now;
        spikeCount = 0;
      }
      if (delta >= CFG_SPIKE_DELTA)
      {
        spikeCount++;
        if (spikeCount >= CFG_SPIKE_COUNT_THRESHOLD)
        {
          reason = QUALITY_UNRELIABLE_SPIKES;
        }
      }

      if (delta > CFG_STUCK_EPS)
      {
        stuckStartMs = now;
      }
      else if ((now - stuckStartMs) >= CFG_STUCK_MS)
      {
        reason = QUALITY_UNRELIABLE_STUCK;
      }
    }
  }

  lastRaw = raw;
  hasLast = true;

  probeQualityReason = reason;
  return reason == QUALITY_OK;
}

static void refreshCalibrationState(bool force = false)
{
  if (calibrationInProgress)
  {
    percentEma = NAN;
    refreshValidityFlags(NAN, force);
    setCalibrationState(CAL_STATE_CALIBRATING, force);
    refreshStatus(force);
    return;
  }

  CalibrationState nextState = CAL_STATE_NEEDS;
  if (probeConnected && hasCalibrationValues())
  {
    nextState = CAL_STATE_CALIBRATED;
  }
  else
  {
    percentEma = NAN;
    refreshValidityFlags(NAN, force);
  }

  if (nextState != calibrationState)
  {
    calibrationState = nextState;
  }

  publishCalibrationState(force);
  refreshStatus(force);
}

static void refreshValidityFlags(float currentPercent, bool forcePublish)
{
  percentValid = probeConnected && calibrationState == CAL_STATE_CALIBRATED && !isnan(currentPercent);
  litersValid = percentValid && !isnan(tankVolumeLiters);
  centimetersValid = percentValid && !isnan(rodLengthCm);

  publishBoolState(TOPIC_TANK_PERCENT_VALID, percentValid, false, publishedPercentValid, lastPublishedPercentValid, forcePublish);
  publishBoolState(TOPIC_TANK_LITERS_VALID, litersValid, false, publishedLitersValid, lastPublishedLitersValid, forcePublish);
  publishBoolState(TOPIC_TANK_CM_VALID, centimetersValid, false, publishedCmValid, lastPublishedCmValid, forcePublish);
}

static void publishConfigValues(bool force = false)
{
  if (!mqtt.connected())
    return;

  if (!isnan(tankVolumeLiters))
  {
    mqtt.publish(TOPIC_CFG_TANK_VOLUME, String(tankExternalFromLiters(tankVolumeLiters), 2).c_str(), true);
  }
  else if (force)
  {
    mqtt.publish(TOPIC_CFG_TANK_VOLUME, "", true);
  }

  if (!isnan(rodLengthCm))
  {
    mqtt.publish(TOPIC_CFG_ROD_LENGTH, String(rodExternalFromCm(rodLengthCm), 2).c_str(), true);
  }
  else if (force)
  {
    mqtt.publish(TOPIC_CFG_ROD_LENGTH, "", true);
  }

  mqtt.publish(TOPIC_CFG_SIM_ENABLED, simulationEnabled ? "1" : "0", true);
  mqtt.publish(TOPIC_CFG_SIM_MODE, String(simulationMode).c_str(), true);
}

static void updateProbeStatus(uint16_t raw, bool forcePublish = false)
{
  const bool prevConnected = probeConnected;
  const ProbeQualityReason prevReason = probeQualityReason;

  const bool connected = evaluateProbeConnected(raw);
  probeConnected = connected;
  rawValid = probeConnected;

  if (!probeConnected)
  {
    percentEma = NAN;
    refreshValidityFlags(NAN, true);
    if (!calibrationInProgress)
    {
      setCalibrationState(CAL_STATE_NEEDS);
    }
  }

  if (!prevConnected && probeConnected)
  {
    Serial.println("[PROBE] connected");
  }
  else if (!probeConnected && (prevConnected || prevReason != probeQualityReason))
  {
    Serial.print("[PROBE] disconnected: ");
    Serial.println(qualityReasonToString(probeQualityReason));
  }

  publishQualityReason(forcePublish);
  publishBoolState(TOPIC_TANK_PROBE, probeConnected, false, publishedProbeConnected, lastPublishedProbeConnected, forcePublish);
  publishBoolState(TOPIC_TANK_RAW_VALID, rawValid, false, publishedRawValid, lastPublishedRawValid, forcePublish);

  refreshCalibrationState(forcePublish);
}

static void publishPercentAndDerived(float percent, bool force = false)
{
  refreshValidityFlags(percent, force);

  if (!mqtt.connected())
    return;

  if (percentValid)
  {
    mqtt.publish(TOPIC_TANK_PERCENT, String(percent, 1).c_str());
  }

  if (litersValid)
  {
    const float liters = clampNonNegative(tankVolumeLiters * percent / 100.0f);
    if (force || isnan(lastLiters) || fabs(lastLiters - liters) > 0.01f)
    {
      lastLiters = liters;
      mqtt.publish(TOPIC_TANK_LITERS, String(liters, 2).c_str());
    }
  }
  else
  {
    lastLiters = NAN;
  }

  if (centimetersValid)
  {
    const float centimeters = clampNonNegative(rodLengthCm * percent / 100.0f);
    if (force || isnan(lastCentimeters) || fabs(lastCentimeters - centimeters) > 0.01f)
    {
      lastCentimeters = centimeters;
      mqtt.publish(TOPIC_TANK_CM, String(centimeters, 1).c_str());
    }
  }
  else
  {
    lastCentimeters = NAN;
  }
}

static void recomputeDerivedFromPercent(bool force = false)
{
  if (isnan(percentEma))
  {
    refreshValidityFlags(NAN, force);
    return;
  }

  publishPercentAndDerived(percentEma, force);
}

static void loadConfigValues()
{
  tankVolumeLiters = prefs.getFloat(PREF_KEY_TANK_VOL, NAN);
  rodLengthCm = prefs.getFloat(PREF_KEY_ROD_LEN, NAN);
  simulationEnabled = prefs.getBool(PREF_KEY_SIM_ENABLED, false);
  simulationMode = prefs.getUChar(PREF_KEY_SIM_MODE, 0);
  setSimulationMode(simulationMode);

  Serial.print("[CFG] Tank volume (L): ");
  if (isnan(tankVolumeLiters))
  {
    Serial.println("unset");
  }
  else
  {
    Serial.println(tankVolumeLiters, 2);
  }

  Serial.print("[CFG] Rod length (cm): ");
  if (isnan(rodLengthCm))
  {
    Serial.println("unset");
  }
  else
  {
    Serial.println(rodLengthCm, 2);
  }

  Serial.print("[CFG] Simulation enabled: ");
  Serial.println(simulationEnabled ? "true" : "false");
  Serial.print("[CFG] Simulation mode: ");
  Serial.println(simulationMode);
}

static void updateTankVolume(float value, bool forcePublish = false)
{
  if (isnan(value))
    return;
  const float next = clampNonNegative(value);
  if (!isnan(tankVolumeLiters) && fabs(tankVolumeLiters - next) < 0.001f)
  {
    // Ignore duplicate/echoed values to avoid log spam
    return;
  }
  tankVolumeLiters = next;
  prefs.putFloat(PREF_KEY_TANK_VOL, tankVolumeLiters);
  Serial.print("[CFG] Tank volume set to ");
  Serial.println(tankVolumeLiters, 2);
  publishConfigValues(forcePublish);
  recomputeDerivedFromPercent(true);
}

static void updateRodLength(float value, bool forcePublish = false)
{
  if (isnan(value))
    return;
  const float next = clampNonNegative(value);
  if (!isnan(rodLengthCm) && fabs(rodLengthCm - next) < 0.001f)
  {
    // Ignore duplicate/echoed values to avoid log spam
    return;
  }
  rodLengthCm = next;
  prefs.putFloat(PREF_KEY_ROD_LEN, rodLengthCm);
  Serial.print("[CFG] Rod length set to ");
  Serial.println(rodLengthCm, 2);
  publishConfigValues(forcePublish);
  recomputeDerivedFromPercent(true);
}

static void setSimulationEnabled(bool enabled, bool forcePublish = false, const char *sourceMsg = nullptr)
{
  if (simulationEnabled == enabled && !forcePublish)
    return;

  simulationEnabled = enabled;
  prefs.putBool(PREF_KEY_SIM_ENABLED, simulationEnabled);
  percentEma = NAN;
  refreshValidityFlags(NAN, true);
  refreshCalibrationState(true);
  const uint16_t raw = readRawValue();
  lastRawValue = raw;
  updateProbeStatus(raw, true);
  publishConfigValues(true);

  Serial.print("[SIM] ");
  Serial.print(simulationEnabled ? "enabled" : "disabled");
  if (sourceMsg != nullptr)
  {
    Serial.print(" (msg=\"");
    Serial.print(sourceMsg);
    Serial.print("\")");
  }
  Serial.println();
}

static void setSimulationModeInternal(uint8_t mode, bool forcePublish = false, const char *sourceMsg = nullptr)
{
  uint8_t clamped = mode > 5 ? 5 : mode;
  if (simulationMode == clamped && !forcePublish)
    return;

  simulationMode = clamped;
  setSimulationMode(simulationMode);
  prefs.putUChar(PREF_KEY_SIM_MODE, simulationMode);
  publishConfigValues(true);
  if (simulationEnabled)
  {
    const uint16_t raw = readRawValue();
    lastRawValue = raw;
    updateProbeStatus(raw, true);
  }
  if (forcePublish)
  {
    publishQualityReason(true);
  }

  Serial.print("[SIM] mode = ");
  Serial.print(simulationMode);
  if (sourceMsg != nullptr)
  {
    Serial.print(" (msg=\"");
    Serial.print(sourceMsg);
    Serial.print("\")");
  }
  Serial.println();
}

static void publishStatus(const char *status, bool retained, bool force)
{
  String newStatus(status);
  bool changed = newStatus != lastStatus;

  if (changed)
  {
    Serial.print("[STATUS] -> ");
    Serial.println(status);
    lastStatus = newStatus;
  }

  if (mqtt.connected() && (changed || force))
  {
    mqtt.publish(TOPIC_TANK_STATUS, status, retained);
  }
}

static void loadCalibration()
{
  calDry = prefs.getUShort(PREF_KEY_DRY, 0);
  calWet = prefs.getUShort(PREF_KEY_WET, 0);
  calInverted = prefs.getBool(PREF_KEY_INV, false);

  Serial.print("[CAL] Dry=");
  Serial.print(calDry);
  Serial.print(" Wet=");
  Serial.print(calWet);
  Serial.print(" Inverted=");
  Serial.println(calInverted ? "true" : "false");

  if (!hasCalibrationValues())
  {
    Serial.println("[CAL] Calibration missing or too close. Use 'dry' and 'wet' commands.");
  }
}

static void clearCalibration()
{
  prefs.remove(PREF_KEY_DRY);
  prefs.remove(PREF_KEY_WET);
  prefs.remove(PREF_KEY_INV);
  calDry = 0;
  calWet = 0;
  calInverted = false;
  percentEma = NAN;
  calibrationInProgress = false;
  setCalibrationState(CAL_STATE_NEEDS, true);
  refreshValidityFlags(NAN, true);
  refreshStatus(true);
  Serial.println("[CAL] Cleared calibration.");
}

static uint16_t readTouchAverage(uint8_t samples)
{
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++)
  {
    sum += (uint32_t)touchRead(TOUCH_PIN);
    delay(5);
  }
  return (uint16_t)(sum / samples);
}

static float computePercent(uint16_t raw)
{
  if (!hasCalibrationValues() || calDry == calWet || !probeConnected)
  {
    return NAN;
  }

  const float inputStart = calInverted ? (float)calWet : (float)calDry;
  const float inputEnd = calInverted ? (float)calDry : (float)calWet;
  const float percent = ((float)raw - inputStart) * 100.0f / (inputEnd - inputStart);

  return constrain(percent, 0.0f, 100.0f);
}

static uint16_t readRawValue()
{
  if (simulationEnabled)
  {
    return readSimulatedRaw();
  }
  return readTouchAverage(TOUCH_SAMPLES);
}

static void beginCalibrationCapture()
{
  calibrationInProgress = true;
  setCalibrationState(CAL_STATE_CALIBRATING, true);
  refreshStatus(true);
}

static void finishCalibrationCapture()
{
  calibrationInProgress = false;
  refreshCalibrationState(true);
  refreshValidityFlags(NAN, true);
}

static void captureCalibrationPoint(bool isDry)
{
  beginCalibrationCapture();
  const uint16_t sample = readRawValue();
  lastRawValue = sample;
  updateProbeStatus(sample, true);

  if (isDry)
  {
    calDry = sample;
    prefs.putUShort(PREF_KEY_DRY, calDry);
    Serial.print("[CAL] Captured dry=");
    Serial.println(calDry);
  }
  else
  {
    calWet = sample;
    prefs.putUShort(PREF_KEY_WET, calWet);
    Serial.print("[CAL] Captured wet=");
    Serial.println(calWet);
  }

  percentEma = NAN;
  finishCalibrationCapture();
}

static void handleInvertCalibration()
{
  calInverted = !calInverted;
  prefs.putBool(PREF_KEY_INV, calInverted);
  percentEma = NAN;
  Serial.print("[CAL] Inverted set to ");
  Serial.println(calInverted ? "true" : "false");
  refreshCalibrationState(true);
  refreshValidityFlags(NAN, true);
}

static void handleSerialCommands()
{
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "dry")
  {
    captureCalibrationPoint(true);
  }
  else if (cmd == "wet")
  {
    captureCalibrationPoint(false);
  }
  else if (cmd == "show")
  {
    Serial.print("[CAL] Dry=");
    Serial.print(calDry);
    Serial.print(" Wet=");
    Serial.print(calWet);
    Serial.print(" Inverted=");
    Serial.println(calInverted ? "true" : "false");
    Serial.print("[CAL] Valid=");
    Serial.println(hasCalibrationValues() ? "yes" : "no");
  }
  else if (cmd == "clear")
  {
    clearCalibration();
  }
  else if (cmd == "invert")
  {
    handleInvertCalibration();
  }
  else if (cmd == "help" || cmd.length() > 0)
  {
    printHelpMenu();
  }
}

static bool tryParseFloat(const String &input, float &outValue)
{
  if (input.length() == 0)
    return false;

  char *endPtr = nullptr;
  outValue = strtof(input.c_str(), &endPtr);
  return endPtr != nullptr && endPtr != input.c_str();
}

static bool tryParseBool(const String &input, bool &outValue)
{
  if (input == "1" || input.equalsIgnoreCase("true") || input.equalsIgnoreCase("on"))
  {
    outValue = true;
    return true;
  }
  if (input == "0" || input.equalsIgnoreCase("false") || input.equalsIgnoreCase("off"))
  {
    outValue = false;
    return true;
  }
  return false;
}

static void handleConfigCommand(const String &topic, const String &message)
{
  if (topic == TOPIC_CMD_TANK_VOLUME)
  {
    float value = NAN;
    if (!tryParseFloat(message, value))
      return;
    updateTankVolume(tankLitersFromExternal(value), true);
  }
  else if (topic == TOPIC_CMD_ROD_LENGTH)
  {
    float value = NAN;
    if (!tryParseFloat(message, value))
      return;
    updateRodLength(rodCmFromExternal(value), true);
  }
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  std::unique_ptr<char[]> msgBuf(new char[length + 1]);
  memcpy(msgBuf.get(), payload, length);
  msgBuf[length] = '\0';

  String message(msgBuf.get());
  message.trim();

  const String topicStr(topic);
  const char *msgCStr = msgBuf.get();

  if (topicStr == TOPIC_CMD_CAL_DRY)
  {
    captureCalibrationPoint(true);
    return;
  }
  if (topicStr == TOPIC_CMD_CAL_WET)
  {
    captureCalibrationPoint(false);
    return;
  }
  if (topicStr == TOPIC_CMD_CLEAR_CAL)
  {
    clearCalibration();
    return;
  }

  if (topicStr == TOPIC_CMD_SIM_ENABLED)
  {
    bool value = false;
    if (!tryParseBool(message, value))
      return;
    setSimulationEnabled(value, true, message.c_str());
    Serial.print("[SIM CMD] topic=");
    Serial.print(topicStr);
    Serial.print(" msg=\"");
    Serial.print(message);
    Serial.print("\" -> enabled=");
    Serial.print(simulationEnabled ? "1" : "0");
    Serial.print(" mode=");
    Serial.println(simulationMode);
    return;
  }

  if (topicStr == TOPIC_CMD_SIM_MODE)
  {
    int modeInt = atoi(message.c_str());
    if (modeInt < 0)
      modeInt = 0;
    if (modeInt > 5)
      modeInt = 5;
    setSimulationModeInternal((uint8_t)modeInt, true, message.c_str());
    Serial.print("[SIM CMD] topic=");
    Serial.print(topicStr);
    Serial.print(" msg=\"");
    Serial.print(message);
    Serial.print("\" -> enabled=");
    Serial.print(simulationEnabled ? "1" : "0");
    Serial.print(" mode=");
    Serial.println(simulationMode);
    return;
  }

  handleConfigCommand(topicStr, message);
}

static void subscribeTopics()
{
  if (!mqtt.connected())
    return;

  mqtt.subscribe(TOPIC_CMD_CAL_DRY);
  mqtt.subscribe(TOPIC_CMD_CAL_WET);
  mqtt.subscribe(TOPIC_CMD_CLEAR_CAL);
  mqtt.subscribe(TOPIC_CMD_TANK_VOLUME);
  mqtt.subscribe(TOPIC_CMD_ROD_LENGTH);
  mqtt.subscribe(TOPIC_CMD_SIM_ENABLED);
  mqtt.subscribe(TOPIC_CMD_SIM_MODE);
}

static void publishDiscovery()
{
  if (!ENABLE_DISCOVERY || !mqtt.connected() || discoverySent)
    return;

  String deviceJson = String("{\"name\":\"") + DEVICE_NAME + "\",\"identifiers\":[\"" + DEVICE_ID +
                      "\"],\"manufacturer\":\"" + DEVICE_MANUFACTURER + "\",\"model\":\"" + DEVICE_MODEL +
                      "\",\"sw_version\":\"" + DEVICE_SW_VERSION + "\"}";

  const String availability = String("\"availability_topic\":\"") + TOPIC_TANK_STATUS + "\",\"payload_available\":\"" + STATUS_ONLINE +
                              "\",\"payload_not_available\":\"" + STATUS_OFFLINE + "\"";

  String rawConfig = String("{\"name\":\"Water Tank Raw\",\"state_topic\":\"") + TOPIC_TANK_RAW +
                     "\",\"unique_id\":\"water_tank_raw\"," + availability + ",\"state_class\":\"measurement\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_raw/config").c_str(), rawConfig.c_str(), true);

  String percentConfig = String("{\"name\":\"Water Tank Level\",\"state_topic\":\"") + TOPIC_TANK_PERCENT +
                         "\",\"unique_id\":\"water_tank_percent\",\"unit_of_measurement\":\"%\"," + availability +
                         ",\"state_class\":\"measurement\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_percent/config").c_str(), percentConfig.c_str(), true);

  String litersConfig = String("{\"name\":\"Water Tank Liters\",\"state_topic\":\"") + TOPIC_TANK_LITERS +
                        "\",\"unique_id\":\"water_tank_liters\",\"unit_of_measurement\":\"mL\"," + availability +
                        ",\"state_class\":\"measurement\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_liters/config").c_str(), litersConfig.c_str(), true);

  String cmConfig = String("{\"name\":\"Water Tank Height\",\"state_topic\":\"") + TOPIC_TANK_CM +
                    "\",\"unique_id\":\"water_tank_height_cm\",\"unit_of_measurement\":\"cm\"," + availability +
                    ",\"state_class\":\"measurement\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_height/config").c_str(), cmConfig.c_str(), true);

  String calStateConfig = String("{\"name\":\"Water Tank Calibration State\",\"state_topic\":\"") + TOPIC_TANK_CAL_STATE +
                          "\",\"unique_id\":\"water_tank_cal_state\"," + availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_cal_state/config").c_str(), calStateConfig.c_str(), true);

  String statusConfig = String("{\"name\":\"Water Tank Status\",\"state_topic\":\"") + TOPIC_TANK_STATUS +
                        "\",\"unique_id\":\"water_tank_status\"," + availability + ",\"entity_category\":\"diagnostic\",\"device\":" +
                        deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_status/config").c_str(), statusConfig.c_str(), true);

  String qualityConfig = String("{\"name\":\"Water Tank Quality Reason\",\"state_topic\":\"") + TOPIC_TANK_QUALITY +
                         "\",\"unique_id\":\"water_tank_quality_reason\"," + availability +
                         ",\"entity_category\":\"diagnostic\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_quality_reason/config").c_str(), qualityConfig.c_str(), true);

  String probeConfig = String("{\"name\":\"Probe Connected\",\"state_topic\":\"") + TOPIC_TANK_PROBE +
                       "\",\"unique_id\":\"water_tank_probe_connected\",\"payload_on\":\"1\",\"payload_off\":\"0\"," + availability +
                       ",\"device_class\":\"connectivity\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/binary_sensor/water_tank_probe_connected/config").c_str(), probeConfig.c_str(), true);

  String percentValidConfig = String("{\"name\":\"Percent Valid\",\"state_topic\":\"") + TOPIC_TANK_PERCENT_VALID +
                              "\",\"unique_id\":\"water_tank_percent_valid\",\"payload_on\":\"1\",\"payload_off\":\"0\"," + availability +
                              ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/binary_sensor/water_tank_percent_valid/config").c_str(), percentValidConfig.c_str(), true);

  String rawValidConfig = String("{\"name\":\"Raw Valid\",\"state_topic\":\"") + TOPIC_TANK_RAW_VALID +
                          "\",\"unique_id\":\"water_tank_raw_valid\",\"payload_on\":\"1\",\"payload_off\":\"0\"," + availability +
                          ",\"entity_category\":\"diagnostic\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/binary_sensor/water_tank_raw_valid/config").c_str(), rawValidConfig.c_str(), true);

  String litersValidConfig = String("{\"name\":\"Liters Valid\",\"state_topic\":\"") + TOPIC_TANK_LITERS_VALID +
                             "\",\"unique_id\":\"water_tank_liters_valid\",\"payload_on\":\"1\",\"payload_off\":\"0\"," + availability +
                             ",\"entity_category\":\"diagnostic\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/binary_sensor/water_tank_liters_valid/config").c_str(), litersValidConfig.c_str(), true);

  String cmValidConfig = String("{\"name\":\"Centimeters Valid\",\"state_topic\":\"") + TOPIC_TANK_CM_VALID +
                         "\",\"unique_id\":\"water_tank_cm_valid\",\"payload_on\":\"1\",\"payload_off\":\"0\"," + availability +
                         ",\"entity_category\":\"diagnostic\",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/binary_sensor/water_tank_cm_valid/config").c_str(), cmValidConfig.c_str(), true);

  String simSwitchConfig = String("{\"name\":\"Simulation Enabled\",\"state_topic\":\"") + TOPIC_CFG_SIM_ENABLED +
                           "\",\"command_topic\":\"" + TOPIC_CMD_SIM_ENABLED +
                           "\",\"unique_id\":\"water_tank_sim_enabled\",\"payload_on\":\"1\",\"payload_off\":\"0\",\"entity_category\":\"diagnostic\"," + availability +
                           ",\"device\":" + deviceJson + "}";
  {
    const char *topic = (String(DISCOVERY_PREFIX) + "/switch/water_tank_simulation_enabled/config").c_str();
    const bool ok = mqtt.publish(topic, simSwitchConfig.c_str(), true);
    if (!ok)
    {
      Serial.println("[DISCOVERY] Failed to publish simulation switch config (payload too large?)");
    }
  }

  String simModeConfig = String("{\"name\":\"Simulation Mode\",\"state_topic\":\"") + TOPIC_CFG_SIM_MODE +
                         "\",\"command_topic\":\"" + TOPIC_CMD_SIM_MODE +
                         "\",\"unique_id\":\"water_tank_sim_mode\",\"options\":[\"0\",\"1\",\"2\",\"3\",\"4\",\"5\"],\"entity_category\":\"diagnostic\"," + availability +
                         ",\"device\":" + deviceJson + "}";
  {
    const char *topic = (String(DISCOVERY_PREFIX) + "/select/water_tank_simulation_mode/config").c_str();
    const bool ok = mqtt.publish(topic, simModeConfig.c_str(), true);
    if (!ok)
    {
      Serial.println("[DISCOVERY] Failed to publish simulation select config (payload too large?)");
    }
  }

  String volumeNumberConfig = String("{\"name\":\"Tank Volume\",\"state_topic\":\"") + TOPIC_CFG_TANK_VOLUME +
                              "\",\"command_topic\":\"" + TOPIC_CMD_TANK_VOLUME +
                              "\",\"unique_id\":\"water_tank_volume_number\",\"unit_of_measurement\":\"" + String(CFG_TANK_VOLUME_UNIT) +
                              "\",\"min\":0,\"max\":" + String(CFG_TANK_VOLUME_MAX, 1) + ",\"step\":" + String(CFG_TANK_VOLUME_STEP, 2) + "," +
                              availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/number/water_tank_volume/config").c_str(), volumeNumberConfig.c_str(), true);

  String rodNumberConfig = String("{\"name\":\"Rod Length\",\"state_topic\":\"") + TOPIC_CFG_ROD_LENGTH +
                           "\",\"command_topic\":\"" + TOPIC_CMD_ROD_LENGTH +
                           "\",\"unique_id\":\"water_tank_rod_length_number\",\"unit_of_measurement\":\"" + String(CFG_ROD_LENGTH_UNIT) +
                           "\",\"min\":0,\"max\":" + String(CFG_ROD_LENGTH_MAX, 1) + ",\"step\":" + String(CFG_ROD_LENGTH_STEP, 2) + "," +
                           availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/number/water_tank_rod_length/config").c_str(), rodNumberConfig.c_str(), true);

  String dryButtonConfig = String("{\"name\":\"Calibrate Dry\",\"command_topic\":\"") + TOPIC_CMD_CAL_DRY +
                           "\",\"unique_id\":\"water_tank_calibrate_dry_button\"," + availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/button/water_tank_calibrate_dry/config").c_str(), dryButtonConfig.c_str(), true);

  String wetButtonConfig = String("{\"name\":\"Calibrate Wet\",\"command_topic\":\"") + TOPIC_CMD_CAL_WET +
                           "\",\"unique_id\":\"water_tank_calibrate_wet_button\"," + availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/button/water_tank_calibrate_wet/config").c_str(), wetButtonConfig.c_str(), true);

  String clearButtonConfig = String("{\"name\":\"Clear Calibration\",\"command_topic\":\"") + TOPIC_CMD_CLEAR_CAL +
                             "\",\"unique_id\":\"water_tank_clear_cal_button\"," + availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/button/water_tank_clear_cal/config").c_str(), clearButtonConfig.c_str(), true);

  discoverySent = true;
  Serial.println("[MQTT] Home Assistant discovery published.");
}

static void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  Serial.print("[WIFI] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(250);
    Serial.print(".");

    if (millis() - start > WIFI_TIMEOUT_MS)
    {
      Serial.println();
      logLine("[WIFI] Timeout. Check SSID/password in secrets.h");
      return;
    }
  }

  Serial.println();
  logLine("[WIFI] Connected!");
  Serial.print("[WIFI] IP: ");
  Serial.println(WiFi.localIP());
}

static void connectMQTT()
{
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED)
    return;

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(1024);

  Serial.print("[MQTT] Connecting to ");
  Serial.print(MQTT_HOST);
  Serial.print(":");
  Serial.println(MQTT_PORT);

  while (!mqtt.connected())
  {
    const bool ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, TOPIC_TANK_STATUS, 0, true, STATUS_OFFLINE);

    if (ok)
    {
      logLine("[MQTT] Connected!");
      discoverySent = false;
      publishStatus(STATUS_ONLINE, true, true);
      subscribeTopics();
      publishCalibrationState(true);
      refreshCalibrationState(true);
      publishConfigValues(true);
      publishQualityReason(true);
      publishBoolState(TOPIC_TANK_PROBE, probeConnected, false, publishedProbeConnected, lastPublishedProbeConnected, true);
      publishBoolState(TOPIC_TANK_RAW_VALID, rawValid, false, publishedRawValid, lastPublishedRawValid, true);
      refreshValidityFlags(percentEma, true);
      recomputeDerivedFromPercent(true);
      publishDiscovery();
      break;
    }

    Serial.print("[MQTT] Failed, rc=");
    Serial.print(mqtt.state());
    logLine(". Retrying in 2s...");
    delay(2000);
  }
}

static void ensureConnections()
{
  connectWiFi();
  connectMQTT();
}

// ---------------- Arduino lifecycle ----------------

void setup()
{
  Serial.begin(115200);
  delay(1500);

  logLine("\n[BOOT] water_level_sensor starting...");
  Serial.print("[BOOT] TOUCH_PIN=");
  Serial.println(TOUCH_PIN);

  if (String(WIFI_SSID).length() == 0 || String(MQTT_USER).length() == 0)
  {
    logLine("[BOOT] WARNING: secrets.h looks empty. Wi‑Fi/MQTT may not connect.");
  }

  prefs.begin(PREF_NAMESPACE, false);
  loadCalibration();
  loadConfigValues();
  refreshCalibrationState(true);
  refreshValidityFlags(NAN, true);
  printHelpMenu();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(mqttCallback);

  ensureConnections();
}

void loop()
{
  ensureConnections();

  if (mqtt.connected())
  {
    mqtt.loop();
    publishDiscovery(); // re-publish on reconnect if needed
  }

  handleSerialCommands();

  static uint32_t lastRawPublish = 0;
  static uint32_t lastPercentPublish = 0;
  const uint32_t now = millis();

  if (now - lastRawPublish >= RAW_PUBLISH_MS)
  {
    lastRawPublish = now;
    const uint16_t raw = readRawValue();
    lastRawValue = raw;
    updateProbeStatus(raw);

    if (mqtt.connected())
    {
      mqtt.publish(TOPIC_TANK_RAW, String(raw).c_str());
    }

    Serial.print("[RAW] ");
    Serial.println(raw);
  }

  if (now - lastPercentPublish >= PERCENT_PUBLISH_MS)
  {
    lastPercentPublish = now;
    const float rawPercent = computePercent(lastRawValue);

    if (!isnan(rawPercent))
    {
      if (isnan(percentEma))
      {
        percentEma = rawPercent;
      }
      else
      {
        percentEma = (PERCENT_EMA_ALPHA * rawPercent) + ((1.0f - PERCENT_EMA_ALPHA) * percentEma);
      }
    }
    else
    {
      percentEma = NAN;
    }

    publishPercentAndDerived(percentEma);

    Serial.print("[PERCENT] ");
    if (isnan(percentEma))
    {
      Serial.println("N/A");
    }
    else
    {
      Serial.println(percentEma, 1);
    }
  }
}
