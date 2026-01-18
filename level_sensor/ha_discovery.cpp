#include "ha_discovery.h"

#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "ha_entities.h"

static HaDiscoveryConfig s_cfg{};
static bool s_initialized = false;
static bool s_published = false;

static const char *AVAIL_TOPIC_SUFFIX = "availability";
static const char *STATE_TOPIC_SUFFIX = "state";
static const char *PAYLOAD_AVAILABLE = "online";
static const char *PAYLOAD_NOT_AVAILABLE = "offline";

static const char *buildUniqId(const char *objectId, const char *overrideId)
{
    return overrideId ? overrideId : objectId;
}

static bool publishSensor(const HaSensorSpec &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<640> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["val_tpl"] = s.valueTemplate;
    if (s.deviceClass)
        doc["dev_cla"] = s.deviceClass;
    if (s.unit)
        doc["unit_of_meas"] = s.unit;
    if (s.icon)
        doc["icon"] = s.icon;
    if (s.attrTemplate)
    {
        doc["json_attr_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
        doc["json_attr_tpl"] = s.attrTemplate;
    }

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery sensor too large %s", s.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery sensor %s", topic);
    }
    return ok;
}

static bool publishBinarySensor(const HaBinarySensorSpec &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<640> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["val_tpl"] = s.valueTemplate;
    doc["pl_on"] = true;
    doc["pl_off"] = false;
    if (s.deviceClass)
        doc["dev_cla"] = s.deviceClass;
    if (s.icon)
        doc["icon"] = s.icon;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[768];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery binary sensor too large %s", s.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery binary sensor %s", topic);
    }
    return ok;
}

static bool publishControlButton(const HaButtonSpec &b)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/button/%s_%s/config", s_cfg.deviceId, b.objectId);

    StaticJsonDocument<640> doc;
    doc["name"] = b.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(b.objectId, b.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["pl_press"] = b.payloadJson;
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
        LOG_WARN(LogDomain::MQTT, "HA discovery button too large %s", b.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery button %s", topic);
    }
    return ok;
}

static bool publishNumber(const HaNumberSpec &nSpec)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s_%s/config", s_cfg.deviceId, nSpec.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = nSpec.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(nSpec.objectId, nSpec.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = String("{{ value_json.config.") + nSpec.dataKey + " }}";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["min"] = nSpec.min;
    doc["max"] = nSpec.max;
    doc["step"] = nSpec.step;
    doc["mode"] = "box";
    doc["cmd_tpl"] = String("{\"schema\":1,\"type\":\"") + nSpec.cmdType + "\",\"data\":{\"" + nSpec.dataKey + "\":{{ value }}}}";

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[960];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery number too large %s", nSpec.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery number %s", topic);
    }
    return ok;
}

static bool publishSwitch(const HaSwitchSpec &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = s.valueTemplate;
    doc["pl_on"] = s.payloadOnJson;
    doc["pl_off"] = s.payloadOffJson;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[960];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery switch too large %s", s.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery switch %s", topic);
    }
    return ok;
}

static bool publishSelect(const HaSelectSpec &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["val_tpl"] = s.valueTemplate;
    JsonArray opts = doc.createNestedArray("options");
    for (size_t i = 0; i < s.optionCount; ++i)
    {
        opts.add(s.options[i]);
    }
    doc["cmd_tpl"] = s.cmdTemplateJson;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    JsonObject dev = doc.createNestedObject("dev");
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;

    char buf[960];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery select too large %s", s.objectId);
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery select %s", topic);
    }
    return ok;
}

static bool publishOnlineEntity()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s_online/config", s_cfg.deviceId);

    StaticJsonDocument<512> doc;
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

    char buf[640];
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

    size_t sensorCount = 0;
    const HaSensorSpec *sensors = ha_getSensors(sensorCount);
    for (size_t i = 0; i < sensorCount; ++i)
    {
        anyOk |= publishSensor(sensors[i]);
    }

    size_t binCount = 0;
    const HaBinarySensorSpec *binSensors = ha_getBinarySensors(binCount);
    for (size_t i = 0; i < binCount; ++i)
    {
        anyOk |= publishBinarySensor(binSensors[i]);
    }

    size_t btnCount = 0;
    const HaButtonSpec *buttons = ha_getButtons(btnCount);
    for (size_t i = 0; i < btnCount; ++i)
    {
        anyOk |= publishControlButton(buttons[i]);
    }

    size_t numCount = 0;
    const HaNumberSpec *numbers = ha_getNumbers(numCount);
    for (size_t i = 0; i < numCount; ++i)
    {
        anyOk |= publishNumber(numbers[i]);
    }

    size_t swCount = 0;
    const HaSwitchSpec *switches = ha_getSwitches(swCount);
    for (size_t i = 0; i < swCount; ++i)
    {
        anyOk |= publishSwitch(switches[i]);
    }

    size_t selCount = 0;
    const HaSelectSpec *selects = ha_getSelects(selCount);
    for (size_t i = 0; i < selCount; ++i)
    {
        anyOk |= publishSelect(selects[i]);
    }

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
