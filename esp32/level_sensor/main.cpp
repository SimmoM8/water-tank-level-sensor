#include <WiFi.h>
#include <math.h>
#include <Arduino.h>
#include <stdlib.h>
#include "main.h"
#include "probe_reader.h"
#include "wifi_provisioning.h"
#include "device_state.h"
#include "ota_service.h"
#include "mqtt_transport.h"
#include "storage_nvs.h"
#include "simulation.h"
#include "commands.h"
#include "applied_config.h"
#include "logger.h"

DeviceState g_state;

// Optional config overrides (see config.h)
#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
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

// --------- MQTT credentials (secrets.h) ---------
#include "secrets.h"

#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASS
#define MQTT_PASS ""
#endif
#ifndef OTA_PASS
#define OTA_PASS "password"
#endif
// -------------------------------------------

// ===== MQTT config =====
static const char *MQTT_HOST = "192.168.0.37";
static const int MQTT_PORT = 1883;

// Use a stable client id (unique per device). If you add more ESP32 devices later,
// change this string so they don't clash.
static const char *MQTT_CLIENT_ID = "water-tank-esp32";
static const char *BASE_TOPIC = "water_tank/water_tank_esp32";

// ===== Device identity =====
static const char *DEVICE_ID = "water_tank_esp32";
static const char *DEVICE_NAME = "Water Tank Sensor";
static const char *DEVICE_FW = "1.3";

// ===== Sensor / Sampling =====
static const int TOUCH_PIN = 14; // GPIO14 or A7
static const uint8_t TOUCH_SAMPLES = 16;
static const uint8_t TOUCH_SAMPLE_DELAY_MS = 5;
static const uint32_t RAW_SAMPLE_MS = 1000;
static const uint32_t PERCENT_SAMPLE_MS = 3000;
static const float PERCENT_EMA_ALPHA = 0.2f;

// ===== Network timeouts =====
static const uint32_t WIFI_TIMEOUT_MS = 20000;

// ===== Runtime state (owned by this module) =====
static bool calibrationInProgress = false;

static int32_t lastRawValue = 0;
static float percentEma = NAN;
static bool probeConnected = false;
static ProbeQualityReason probeQualityReason = ProbeQualityReason::UNKNOWN;

static int32_t calDry = 0;
static int32_t calWet = 0;
static bool calInverted = false;

static float lastLiters = NAN; // Derived caches for change detection
static float lastCentimeters = NAN;
static char s_emptyStr[1] = {0};

static void applyConfigFromCache(bool logValues);
static bool reloadConfigIfDirty(bool logValues);
static void handleSerialCommands();
static void updatePercentFromRaw();
static void refreshStateSnapshot();
static void windowFast();
static void windowSensor();
static void windowCompute();
static void windowStateMeta();
static void windowMqtt();

struct LoopWindow
{
  const char *name;
  uint32_t intervalMs;
  uint32_t lastMs;
  void (*fn)();
};

static void runWindow(LoopWindow &w, uint32_t now)
{
  if (w.intervalMs == 0 || (uint32_t)(now - w.lastMs) >= w.intervalMs)
  {
    w.fn();
    w.lastMs = now;
  }
}

// ----------------- Helpers -----------------

static void logLine(const char *msg)
{
  LOG_INFO(LogDomain::SYSTEM, "%s", msg);
}

static void printHelpMenu()
{
  LOG_INFO(LogDomain::SYSTEM, "[CAL] Serial commands:");
  LOG_INFO(LogDomain::SYSTEM, "  dry   -> capture current averaged raw as dry, save to NVS");
  LOG_INFO(LogDomain::SYSTEM, "  wet   -> capture current averaged raw as wet, save to NVS");
  LOG_INFO(LogDomain::SYSTEM, "  show  -> print current calibration values");
  LOG_INFO(LogDomain::SYSTEM, "  clear -> clear stored calibration");
  LOG_INFO(LogDomain::SYSTEM, "  invert-> toggle inverted flag and save");
  LOG_INFO(LogDomain::SYSTEM, "  wifi  -> start WiFi captive portal (setup mode)");
  LOG_INFO(LogDomain::SYSTEM, "  wipewifi -> clear WiFi creds + reboot into setup portal");
  LOG_INFO(LogDomain::SYSTEM, "  mode touch -> use touchRead()");
  LOG_INFO(LogDomain::SYSTEM, "  mode sim   -> use simulation backend");
  LOG_INFO(LogDomain::SYSTEM, "  help  -> show this menu");
}

static bool hasCalibrationValues()
{
  const AppliedConfig &cfg = config_get();
  return cfg.calDry > 0 && cfg.calWet > 0 && (uint32_t)abs(cfg.calWet - cfg.calDry) >= CFG_CAL_MIN_DIFF;
}

static float clampNonNegative(float value)
{
  return value < 0.0f ? 0.0f : value;
}

static float computePercent(int32_t raw)
{
  if (!hasCalibrationValues() || calDry == calWet || !probeConnected)
  {
    return NAN;
  }

  const AppliedConfig &cfg = config_get();
  const float inputStart = cfg.calInverted ? (float)cfg.calWet : (float)cfg.calDry;
  const float inputEnd = cfg.calInverted ? (float)cfg.calDry : (float)cfg.calWet;
  const float percent = ((float)raw - inputStart) * 100.0f / (inputEnd - inputStart);

  return constrain(percent, 0.0f, 100.0f);
}

static bool evaluateProbeConnected(int32_t raw)
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

  ProbeQualityReason reason = ProbeQualityReason::OK;
  if (raw < CFG_PROBE_DISCONNECTED_BELOW_RAW)
  {
    reason = ProbeQualityReason::DISCONNECTED_LOW_RAW;
  }
  else if (raw < CFG_PROBE_MIN_RAW || raw > CFG_PROBE_MAX_RAW)
  {
    reason = ProbeQualityReason::OUT_OF_BOUNDS;
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
      reason = ProbeQualityReason::UNRELIABLE_RAPID;
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
          reason = ProbeQualityReason::UNRELIABLE_SPIKES;
        }
      }

      if (delta > CFG_STUCK_EPS)
      {
        stuckStartMs = now;
      }
      else if ((now - stuckStartMs) >= CFG_STUCK_MS)
      {
        reason = ProbeQualityReason::UNRELIABLE_STUCK;
      }
    }
  }

  lastRaw = raw;
  hasLast = true;

  probeQualityReason = reason;
  return reason == ProbeQualityReason::OK;
}

static int32_t getRaw()
{
  return (int32_t)probe_getRaw();
}

static void refreshCalibrationState()
{
  CalibrationState nextState = CalibrationState::NEEDS;

  if (calibrationInProgress)
  {
    nextState = CalibrationState::CALIBRATING;
  }
  else if (probeConnected && hasCalibrationValues())
  {
    nextState = CalibrationState::CALIBRATED;
  }

  g_state.calibration.state = nextState;
}

static void refreshValidityFlags(float currentPercent)
{
  g_state.level.percentValid = probeConnected && g_state.calibration.state == CalibrationState::CALIBRATED && !isnan(currentPercent);
  const AppliedConfig &cfg = config_get();
  g_state.level.litersValid = g_state.level.percentValid && !isnan(cfg.tankVolumeLiters);
  g_state.level.centimetersValid = g_state.level.percentValid && !isnan(cfg.rodLengthCm);
}

static void refreshLevelFromPercent(float percent)
{
  g_state.level.percent = percent;
  refreshValidityFlags(percent);

  if (g_state.level.percentValid)
  {
    const AppliedConfig &cfg = config_get();
    const float liters = clampNonNegative(cfg.tankVolumeLiters * percent / 100.0f);
    const float centimeters = clampNonNegative(cfg.rodLengthCm * percent / 100.0f);

    g_state.level.liters = liters;
    g_state.level.litersValid = true;
    g_state.level.centimeters = centimeters;
    g_state.level.centimetersValid = true;

    const bool litersChanged = isnan(lastLiters) || fabs(lastLiters - liters) > 0.01f;
    const bool cmChanged = isnan(lastCentimeters) || fabs(lastCentimeters - centimeters) > 0.01f;
    if (litersChanged || cmChanged)
    {
      mqtt_requestStatePublish();
    }

    lastLiters = liters;
    lastCentimeters = centimeters;
  }
  else
  {
    g_state.level.liters = NAN;
    g_state.level.litersValid = false;
    g_state.level.centimeters = NAN;
    g_state.level.centimetersValid = false;
    lastLiters = NAN;
    lastCentimeters = NAN;
  }
}

static void refreshProbeState(int32_t raw, bool forcePublish)
{
  const bool wasConnected = probeConnected;
  const ProbeQualityReason prevReason = probeQualityReason;

  probeConnected = evaluateProbeConnected(raw);

  g_state.probe.connected = probeConnected;
  g_state.probe.quality = probeQualityReason;
  g_state.probe.raw = raw;
  g_state.probe.rawValid = probeConnected;
  const AppliedConfig &cfg = config_get();
  g_state.probe.senseMode = cfg.simulationEnabled ? SenseMode::SIM : SenseMode::TOUCH;

  refreshCalibrationState();

  if (forcePublish || wasConnected != probeConnected || prevReason != probeQualityReason)
  {
    mqtt_requestStatePublish();
  }
}

static void refreshDeviceMeta()
{
  g_state.schema = STATE_SCHEMA_VERSION;
  g_state.ts = (uint32_t)(millis() / 1000);

  static char ipBuf[16] = {0};
  const IPAddress ip = WiFi.localIP();
  snprintf(ipBuf, sizeof(ipBuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  g_state.device.id = DEVICE_ID;
  g_state.device.name = DEVICE_NAME;
  g_state.device.fw = DEVICE_FW;

  g_state.wifi.rssi = WiFi.RSSI();
  g_state.wifi.ip = ipBuf;

  g_state.mqtt.connected = mqtt_isConnected();

  const AppliedConfig &cfg = config_get();
  g_state.config.tankVolumeLiters = cfg.tankVolumeLiters;
  g_state.config.rodLengthCm = cfg.rodLengthCm;
  g_state.config.simulationEnabled = cfg.simulationEnabled;
  g_state.config.simulationMode = cfg.simulationMode;
}

static void updateTankVolume(float value, bool /*forcePublish*/ = false)
{
  if (isnan(value))
    return;

  const float next = clampNonNegative(value);
  storage_saveTankVolume(next);
  config_markDirty();
}

static void updateRodLength(float value, bool /*forcePublish*/ = false)
{
  if (isnan(value))
    return;

  const float next = clampNonNegative(value);
  storage_saveTankHeight(next);
  config_markDirty();
}

static void clearCalibration()
{
  storage_clearCalibration();
  config_markDirty();
  reloadConfigIfDirty(false);
  calDry = 0;
  calWet = 0;
  calInverted = false;
  calibrationInProgress = false;
  percentEma = NAN;
  g_state.calibration.dry = 0;
  g_state.calibration.wet = 0;
  g_state.calibration.inverted = false;
  refreshCalibrationState();
  mqtt_requestStatePublish();
  LOG_INFO(LogDomain::CAL, "Calibration cleared");
}

static void beginCalibrationCapture()
{
  calibrationInProgress = true;
  refreshCalibrationState();
  mqtt_requestStatePublish();
}

static void finishCalibrationCapture()
{
  calibrationInProgress = false;
  refreshCalibrationState();
  mqtt_requestStatePublish();
}

static void captureCalibrationPoint(bool isDry)
{
  beginCalibrationCapture();
  const uint16_t sample = getRaw();
  lastRawValue = sample;
  refreshProbeState(sample, true);

  if (isDry)
  {
    calDry = sample;
    g_state.calibration.dry = calDry;
    storage_saveCalibrationDry(calDry);
    config_markDirty();
    reloadConfigIfDirty(false);
    LOG_INFO(LogDomain::CAL, "Captured dry=%ld", (long)calDry);
  }
  else
  {
    calWet = sample;
    g_state.calibration.wet = calWet;
    storage_saveCalibrationWet(calWet);
    config_markDirty();
    reloadConfigIfDirty(false);
    LOG_INFO(LogDomain::CAL, "Captured wet=%ld", (long)calWet);
  }

  percentEma = NAN;
  refreshCalibrationState();
  finishCalibrationCapture();
}

static void handleInvertCalibration()
{
  calInverted = !calInverted;
  storage_saveCalibrationInverted(calInverted);
  g_state.calibration.inverted = calInverted;
  config_markDirty();
  reloadConfigIfDirty(false);
  percentEma = NAN;
  refreshCalibrationState();
  mqtt_requestStatePublish();
  LOG_INFO(LogDomain::CAL, "Calibration inverted=%s", calInverted ? "true" : "false");
}

static void setSimulationEnabled(bool enabled, bool /*forcePublish*/ = false, const char * /*sourceMsg*/ = nullptr)
{
  storage_saveSimulationEnabled(enabled);
  config_markDirty();
}

static void setSimulationModeInternal(uint8_t mode, bool /*forcePublish*/ = false, const char * /*sourceMsg*/ = nullptr)
{
  uint8_t clamped = mode > 5 ? 5 : mode;
  storage_saveSimulationMode(clamped);
  config_markDirty();
}

static void applyConfigFromCache(bool logValues)
{
  const AppliedConfig &cfg = config_get();

  calDry = cfg.calDry;
  calWet = cfg.calWet;
  calInverted = cfg.calInverted;

  g_state.calibration.dry = calDry;
  g_state.calibration.wet = calWet;
  g_state.calibration.inverted = calInverted;
  g_state.calibration.minDiff = CFG_CAL_MIN_DIFF;

  g_state.config.tankVolumeLiters = cfg.tankVolumeLiters;
  g_state.config.rodLengthCm = cfg.rodLengthCm;
  g_state.config.simulationEnabled = cfg.simulationEnabled;
  g_state.config.simulationMode = cfg.simulationMode;

  setSimulationMode(cfg.simulationMode);
  probe_updateMode(cfg.simulationEnabled ? READ_SIM : READ_PROBE);

  refreshCalibrationState();

  if (!logValues)
  {
    return;
  }

  LOG_INFO(LogDomain::CONFIG, "[CFG] Tank volume (L): %s", isnan(cfg.tankVolumeLiters) ? "unset" : "set");
  if (!isnan(cfg.tankVolumeLiters))
  {
    LOG_INFO(LogDomain::CONFIG, "[CFG] Tank volume (L) value=%.2f", cfg.tankVolumeLiters);
  }

  LOG_INFO(LogDomain::CONFIG, "[CFG] Rod length (cm): %s", isnan(cfg.rodLengthCm) ? "unset" : "set");
  if (!isnan(cfg.rodLengthCm))
  {
    LOG_INFO(LogDomain::CONFIG, "[CFG] Rod length (cm) value=%.2f", cfg.rodLengthCm);
  }

  LOG_INFO(LogDomain::CONFIG, "[CFG] Simulation enabled: %s", cfg.simulationEnabled ? "true" : "false");
  LOG_INFO(LogDomain::CONFIG, "[CFG] Simulation mode: %u", cfg.simulationMode);

  LOG_INFO(LogDomain::CAL, "[CAL] Dry=%ld Wet=%ld Inverted=%s", (long)calDry, (long)calWet, calInverted ? "true" : "false");

  if (!hasCalibrationValues())
  {
    LOG_WARN(LogDomain::CAL, "[CAL] Calibration missing or too close. Use 'dry' and 'wet' commands.");
  }
}

static bool reloadConfigIfDirty(bool logValues)
{
  if (config_reloadIfDirty())
  {
    applyConfigFromCache(logValues);
    return true;
  }
  return false;
}

static void windowFast()
{
  ota_handle();
  wifi_ensureConnected(WIFI_TIMEOUT_MS);

  if (reloadConfigIfDirty(true))
  {
    LOG_INFO(LogDomain::CONFIG, "Config reloaded from NVS");
    refreshLevelFromPercent(percentEma);
    mqtt_requestStatePublish();
  }

  handleSerialCommands();
}

static void windowSensor()
{
  lastRawValue = getRaw();
  refreshProbeState(lastRawValue, false);
  mqtt_requestStatePublish();
}

static void windowCompute()
{
  updatePercentFromRaw();
}

static void windowStateMeta()
{
  refreshStateSnapshot();
}

static void windowMqtt()
{
  mqtt_tick(g_state);
}

static void handleSerialCommands()
{
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "mode touch")
  {
    setSimulationEnabled(false, true, "serial");
    probe_updateMode(READ_PROBE);
    return;
  }
  if (cmd == "mode sim")
  {
    setSimulationEnabled(true, true, "serial");
    probe_updateMode(READ_SIM);
    return;
  }

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
    LOG_INFO(LogDomain::CAL, "[CAL] Dry=%ld Wet=%ld Inverted=%s", (long)calDry, (long)calWet, calInverted ? "true" : "false");
    LOG_INFO(LogDomain::CAL, "[CAL] Valid=%s", hasCalibrationValues() ? "yes" : "no");
  }
  else if (cmd == "clear")
  {
    clearCalibration();
  }
  else if (cmd == "invert")
  {
    handleInvertCalibration();
  }
  else if (cmd == "wifi")
  {
    wifi_requestPortal();
  }
  else if (cmd == "wipewifi")
  {
    wifi_wipeCredentialsAndReboot();
  }
  else if (cmd == "help")
  {
    printHelpMenu();
  }
  else if (cmd.length() > 0)
  {
    printHelpMenu();
  }
}

static void updatePercentFromRaw()
{
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

  refreshLevelFromPercent(percentEma);
}

static void refreshStateSnapshot()
{
  refreshDeviceMeta();
  g_state.calibration.dry = calDry;
  g_state.calibration.wet = calWet;
  g_state.calibration.inverted = calInverted;
  g_state.calibration.minDiff = CFG_CAL_MIN_DIFF;
}

static LoopWindow g_windows[] = {
    {"FAST", 0u, 0u, windowFast},
    {"SENSOR", RAW_SAMPLE_MS, 0u, windowSensor},
    {"COMPUTE", PERCENT_SAMPLE_MS, 0u, windowCompute},
    {"STATE_META", 1000u, 0u, windowStateMeta},
    {"MQTT", 0u, 0u, windowMqtt}};

// ---------------- Arduino lifecycle ----------------

void appSetup()
{
  Serial.begin(115200);
  delay(1500);
  logger_begin(BASE_TOPIC, true, true);
  logger_setHighFreqEnabled(false);
  LOG_INFO(LogDomain::SYSTEM, "BOOT water_level_sensor starting...");
  LOG_INFO(LogDomain::SYSTEM, "TOUCH_PIN=%d", TOUCH_PIN);

  storage_begin();
  wifi_begin();
  config_begin();

  probe_begin({(uint8_t)TOUCH_PIN, TOUCH_SAMPLES, TOUCH_SAMPLE_DELAY_MS});
  applyConfigFromCache(true);
  refreshStateSnapshot();
  printHelpMenu();

  ota_begin(DEVICE_ID, OTA_PASS);

  g_state.lastCmd.requestId = s_emptyStr;
  g_state.lastCmd.type = s_emptyStr;
  g_state.lastCmd.message = s_emptyStr;
  g_state.lastCmd.status = CmdStatus::RECEIVED;
  g_state.lastCmd.ts = g_state.ts;

  g_state.level.percent = NAN;
  g_state.level.liters = NAN;
  g_state.level.centimeters = NAN;
  g_state.level.percentValid = false;
  g_state.level.litersValid = false;
  g_state.level.centimetersValid = false;

  CommandsContext cmdCtx{
      .state = &g_state,
      .updateTankVolume = updateTankVolume,
      .updateRodLength = updateRodLength,
      .captureCalibrationPoint = captureCalibrationPoint,
      .clearCalibration = clearCalibration,
      .setSimulationEnabled = setSimulationEnabled,
      .setSimulationModeInternal = setSimulationModeInternal,
      .requestStatePublish = mqtt_requestStatePublish,
      .publishAck = mqtt_publishAck};
  commands_begin(cmdCtx);

  MqttConfig mqttCfg{
      .host = MQTT_HOST,
      .port = MQTT_PORT,
      .clientId = MQTT_CLIENT_ID,
      .user = MQTT_USER,
      .pass = MQTT_PASS,
      .baseTopic = BASE_TOPIC,
      .deviceId = DEVICE_ID};
  mqtt_begin(mqttCfg, commands_handle);
}

void appLoop()
{
  const uint32_t now = millis();
  for (LoopWindow &w : g_windows)
  {
    runWindow(w, now);
  }
}
