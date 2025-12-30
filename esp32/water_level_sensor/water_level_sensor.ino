#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

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

static const char *TOPIC_TANK_RAW = "home/water/tank/raw";
static const char *TOPIC_TANK_PERCENT = "home/water/tank/percent";
static const char *TOPIC_TANK_STATUS = "home/water/tank/status";

static const char *STATUS_ONLINE = "online";
static const char *STATUS_OFFLINE = "offline";
static const char *STATUS_CALIBRATING = "calibrating";
static const char *STATUS_OK = "ok";
static const char *STATUS_NEEDS_CAL = "needs_calibration";

// ===== Sensor / Sampling =====
// Arduino Nano ESP32: A6 is GPIO13 and is touch-capable.
static const int TOUCH_PIN = A6;

static const uint8_t TOUCH_SAMPLES = 16;          // average N reads to reduce jitter
static const uint32_t RAW_PUBLISH_MS = 1000;      // publish the raw value once per second
static const uint32_t PERCENT_PUBLISH_MS = 3000;  // publish the percent value less often
static const float PERCENT_EMA_ALPHA = 0.2f;      // EMA smoothing factor
static const uint16_t CAL_MIN_DIFF = 20;          // minimum delta between dry/wet to accept calibration

// ===== Network timeouts =====
static const uint32_t WIFI_TIMEOUT_MS = 20000;

// ===== Preferences (NVS) =====
static const char *PREF_NAMESPACE = "water_level";
static const char *PREF_KEY_DRY = "dry";
static const char *PREF_KEY_WET = "wet";
static const char *PREF_KEY_INV = "inv";

// ===== MQTT Discovery =====
static const bool ENABLE_DISCOVERY = true;
static const char *DISCOVERY_PREFIX = "homeassistant";
static const char *DEVICE_ID = "water_tank_esp32";
static const char *DEVICE_NAME = "Water Tank Sensor";
static const char *DEVICE_MANUFACTURER = "DIY";
static const char *DEVICE_MODEL = "Nano ESP32";
static const char *DEVICE_SW_VERSION = "1.0";

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

// ----------------- Helpers -----------------

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

static bool hasCalibration()
{
  return calDry > 0 && calWet > 0 && (uint16_t)abs((int)calWet - (int)calDry) >= CAL_MIN_DIFF;
}

static void publishStatus(const char *status, bool retained = true, bool force = false)
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

static void publishCalibrationStatus()
{
  if (hasCalibration())
  {
    publishStatus(STATUS_OK);
  }
  else
  {
    publishStatus(STATUS_NEEDS_CAL);
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

  if (!hasCalibration())
  {
    Serial.println("[CAL] Calibration missing or too close. Use 'dry' and 'wet' commands.");
  }
}

static void clearCalibration()
{
  prefs.clear();
  calDry = 0;
  calWet = 0;
  calInverted = false;
  percentEma = NAN;
  Serial.println("[CAL] Cleared calibration.");
  publishCalibrationStatus();
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
  if (!hasCalibration() || calDry == calWet)
  {
    return NAN;
  }

  const float inputStart = calInverted ? (float)calWet : (float)calDry;
  const float inputEnd = calInverted ? (float)calDry : (float)calWet;
  const float percent = ((float)raw - inputStart) * 100.0f / (inputEnd - inputStart);

  return constrain(percent, 0.0f, 100.0f);
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
    publishStatus(STATUS_CALIBRATING);
    const uint16_t sample = readTouchAverage(TOUCH_SAMPLES);
    calDry = sample;
    prefs.putUShort(PREF_KEY_DRY, calDry);
    percentEma = NAN;
    Serial.print("[CAL] Captured dry=");
    Serial.println(calDry);
    publishCalibrationStatus();
  }
  else if (cmd == "wet")
  {
    publishStatus(STATUS_CALIBRATING);
    const uint16_t sample = readTouchAverage(TOUCH_SAMPLES);
    calWet = sample;
    prefs.putUShort(PREF_KEY_WET, calWet);
    percentEma = NAN;
    Serial.print("[CAL] Captured wet=");
    Serial.println(calWet);
    publishCalibrationStatus();
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
    Serial.println(hasCalibration() ? "yes" : "no");
  }
  else if (cmd == "clear")
  {
    clearCalibration();
  }
  else if (cmd == "invert")
  {
    calInverted = !calInverted;
    prefs.putBool(PREF_KEY_INV, calInverted);
    percentEma = NAN;
    Serial.print("[CAL] Inverted set to ");
    Serial.println(calInverted ? "true" : "false");
    publishCalibrationStatus();
  }
  else if (cmd == "help" || cmd.length() > 0)
  {
    printHelpMenu();
  }
}

static void publishDiscovery()
{
  if (!ENABLE_DISCOVERY || !mqtt.connected() || discoverySent)
    return;

  String deviceJson = String("{\"name\":\"") + DEVICE_NAME + "\",\"identifiers\":[\"" + DEVICE_ID +
                       "\"],\"manufacturer\":\"" + DEVICE_MANUFACTURER + "\",\"model\":\"" + DEVICE_MODEL +
                       "\",\"sw_version\":\"" + DEVICE_SW_VERSION + "\"}";

  const String availability = String("\"availability_topic\":\"") + TOPIC_TANK_STATUS + "\"";

  String rawConfig = String("{\"name\":\"Water Tank Raw\",\"state_topic\":\"") + TOPIC_TANK_RAW +
                     "\",\"unique_id\":\"water_tank_raw\"," + availability + ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_raw/config").c_str(), rawConfig.c_str(), true);

  String percentConfig = String("{\"name\":\"Water Tank Level\",\"state_topic\":\"") + TOPIC_TANK_PERCENT +
                         "\",\"unique_id\":\"water_tank_percent\",\"unit_of_measurement\":\"%\"," + availability +
                         ",\"device\":" + deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_percent/config").c_str(), percentConfig.c_str(), true);

  String statusConfig = String("{\"name\":\"Water Tank Status\",\"state_topic\":\"") + TOPIC_TANK_STATUS +
                        "\",\"unique_id\":\"water_tank_status\"," + availability + ",\"entity_category\":\"diagnostic\",\"device\":" +
                        deviceJson + "}";
  mqtt.publish((String(DISCOVERY_PREFIX) + "/sensor/water_tank_status/config").c_str(), statusConfig.c_str(), true);

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
  mqtt.setBufferSize(512);

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
      publishCalibrationStatus();
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
  publishCalibrationStatus();
  printHelpMenu();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  mqtt.setBufferSize(512);

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
    const uint16_t raw = readTouchAverage(TOUCH_SAMPLES);
    lastRawValue = raw;

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
      publishStatus(STATUS_OK);

      if (mqtt.connected())
      {
        mqtt.publish(TOPIC_TANK_PERCENT, String(percentEma, 1).c_str());
      }
    }
    else
    {
      publishStatus(STATUS_NEEDS_CAL);
    }

    Serial.print("[PERCENT] ");
    if (isnan(rawPercent))
    {
      Serial.println("N/A");
    }
    else
    {
      Serial.println(percentEma, 1);
    }
  }
}
