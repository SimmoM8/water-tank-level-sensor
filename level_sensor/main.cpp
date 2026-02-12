#include <WiFi.h>
#include <math.h>
#include <Arduino.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <esp_system.h>
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
#include "quality.h"
#include "version.h"

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
#ifndef CFG_CAL_RECOMMEND_MARGIN
#define CFG_CAL_RECOMMEND_MARGIN 2000u
#endif
#ifndef CFG_CAL_RECOMMEND_COUNT
#define CFG_CAL_RECOMMEND_COUNT 3u
#endif
#ifndef CFG_CAL_RECOMMEND_WINDOW_MS
#define CFG_CAL_RECOMMEND_WINDOW_MS 60000u
#endif
#ifndef CFG_ZERO_HIT_COUNT
#define CFG_ZERO_HIT_COUNT 2u
#endif
#ifndef CFG_ZERO_WINDOW_MS
#define CFG_ZERO_WINDOW_MS 5000u
#endif
#ifndef CFG_RAW_SAMPLE_MS
#define CFG_RAW_SAMPLE_MS 1000u
#endif
#ifndef CFG_PERCENT_SAMPLE_MS
#define CFG_PERCENT_SAMPLE_MS 3000u
#endif
#ifndef CFG_PERCENT_EMA_ALPHA
#define CFG_PERCENT_EMA_ALPHA 0.2f
#endif
#ifndef CFG_CRASH_MAX_BAD_BOOTS
#define CFG_CRASH_MAX_BAD_BOOTS 3u
#endif
#ifndef CFG_CRASH_GOOD_BOOT_AFTER_MS
#define CFG_CRASH_GOOD_BOOT_AFTER_MS 90000u
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
static const char *MQTT_HOST = "192.168.0.198";
static const int MQTT_PORT = 1883;

// Use a stable client id (unique per device). If you add more ESP32 devices later,
// change this string so they don't clash.
static const char *MQTT_CLIENT_ID = "water-tank-esp32";
static const char *BASE_TOPIC = "water_tank/water_tank_esp32";

// ===== Device identity =====
static const char *DEVICE_ID = "water_tank_esp32";
static const char *DEVICE_NAME = "Water Tank Sensor";
static constexpr char DEVICE_FW[] = FW_VERSION;
static constexpr char DEVICE_HW[] = HW_VERSION;
static_assert(sizeof(DEVICE_FW) == fw_version::kSizeWithNul, "FW_VERSION literal size mismatch");
static_assert((sizeof(DEVICE_FW) - 1) < DEVICE_FW_VERSION_MAX,
              "FW_VERSION too long: DEVICE_FW_VERSION_MAX must include the trailing NUL");
static_assert(sizeof(DEVICE_HW) == hw_version::kSizeWithNul, "HW_VERSION literal size mismatch");

// ===== Sensor / Sampling =====
static const int TOUCH_PIN = 14; // GPIO14 or A7
static const uint8_t TOUCH_SAMPLES = 8;
static const uint8_t TOUCH_SAMPLE_DELAY_MS = 5;
static const uint32_t RAW_SAMPLE_MS = CFG_RAW_SAMPLE_MS;
static const uint32_t PERCENT_SAMPLE_MS = CFG_PERCENT_SAMPLE_MS;
static const float PERCENT_EMA_ALPHA = CFG_PERCENT_EMA_ALPHA;
static constexpr uint8_t SIM_MODE_MAX = 5;
static constexpr float LEVEL_CHANGE_EPS = 0.01f;
static constexpr size_t SERIAL_CMD_BUF = 64;
static constexpr char SERIAL_CMD_DELIMS[] = " \t";

static_assert(TOUCH_SAMPLES > 0, "TOUCH_SAMPLES must be > 0");
static_assert(CFG_PERCENT_EMA_ALPHA >= 0.0f && CFG_PERCENT_EMA_ALPHA <= 1.0f, "CFG_PERCENT_EMA_ALPHA must be 0..1");

static const uint32_t OTA_MANIFEST_CHECK_MS = 21600000u; // 6h
static const uint32_t OTA_MANIFEST_RETRY_MS = 60000u;    // 60s on failure
static uint32_t s_lastManifestCheckMs = 0;
static uint32_t s_lastManifestAttemptMs = 0;

// ===== Network timeouts =====
static const uint32_t WIFI_TIMEOUT_MS = 20000;

// ===== Runtime state (owned by this module) =====
static bool calibrationInProgress = false;

static int32_t lastRawValue = 0;
static float percentEma = NAN;
static bool probeConnected = false;
static ProbeQualityReason probeQualityReason = ProbeQualityReason::UNKNOWN;
static QualityRuntime probeQualityRt{};

static int32_t calDry = 0;
static int32_t calWet = 0;
static bool calInverted = false;

static float lastLiters = NAN; // Derived caches for change detection
static float lastCentimeters = NAN;
static char s_emptyStr[1] = {0};
static bool s_goodBootMarked = false;

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
static void setCalibrationDryValue(int32_t value, const char *sourceMsg = nullptr);
static void setCalibrationWetValue(int32_t value, const char *sourceMsg = nullptr);

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

static const char *mapResetReason(esp_reset_reason_t reason)
{
  switch (reason)
  {
  case ESP_RST_POWERON:
    return "power_on";
  case ESP_RST_SW:
    return "software_reset";
#ifdef ESP_RST_PANIC
  case ESP_RST_PANIC:
    return "panic";
#endif
  case ESP_RST_DEEPSLEEP:
    return "deep_sleep";
  case ESP_RST_INT_WDT:
#ifdef ESP_RST_TASK_WDT
  case ESP_RST_TASK_WDT:
#endif
  case ESP_RST_WDT:
    return "watchdog";
  default:
    return "other";
  }
}

static const char *rebootIntentLabel(uint8_t intent)
{
  switch ((RebootIntent)intent)
  {
  case RebootIntent::NONE:
    return "none";
  case RebootIntent::OTA:
    return "ota";
  case RebootIntent::WIFI_WIPE:
    return "wifi_wipe";
  case RebootIntent::USER_CMD:
    return "user_cmd";
  case RebootIntent::OTHER:
    return "other";
  }
  return "other";
}

static uint8_t normalizeRebootIntent(uint8_t intent)
{
  if (intent > (uint8_t)RebootIntent::OTHER)
  {
    return (uint8_t)RebootIntent::OTHER;
  }
  return intent;
}

enum class BootClassification : uint8_t
{
  BAD = 0,
  INTENTIONAL,
  NEUTRAL
};

static BootClassification classifyBoot(const char *resetReason, uint8_t rebootIntent)
{
  if (!resetReason || resetReason[0] == '\0')
  {
    return BootClassification::NEUTRAL;
  }
  if (strcmp(resetReason, "watchdog") == 0 || strcmp(resetReason, "panic") == 0)
  {
    return BootClassification::BAD;
  }
  if (strcmp(resetReason, "software_reset") == 0)
  {
    return (rebootIntent == (uint8_t)RebootIntent::NONE) ? BootClassification::BAD : BootClassification::INTENTIONAL;
  }
  return BootClassification::NEUTRAL;
}

static const char *bootClassificationLabel(BootClassification cls)
{
  switch (cls)
  {
  case BootClassification::BAD:
    return "bad";
  case BootClassification::INTENTIONAL:
    return "intentional";
  case BootClassification::NEUTRAL:
    return "neutral";
  }
  return "neutral";
}

static void applySafeModeState(bool enabled, const char *reason)
{
  g_state.safe_mode = enabled;
  strncpy(g_state.safe_mode_reason, reason ? reason : "", sizeof(g_state.safe_mode_reason));
  g_state.safe_mode_reason[sizeof(g_state.safe_mode_reason) - 1] = '\0';

  // Keep compatibility crash-loop fields aligned.
  g_state.crash_loop = enabled;
  strncpy(g_state.crash_loop_reason, reason ? reason : "", sizeof(g_state.crash_loop_reason));
  g_state.crash_loop_reason[sizeof(g_state.crash_loop_reason) - 1] = '\0';
}

static void logSafeModeStatus()
{
  LOG_INFO(LogDomain::SYSTEM, "safe_mode=%s bad_boot_streak=%lu reason=%s last_good_boot_ts=%lu",
           g_state.safe_mode ? "true" : "false",
           (unsigned long)g_state.bad_boot_streak,
           g_state.safe_mode_reason,
           (unsigned long)g_state.last_good_boot_ts);
}

static void printHelpMenu()
{
  LOG_INFO(LogDomain::SYSTEM, "[CAL] Serial commands:");
  LOG_INFO(LogDomain::SYSTEM, "  dry   -> capture current averaged raw as dry, save to NVS");
  LOG_INFO(LogDomain::SYSTEM, "  wet   -> capture current averaged raw as wet, save to NVS");
  LOG_INFO(LogDomain::SYSTEM, "  show  -> print current NVS contents / internal state");
  LOG_INFO(LogDomain::SYSTEM, "  clear -> clear stored calibration");
  LOG_INFO(LogDomain::SYSTEM, "  invert-> toggle inverted flag and save");
  LOG_INFO(LogDomain::SYSTEM, "  wifi  -> start WiFi captive portal (setup mode)");
  LOG_INFO(LogDomain::SYSTEM, "  wipewifi -> clear WiFi creds + reboot into setup portal");
  LOG_INFO(LogDomain::SYSTEM, "  safe_mode -> show safe mode status");
  LOG_INFO(LogDomain::SYSTEM, "  safe_mode clear -> clear safe mode and reset bad-boot streak");
  LOG_INFO(LogDomain::SYSTEM, "  safe_mode enter -> force safe mode on (testing)");
  LOG_INFO(LogDomain::SYSTEM, "  log hf on/off -> enable/disable high-frequency logs");
  LOG_INFO(LogDomain::SYSTEM, "  sim <0-5> -> set simulation mode and enable sim backend");
  LOG_INFO(LogDomain::SYSTEM, "  mode touch -> use touchRead()");
  LOG_INFO(LogDomain::SYSTEM, "  mode sim   -> use simulation backend");
  LOG_INFO(LogDomain::SYSTEM, "  ota <url> <sha256> -> start force pull-OTA from serial");
  LOG_INFO(LogDomain::SYSTEM, "  help  -> show this menu");
}

static int32_t clampNonNegativeInt32(int32_t value)
{
  return value < 0 ? 0 : value;
}

static uint8_t clampSimulationMode(int value)
{
  if (value < 0)
  {
    return 0;
  }
  if (value > SIM_MODE_MAX)
  {
    return SIM_MODE_MAX;
  }
  return (uint8_t)value;
}

static bool parseInt(const char *s, int &out)
{
  if (!s || *s == '\0')
  {
    return false;
  }
  char *end = nullptr;
  long v = strtol(s, &end, 10);
  if (!end || *end != '\0')
  {
    return false;
  }
  out = (int)v;
  return true;
}

static bool isHex64(const char *s)
{
  if (!s)
  {
    return false;
  }
  for (int i = 0; i < 64; i++)
  {
    const char c = s[i];
    if (c == '\0')
    {
      return false;
    }
    if (!((c >= '0' && c <= '9') ||
          (c >= 'a' && c <= 'f') ||
          (c >= 'A' && c <= 'F')))
    {
      return false;
    }
  }
  return s[64] == '\0';
}

static bool readSerialLine(char *buf, size_t bufSize)
{
  if (!buf || bufSize < 2 || !Serial.available())
  {
    return false;
  }

  size_t len = Serial.readBytesUntil('\n', buf, bufSize - 1);
  if (len == 0)
  {
    return false;
  }

  while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ' || buf[len - 1] == '\t'))
  {
    len--;
  }
  buf[len] = '\0';

  size_t start = 0;
  while (buf[start] != '\0' && (buf[start] == ' ' || buf[start] == '\t'))
  {
    start++;
  }
  if (start > 0)
  {
    memmove(buf, buf + start, len - start + 1);
    len -= start;
  }

  for (size_t i = 0; i < len; i++)
  {
    buf[i] = (char)tolower((unsigned char)buf[i]);
  }

  return buf[0] != '\0';
}

static bool hasCalibrationValues()
{
  const AppliedConfig &cfg = config_get();
  const int32_t diff = (cfg.calDry > cfg.calWet) ? (cfg.calDry - cfg.calWet) : (cfg.calWet - cfg.calDry);
  return cfg.calDry > 0 && cfg.calWet > 0 && (uint32_t)diff >= CFG_CAL_MIN_DIFF;
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

    const bool litersChanged = isnan(lastLiters) || fabs(lastLiters - liters) > LEVEL_CHANGE_EPS;
    const bool cmChanged = isnan(lastCentimeters) || fabs(lastCentimeters - centimeters) > LEVEL_CHANGE_EPS;
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

  const AppliedConfig &cfg = config_get();
  QualityConfig qc{
      .disconnectedBelowRaw = CFG_PROBE_DISCONNECTED_BELOW_RAW,
      .rawMin = CFG_PROBE_MIN_RAW,
      .rawMax = CFG_PROBE_MAX_RAW,
      .rapidFluctuationDelta = CFG_RAPID_FLUCTUATION_DELTA,
      .spikeDelta = CFG_SPIKE_DELTA,
      .spikeCountThreshold = (uint8_t)CFG_SPIKE_COUNT_THRESHOLD,
      .spikeWindowMs = CFG_SPIKE_WINDOW_MS,
      .stuckDelta = CFG_STUCK_EPS,
      .stuckMs = CFG_STUCK_MS,
      .calRecommendMargin = CFG_CAL_RECOMMEND_MARGIN,
      .calRecommendCount = (uint8_t)CFG_CAL_RECOMMEND_COUNT,
      .calRecommendWindowMs = CFG_CAL_RECOMMEND_WINDOW_MS,
      .zeroHitCount = (uint8_t)CFG_ZERO_HIT_COUNT,
      .zeroWindowMs = CFG_ZERO_WINDOW_MS};

  const QualityResult qr = quality_evaluate((uint32_t)raw, cfg, qc, probeQualityRt, millis());

  probeConnected = qr.connected;
  probeQualityReason = qr.reason;

  g_state.probe.connected = probeConnected;
  g_state.probe.quality = probeQualityReason;
  g_state.probe.raw = raw;
  g_state.probe.rawValid = probeConnected;
  g_state.probe.senseMode = cfg.senseMode;

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
  strncpy(g_state.fw_version, DEVICE_FW, sizeof(g_state.fw_version));
  g_state.fw_version[sizeof(g_state.fw_version) - 1] = '\0';
  g_state.device.fw = g_state.fw_version;

  g_state.wifi.rssi = WiFi.RSSI();
  g_state.wifi.ip = ipBuf;

  g_state.mqtt.connected = mqtt_isConnected();

  WifiTimeSyncStatus timeStatus{};
  wifi_getTimeSyncStatus(timeStatus);
  g_state.time.valid = timeStatus.valid;
  strncpy(g_state.time.status, timeStatus.status ? timeStatus.status : "time_not_set", sizeof(g_state.time.status));
  g_state.time.status[sizeof(g_state.time.status) - 1] = '\0';
  g_state.time.last_attempt_s = timeStatus.lastAttemptMs / 1000u;
  g_state.time.last_success_s = timeStatus.lastSuccessMs / 1000u;
  g_state.time.next_retry_s = timeStatus.nextRetryMs / 1000u;

  const AppliedConfig &cfg = config_get();
  g_state.config.tankVolumeLiters = cfg.tankVolumeLiters;
  g_state.config.rodLengthCm = cfg.rodLengthCm;
  g_state.config.senseMode = cfg.senseMode;
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

static void wipeWifiCredentials()
{
  LOG_WARN(LogDomain::WIFI, "Wipe WiFi credentials requested via command");
  storage_saveRebootIntent((uint8_t)RebootIntent::WIFI_WIPE);
  wifi_wipeCredentialsAndReboot();
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
  const int32_t sample = getRaw();
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

static void setSenseMode(SenseMode mode, bool /*forcePublish*/ = false, const char * /*sourceMsg*/ = nullptr)
{
  storage_saveSenseMode(mode);
  g_state.config.senseMode = mode;
  probe_updateMode(mode == SenseMode::SIM ? READ_SIM : READ_PROBE);
  if (mode == SenseMode::SIM)
  {
    sim_start(g_state.probe.raw);
  }
  config_markDirty();
  mqtt_requestStatePublish();
}

static void setSimulationModeInternal(uint8_t mode, bool /*forcePublish*/ = false, const char * /*sourceMsg*/ = nullptr)
{
  uint8_t clamped = clampSimulationMode(mode);
  storage_saveSimulationMode(clamped);
  g_state.config.simulationMode = clamped;
  setSimulationMode(clamped);
  config_markDirty();
  mqtt_requestStatePublish();
}

static void setCalibrationValueInternal(int32_t value, bool isDry, const char *sourceMsg)
{
  const int32_t clamped = clampNonNegativeInt32(value);
  if (isDry)
  {
    calDry = clamped;
    g_state.calibration.dry = calDry;
    storage_saveCalibrationDry(calDry);
  }
  else
  {
    calWet = clamped;
    g_state.calibration.wet = calWet;
    storage_saveCalibrationWet(calWet);
  }

  config_markDirty();
  reloadConfigIfDirty(false);
  refreshCalibrationState();
  mqtt_requestStatePublish();
  LOG_INFO(LogDomain::CAL, "Calibration %s set to %ld (%s)", isDry ? "dry" : "wet", (long)clamped, sourceMsg ? sourceMsg : "");
}

static void setCalibrationDryValue(int32_t value, const char *sourceMsg)
{
  setCalibrationValueInternal(value, true, sourceMsg);
}

static void setCalibrationWetValue(int32_t value, const char *sourceMsg)
{
  setCalibrationValueInternal(value, false, sourceMsg);
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
  g_state.config.senseMode = cfg.senseMode;
  g_state.config.simulationMode = cfg.simulationMode;

  setSimulationMode(cfg.simulationMode);
  probe_updateMode(cfg.senseMode == SenseMode::SIM ? READ_SIM : READ_PROBE);

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

  LOG_INFO(LogDomain::CONFIG, "[CFG] Sense mode: %s", cfg.senseMode == SenseMode::SIM ? "SIM" : "TOUCH");
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
  ota_tick(&g_state); // NEW pull-OTA
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
  logger_logEvery("raw_sample", 1000, LogLevel::DEBUG, LogDomain::PROBE,
                  "raw=%ld connected=%s quality=%d", (long)lastRawValue,
                  probeConnected ? "true" : "false", (int)probeQualityReason);
  mqtt_requestStatePublish();
}

static void windowCompute()
{
  updatePercentFromRaw();
}

static void maybeCheckManifest()
{
  if (g_state.safe_mode)
  {
    logger_logEvery("ota_manifest_safe_mode", 30000u, LogLevel::DEBUG, LogDomain::OTA,
                    "Skipping manifest check: safe_mode=true reason=%s",
                    g_state.safe_mode_reason);
    return;
  }

  if (!WiFi.isConnected() || ota_isBusy())
    return;

  const uint32_t now = millis();
  const bool due = (s_lastManifestCheckMs == 0) || (now - s_lastManifestCheckMs >= OTA_MANIFEST_CHECK_MS);
  const bool retryOk = (s_lastManifestAttemptMs == 0) || (now - s_lastManifestAttemptMs >= OTA_MANIFEST_RETRY_MS);
  if (!due || !retryOk)
    return;

  s_lastManifestAttemptMs = now;
  char err[32] = {0};
  if (ota_checkManifest(&g_state, err, sizeof(err)))
  {
    s_lastManifestCheckMs = now;
    mqtt_requestStatePublish();
  }
}

static void windowStateMeta()
{
  refreshStateSnapshot();
  maybeCheckManifest();
}

static void windowMqtt()
{
  mqtt_tick(g_state);
}

static void handleSerialCommands()
{
  char line[SERIAL_CMD_BUF];
  if (!readSerialLine(line, sizeof(line)))
  {
    return;
  }

  char *save = nullptr;
  char *cmd = strtok_r(line, SERIAL_CMD_DELIMS, &save);
  if (!cmd || *cmd == '\0')
  {
    return;
  }

  if (strcmp(cmd, "mode") == 0)
  {
    const char *mode = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    if (!mode)
    {
      printHelpMenu();
      return;
    }
    if (strcmp(mode, "touch") == 0)
    {
      setSenseMode(SenseMode::TOUCH, true, "serial");
      return;
    }
    if (strcmp(mode, "sim") == 0)
    {
      const char *modeStr = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
      if (!modeStr)
      {
        setSenseMode(SenseMode::SIM, true, "serial");
        return;
      }
      int modeVal = 0;
      if (!parseInt(modeStr, modeVal))
      {
        printHelpMenu();
        return;
      }
      const uint8_t clamped = clampSimulationMode(modeVal);
      setSenseMode(SenseMode::SIM, true, "serial");
      setSimulationModeInternal(clamped, true, "serial");
      LOG_INFO(LogDomain::SYSTEM, "Simulation mode set to %u (serial)", (unsigned)clamped);
      return;
    }
    printHelpMenu();
    return;
  }

  if (strcmp(cmd, "sim") == 0)
  {
    const char *modeStr = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    int modeVal = 0;
    if (!modeStr || !parseInt(modeStr, modeVal))
    {
      printHelpMenu();
      return;
    }
    const uint8_t clamped = clampSimulationMode(modeVal);
    setSenseMode(SenseMode::SIM, true, "serial");
    setSimulationModeInternal(clamped, true, "serial");
    LOG_INFO(LogDomain::SYSTEM, "Simulation mode set to %u (serial)", (unsigned)clamped);
    return;
  }

  if (strcmp(cmd, "dry") == 0)
  {
    captureCalibrationPoint(true);
    return;
  }
  if (strcmp(cmd, "wet") == 0)
  {
    captureCalibrationPoint(false);
    return;
  }
  if (strcmp(cmd, "show") == 0)
  {
    LOG_INFO(LogDomain::CAL, "[CAL] Dry=%ld Wet=%ld Inverted=%s", (long)calDry, (long)calWet, calInverted ? "true" : "false");
    LOG_INFO(LogDomain::CAL, "[CAL] Valid=%s", hasCalibrationValues() ? "yes" : "no");
    storage_dump();
    return;
  }
  if (strcmp(cmd, "clear") == 0)
  {
    clearCalibration();
    return;
  }
  if (strcmp(cmd, "invert") == 0)
  {
    handleInvertCalibration();
    return;
  }

  if (strcmp(cmd, "log") == 0 || strcmp(cmd, "loghf") == 0)
  {
    const bool isLogHf = strcmp(cmd, "loghf") == 0;
    const char *arg1 = isLogHf ? "hf" : strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    const char *arg2 = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    if (arg1 && strcmp(arg1, "hf") == 0 && arg2)
    {
      if (strcmp(arg2, "on") == 0)
      {
        logger_setHighFreqEnabled(true);
        LOG_INFO(LogDomain::SYSTEM, "High-frequency logging enabled (serial command)");
        return;
      }
      if (strcmp(arg2, "off") == 0)
      {
        logger_setHighFreqEnabled(false);
        LOG_INFO(LogDomain::SYSTEM, "High-frequency logging disabled (serial command)");
        return;
      }
    }
    printHelpMenu();
    return;
  }

  if (strcmp(cmd, "wifi") == 0)
  {
    wifi_requestPortal();
    return;
  }
  if (strcmp(cmd, "wipewifi") == 0)
  {
    wipeWifiCredentials();
    return;
  }
  if (strcmp(cmd, "safe_mode") == 0)
  {
    const char *sub = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    if (!sub || strcmp(sub, "status") == 0)
    {
      logSafeModeStatus();
      return;
    }
    if (strcmp(sub, "clear") == 0)
    {
      storage_saveSafeMode(false);
      storage_saveBadBootStreak(0u);
      g_state.bad_boot_streak = 0u;
      g_state.crash_window_boots = 0u;
      g_state.crash_window_bad = 0u;
      applySafeModeState(false, "cleared");
      s_goodBootMarked = true;
      mqtt_requestStatePublish();
      LOG_INFO(LogDomain::SYSTEM, "Safe mode cleared via serial command");
      return;
    }
    if (strcmp(sub, "enter") == 0)
    {
      storage_saveSafeMode(true);
      applySafeModeState(true, "forced");
      mqtt_requestStatePublish();
      LOG_INFO(LogDomain::SYSTEM, "Safe mode forced on via serial command");
      return;
    }
    printHelpMenu();
    return;
  }
  if (strcmp(cmd, "ota") == 0)
  {
    const char *url = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    const char *sha256 = strtok_r(nullptr, SERIAL_CMD_DELIMS, &save);
    if (!url || !sha256 || url[0] == '\0' || sha256[0] == '\0')
    {
      LOG_WARN(LogDomain::OTA, "OTA serial rejected: missing_url_or_sha256");
      return;
    }
    LOG_INFO(LogDomain::OTA, "SHA len=%u last_char=0x%02X",
             strlen(sha256),
             (unsigned char)sha256[strlen(sha256)]);
    if (!isHex64(sha256))
    {
      LOG_WARN(LogDomain::OTA, "OTA serial rejected: bad_sha256_format");
      return;
    }
    if (ota_isBusy())
    {
      LOG_WARN(LogDomain::OTA, "OTA serial rejected: busy");
      return;
    }
    if (!WiFi.isConnected())
    {
      LOG_WARN(LogDomain::OTA, "OTA serial rejected: wifi_disconnected");
      return;
    }

    char errBuf[48] = {0};
    LOG_INFO(LogDomain::OTA, "OTA serial start: url=%s", url);
    const bool ok = ota_pullStart(
        &g_state,
        "serial_test",
        "dev-test",
        url,
        sha256,
        true,
        true,
        errBuf,
        sizeof(errBuf));
    if (!ok)
    {
      LOG_WARN(LogDomain::OTA, "OTA serial start failed: %s", errBuf[0] ? errBuf : "start_failed");
    }
    return;
  }
  if (strcmp(cmd, "help") == 0)
  {
    printHelpMenu();
    return;
  }

  printHelpMenu();
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

// Contract: call once after boot. Initializes subsystems, state, and MQTT/OTA handlers.
void appSetup()
{
  const char *bootReason = mapResetReason(esp_reset_reason());
  strncpy(g_state.reset_reason, bootReason, sizeof(g_state.reset_reason));
  g_state.reset_reason[sizeof(g_state.reset_reason) - 1] = '\0';

  Serial.begin(115200);
  delay(1500);
  logger_begin(BASE_TOPIC, true, true);
  logger_setHighFreqEnabled(false);
  quality_init(probeQualityRt);
  LOG_INFO(LogDomain::SYSTEM, "BOOT water_level_sensor starting...");
  LOG_INFO(LogDomain::SYSTEM, "Reset reason=%s", g_state.reset_reason);
  LOG_INFO(LogDomain::SYSTEM, "TOUCH_PIN=%d", TOUCH_PIN);

  storage_begin();
  {
    uint32_t persistedBootCount = 0u;
    storage_loadBootCount(persistedBootCount);
    g_state.boot_count = persistedBootCount + 1u;
    storage_saveBootCount(g_state.boot_count);

    uint8_t rebootIntent = (uint8_t)RebootIntent::NONE;
    storage_loadRebootIntent(rebootIntent);
    rebootIntent = normalizeRebootIntent(rebootIntent);
    if (rebootIntent != (uint8_t)RebootIntent::NONE)
    {
      storage_clearRebootIntent();
    }
    g_state.reboot_intent = rebootIntent;
    const char *intentLabel = rebootIntentLabel(rebootIntent);
    strncpy(g_state.reboot_intent_label, intentLabel, sizeof(g_state.reboot_intent_label));
    g_state.reboot_intent_label[sizeof(g_state.reboot_intent_label) - 1] = '\0';

    uint32_t badBootStreak = 0u;
    uint32_t lastGoodBootTs = 0u;
    bool safeMode = false;
    storage_loadBadBootStreak(badBootStreak);
    storage_loadGoodBootTs(lastGoodBootTs);
    storage_loadSafeMode(safeMode);

    const BootClassification cls = classifyBoot(g_state.reset_reason, rebootIntent);
    const bool badBoot = (cls == BootClassification::BAD);
    LOG_INFO(LogDomain::SYSTEM,
             "Boot classification reset_reason=%s reboot_intent=%s class=%s bad_boot=%s",
             g_state.reset_reason,
             g_state.reboot_intent_label,
             bootClassificationLabel(cls),
             badBoot ? "true" : "false");

    if (badBoot)
    {
      badBootStreak++;
      storage_saveBadBootStreak(badBootStreak);
    }

    if (badBootStreak >= (uint32_t)CFG_CRASH_MAX_BAD_BOOTS)
    {
      safeMode = true;
      storage_saveSafeMode(true);
      LOG_WARN(LogDomain::SYSTEM, "Entering safe mode: bad_boot_streak=%lu threshold=%u",
               (unsigned long)badBootStreak,
               (unsigned int)CFG_CRASH_MAX_BAD_BOOTS);
    }

    g_state.bad_boot_streak = badBootStreak;
    g_state.last_good_boot_ts = lastGoodBootTs;
    g_state.safe_mode = safeMode;
    strncpy(g_state.safe_mode_reason, safeMode ? "crash_loop" : "none", sizeof(g_state.safe_mode_reason));
    g_state.safe_mode_reason[sizeof(g_state.safe_mode_reason) - 1] = '\0';

    // Keep existing crash fields aligned for compatibility with older dashboards.
    g_state.crash_loop = safeMode;
    strncpy(g_state.crash_loop_reason, safeMode ? "crash_loop" : bootClassificationLabel(cls), sizeof(g_state.crash_loop_reason));
    g_state.crash_loop_reason[sizeof(g_state.crash_loop_reason) - 1] = '\0';
    g_state.crash_window_boots = badBootStreak;
    g_state.crash_window_bad = badBootStreak;
    if (!safeMode && badBootStreak == 0u)
    {
      g_state.last_stable_boot = g_state.boot_count;
    }

    LOG_INFO(LogDomain::SYSTEM,
             "Crash/safe-mode state streak=%lu safe_mode=%s last_good_boot_ts=%lu",
             (unsigned long)g_state.bad_boot_streak,
             g_state.safe_mode ? "true" : "false",
             (unsigned long)g_state.last_good_boot_ts);

    if (badBoot)
    {
      LOG_WARN(LogDomain::SYSTEM, "Bad boot observed: streak now %lu", (unsigned long)g_state.bad_boot_streak);
    }

    if (g_state.safe_mode)
    {
      LOG_WARN(LogDomain::SYSTEM, "Safe mode active reason=%s", g_state.safe_mode_reason);
    }
    s_goodBootMarked = false;
    LOG_INFO(LogDomain::SYSTEM,
             "Crash loop compatibility state latched=%s boots=%lu bad=%lu reason=%s",
             g_state.crash_loop ? "true" : "false",
             (unsigned long)g_state.crash_window_boots,
             (unsigned long)g_state.crash_window_bad,
             g_state.crash_loop_reason);
  }
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

  if (g_state.ota_state[0] == '\0')
  {
    strncpy(g_state.ota_state, "idle", sizeof(g_state.ota_state));
    g_state.ota_state[sizeof(g_state.ota_state) - 1] = '\0';
  }
  g_state.ota_progress = 0;
  g_state.ota_error[0] = '\0';
  g_state.ota_target_version[0] = '\0';
  g_state.ota_last_ts = 0;
  g_state.ota_last_success_ts = 0;
  g_state.update_available = false;
  g_state.time.valid = false;
  strncpy(g_state.time.status, "time_not_set", sizeof(g_state.time.status));
  g_state.time.status[sizeof(g_state.time.status) - 1] = '\0';
  g_state.time.last_attempt_s = 0;
  g_state.time.last_success_s = 0;
  g_state.time.next_retry_s = 0;
  {
    bool otaForce = false;
    bool otaReboot = true;
    storage_loadOtaOptions(otaForce, otaReboot);
    g_state.ota_force = otaForce;
    g_state.ota_reboot = otaReboot;
    uint32_t otaLastOk = 0;
    if (storage_loadOtaLastSuccess(otaLastOk))
    {
      g_state.ota_last_success_ts = otaLastOk;
    }
  }

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
      .setSenseMode = setSenseMode,
      .setSimulationModeInternal = setSimulationModeInternal,
      .setCalibrationDryValue = setCalibrationDryValue,
      .setCalibrationWetValue = setCalibrationWetValue,
      .reannounce = mqtt_reannounceDiscovery,
      .wipeWifiCredentials = wipeWifiCredentials,
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
      .deviceId = DEVICE_ID,
      .deviceName = DEVICE_NAME,
      .deviceModel = DEVICE_NAME,
      .deviceSw = DEVICE_FW,
      .deviceHw = DEVICE_HW};
  mqtt_begin(mqttCfg, commands_handle);
}

// Contract: called frequently from the Arduino loop; must remain non-blocking.
void appLoop()
{
  const uint32_t now = millis();
  g_state.uptime_seconds = now / 1000u;
  for (LoopWindow &w : g_windows)
  {
    runWindow(w, now);
  }

  if (!s_goodBootMarked &&
      !g_state.safe_mode &&
      g_state.bad_boot_streak > 0u &&
      now >= (uint32_t)CFG_CRASH_GOOD_BOOT_AFTER_MS)
  {
    g_state.bad_boot_streak = 0u;
    storage_saveBadBootStreak(0u);
    g_state.last_good_boot_ts = g_state.ts;
    storage_saveGoodBootTs(g_state.last_good_boot_ts);
    g_state.last_stable_boot = g_state.boot_count;
    g_state.crash_window_boots = 0u;
    g_state.crash_window_bad = 0u;
    g_state.crash_loop = false;
    strncpy(g_state.crash_loop_reason, "stable_runtime", sizeof(g_state.crash_loop_reason));
    g_state.crash_loop_reason[sizeof(g_state.crash_loop_reason) - 1] = '\0';
    LOG_INFO(LogDomain::SYSTEM, "Good boot confirmed: bad_boot_streak reset, last_good_boot_ts=%lu",
             (unsigned long)g_state.last_good_boot_ts);
    s_goodBootMarked = true;
    mqtt_requestStatePublish();
  }
}
