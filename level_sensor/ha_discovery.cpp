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
    {"binary_sensor", "raw_valid", "Raw Valid", "{{ value_json.probe.raw_valid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "percent_valid", "Percent Valid", "{{ value_json.level.percent_valid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "liters_valid", "Liters Valid", "{{ value_json.level.liters_valid }}", nullptr, nullptr, nullptr, true},
    {"binary_sensor", "centimeters_valid", "Centimeters Valid", "{{ value_json.level.centimeters_valid }}", nullptr, nullptr, nullptr, true},
};

static bool publishEntity(const EntityDef &e)
{
    if (!s_cfg.publish)
        return false;

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
        doc["icon"] = e.icon;

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
        return false;
    }

    if (s_cfg.publish(topic, buf, true))
    {
        LOG_INFO(LogDomain::MQTT, "Published HA discovery %s", topic);
        return true;
    }

    LOG_WARN(LogDomain::MQTT, "Failed HA discovery %s", topic);
    return false;
}

static bool publishControlButton(const char *objectId, const char *name, const char *payload)
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
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery button %s", topic);
    }
    return ok;
}

static bool publishOnlineEntity()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s_online/config", s_cfg.deviceId);

    StaticJsonDocument<320> doc;
    doc["name"] = "Device Online";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_online";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_on"] = PAYLOAD_AVAILABLE;
    doc["pl_off"] = PAYLOAD_NOT_AVAILABLE;
    doc["dev_cla"] = "connectivity";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[448];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery online entity too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery online entity");
    }
    return ok;
}

static bool publishNumber(const char *objectId, const char *name, const char *dataKey, float minV, float maxV, float step)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s_%s/config", s_cfg.deviceId, objectId);

    StaticJsonDocument<640> doc;
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
    doc["cmd_tpl"] = String("{\"schema\":1,\"type\":\"set_config\",\"data\":{\"") + dataKey + "\":{{ value }}}}";

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery number too large %s", objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery number %s", topic);
    }
    return ok;
}

static bool publishSwitch()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s_simulation_enabled/config", s_cfg.deviceId);

    StaticJsonDocument<640> doc;
    doc["name"] = "Simulation Enabled";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_simulation_enabled";
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.config.simulation_enabled }}";
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

    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery switch too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery switch %s", topic);
    }
    return ok;
}

static bool publishSelect()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s_simulation_mode/config", s_cfg.deviceId);

    StaticJsonDocument<640> doc;
    doc["name"] = "Simulation Mode";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_simulation_mode";
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.config.simulation_mode }}";
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

    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery select too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery select %s", topic);
    }
    return ok;
}

static bool publishButtonControls()
{
    bool ok = true;
    ok &= publishControlButton("calibrate_dry", "Calibrate Dry", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"dry\"}}");
    ok &= publishControlButton("calibrate_wet", "Calibrate Wet", "{\"schema\":1,\"type\":\"calibrate\",\"data\":{\"point\":\"wet\"}}");
    ok &= publishControlButton("clear_calibration", "Clear Calibration", "{\"schema\":1,\"type\":\"clear_calibration\"}");
    ok &= publishControlButton("reannounce", "Re-announce Device", "{\"schema\":1,\"type\":\"reannounce\",\"request_id\":\"{{ timestamp }}\"}");
    return ok;
}

static bool publishNumberControls()
{
    bool ok = true;
    ok &= publishNumber("tank_volume_l", "Tank Volume (L)", "tank_volume_l", 0.0f, 10000.0f, 1.0f);
    ok &= publishNumber("rod_length_cm", "Rod Length (cm)", "rod_length_cm", 0.0f, 1000.0f, 1.0f);
    return ok;
}

static bool publishDiagnosticLastCmd()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_last_cmd/config", s_cfg.deviceId);

    StaticJsonDocument<384> doc;
    doc["name"] = "Last Command";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_last_cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = "{{ value_json.last_cmd.type }}";
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
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery last_cmd %s", topic);
    }
    return ok;
}

void ha_discovery_begin(const HaDiscoveryConfig &cfg)
{
    s_cfg = cfg;
    s_initialized = cfg.publish != nullptr && cfg.baseTopic != nullptr && cfg.deviceId != nullptr;
    s_published = false;

    LOG_INFO(LogDomain::MQTT, "HA discovery begin: initialized=%s baseTopic=%s deviceId=%s",
             s_initialized ? "true" : "false",
             (s_cfg.baseTopic ? s_cfg.baseTopic : "(null)"),
             (s_cfg.deviceId ? s_cfg.deviceId : "(null)"));
}

void ha_discovery_publishAll()
{
    if (!s_initialized)
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery publishAll skipped: not initialized");
        return;
    }
    if (s_published)
    {
        static bool s_loggedAlreadyPublished = false;
        if (!s_loggedAlreadyPublished)
        {
            LOG_INFO(LogDomain::MQTT, "HA discovery publishAll skipped: already published");
            s_loggedAlreadyPublished = true;
        }
        return;
    }

    bool anyOk = false;

    anyOk |= publishOnlineEntity();

    for (size_t i = 0; i < sizeof(ENTITIES) / sizeof(ENTITIES[0]); ++i)
    {
        anyOk |= publishEntity(ENTITIES[i]);
    }

    anyOk |= publishButtonControls();
    anyOk |= publishNumberControls();
    anyOk |= publishSwitch();
    anyOk |= publishSelect();
    anyOk |= publishDiagnosticLastCmd();

    if (anyOk)
    {
        s_published = true;
        LOG_INFO(LogDomain::MQTT, "HA discovery publishAll complete");
    }
    else
    {
        // keep false so callers can retry once MQTT is actually connected
        LOG_WARN(LogDomain::MQTT, "HA discovery publishAll failed: no config published (will retry)");
    }
}
