#include "ha_discovery.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"

static HaDiscoveryConfig s_cfg{};
static bool s_initialized = false;
static bool s_published = false;

static const char *AVAIL_TOPIC_SUFFIX = "availability";
static const char *STATE_TOPIC_SUFFIX = "state";
static const char *PAYLOAD_AVAILABLE = "online";
static const char *PAYLOAD_NOT_AVAILABLE = "offline";

struct EntityDef
{
    const char *component;
    const char *objectId;
    const char *name;
    const char *valueTemplate;
    const char *deviceClass;
    const char *unit;
    const char *icon;
    bool isBinary;
};

static const EntityDef ENTITIES[] = {
    {"sensor", "raw", "Raw", "{{ value_json.probe.raw }}", nullptr, nullptr, "mdi:water", false},
    {"sensor", "percent", "Percent", "{{ value_json.level.percent }}", "humidity", "%", nullptr, false},
    {"sensor", "liters", "Liters", "{{ value_json.level.liters }}", nullptr, "L", "mdi:water", false},
    {"sensor", "centimeters", "Centimeters", "{{ value_json.level.centimeters }}", nullptr, "cm", "mdi:ruler", false},
    {"sensor", "calibration_state", "Calibration State", "{{ value_json.calibration.state }}", nullptr, nullptr, "mdi:tune", false},
    {"sensor", "cal_dry", "Calibration Dry", "{{ value_json.calibration.dry }}", nullptr, nullptr, nullptr, false},
    {"sensor", "cal_wet", "Calibration Wet", "{{ value_json.calibration.wet }}", nullptr, nullptr, nullptr, false},
    {"sensor", "quality", "Probe Quality", "{{ value_json.probe.quality }}", nullptr, nullptr, "mdi:diagnostics", false},
    {"sensor", "wifi_rssi", "WiFi RSSI", "{{ value_json.wifi.rssi }}", "signal_strength", "dBm", "mdi:wifi", false},
    {"sensor", "ip", "IP Address", "{{ value_json.wifi.ip }}", nullptr, nullptr, "mdi:ip-network", false},
    {"binary_sensor", "probe_connected", "Probe Connected", "{{ value_json.probe.connected }}", "connectivity", nullptr, nullptr, true},
    {"binary_sensor", "raw_valid", "Raw Valid", "{{ value_json.probe.rawValid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "percent_valid", "Percent Valid", "{{ value_json.level.percentValid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "liters_valid", "Liters Valid", "{{ value_json.level.litersValid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "centimeters_valid", "Centimeters Valid", "{{ value_json.level.centimetersValid }}", nullptr, nullptr, nullptr, true},
};

static void publishEntity(const EntityDef &e)
{
    if (!s_cfg.publish)
        return;

    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s_%s/config", e.component, s_cfg.deviceId, e.objectId);

    StaticJsonDocument<384> doc;
    doc["name"] = e.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + e.objectId;
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["val_tpl"] = e.valueTemplate;
    if (e.deviceClass)
        doc["dev_cla"] = e.deviceClass;
    if (e.unit)
        doc["unit_of_meas"] = e.unit;
    if (e.icon)
        doc["ic"] = e.icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery payload too large for %s", e.objectId);
        return;
    }

    if (s_cfg.publish(topic, buf, true))
    {
        LOG_INFO(LogDomain::MQTT, "Published HA discovery %s", topic);
    }
    else
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery %s", topic);
    }
}

static void publishControlButton(const char *objectId, const char *name, const char *payload)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/button/%s_%s/config", s_cfg.deviceId, objectId);

    StaticJsonDocument<384> doc;
    doc["name"] = name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + objectId;
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["pl_press"] = payload;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery button too large %s", objectId);
        return;
    }

    s_cfg.publish(topic, buf, true);
}

static void publishNumber(const char *objectId, const char *name, const char *dataKey, float minV, float maxV, float step)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s_%s/config", s_cfg.deviceId, objectId);

    StaticJsonDocument<384> doc;
    doc["name"] = name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + objectId;
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = String("{{ value_json.config.") + dataKey + " }}";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["min"] = minV;
    doc["max"] = maxV;
    doc["step"] = step;
    doc["mode"] = "box";
    doc["cmd_tpl"] = String("{\"schema\":1,\"type\":\"set_config\",\"data\":{\"") + dataKey + "\":{{ value }}}";

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery number too large %s", objectId);
        return;
    }

    s_cfg.publish(topic, buf, true);
}

static void publishSwitch()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s_simulation_enabled/config", s_cfg.deviceId);

    StaticJsonDocument<384> doc;
    doc["name"] = "Simulation Enabled";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_simulation_enabled";
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.config.simulationEnabled }}";
    doc["pl_on"] = "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":true}}";
    doc["pl_off"] = "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"enabled\":false}}";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery switch too large");
        return;
    }

    s_cfg.publish(topic, buf, true);
}

static void publishSelect()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s_simulation_mode/config", s_cfg.deviceId);

    StaticJsonDocument<384> doc;
    doc["name"] = "Simulation Mode";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_simulation_mode";
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.config.simulationMode }}";
    JsonArray opts = doc.createNestedArray("options");
    for (int i = 0; i <= 5; ++i)
    {
        opts.add(i);
    }
    doc["cmd_tpl"] = "{\"schema\":1,\"type\":\"set_simulation\",\"data\":{\"mode\":{{ value }}}}";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery select too large");
        return;
    }

    s_cfg.publish(topic, buf, true);
}

static void publishButtonControls()
{
    publishControlButton("calibrate_dry", "Calibrate Dry", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"dry\"}}{}");
    publishControlButton("calibrate_wet", "Calibrate Wet", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"wet\"}}{}");
    publishControlButton("clear_calibration", "Clear Calibration", "{\"schema\":1,\"type\":\"clear_calibration\"}");
}

static void publishNumberControls()
{
    publishNumber("tank_volume_l", "Tank Volume (L)", "tankVolumeLiters", 0.0f, 10000.0f, 1.0f);
    publishNumber("rod_length_cm", "Rod Length (cm)", "rodLengthCm", 0.0f, 1000.0f, 1.0f);
}

static void publishDiagnosticLastCmd()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_last_cmd/config", s_cfg.deviceId);

    StaticJsonDocument<384> doc;
    doc["name"] = "Last Command";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_last_cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.last_cmd.type }}";
    JsonObject attrs = doc.createNestedObject("json_attr_t");
    doc["json_attr_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["json_attr_tpl"] = "{{ value_json.last_cmd | tojson }}";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[512];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery last_cmd too large");
        return;
    }

    s_cfg.publish(topic, buf, true);
}

void ha_discovery_begin(const HaDiscoveryConfig &cfg)
{
    s_cfg = cfg;
    s_initialized = cfg.publish != nullptr && cfg.baseTopic != nullptr && cfg.deviceId != nullptr;
    s_published = false;
}

void ha_discovery_publishAll()
{
    if (!s_initialized || s_published)
        return;

    for (size_t i = 0; i < sizeof(ENTITIES) / sizeof(ENTITIES[0]); ++i)
    {
        publishEntity(ENTITIES[i]);
    }

    publishButtonControls();
    publishNumberControls();
    publishSwitch();
    publishSelect();
    publishDiagnosticLastCmd();

    s_published = true;
}
