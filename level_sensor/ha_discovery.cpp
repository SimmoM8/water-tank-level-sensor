#include "ha_discovery.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "telemetry_registry.h"

static HaDiscoveryConfig s_cfg{};
static bool s_initialized = false;
static bool s_published = false;

static const char *AVAIL_TOPIC_SUFFIX = "availability";
static const char *STATE_TOPIC_SUFFIX = "state";
static const char *DEVICE_INFO_TOPIC_SUFFIX = "device_info";
static const char *OTA_PROGRESS_TOPIC_SUFFIX = "ota/progress";
static const char *OTA_STATUS_TOPIC_SUFFIX = "ota/status";
static const char *PAYLOAD_AVAILABLE = "online";
static const char *PAYLOAD_NOT_AVAILABLE = "offline";
static const char *DEVICE_MANUFACTURER = "Dads Smart Home";
static const char *ORIGIN_NAME = "dads-smart-home-water-tank";

static const char *buildUniqId(const char *objectId, const char *overrideId)
{
    return overrideId ? overrideId : objectId;
}

static void addDeviceShort(JsonObject dev)
{
    dev["name"] = s_cfg.deviceName;
    dev["ids"] = s_cfg.deviceId;
    dev["mdl"] = s_cfg.deviceModel;
    dev["sw"] = s_cfg.deviceSw;
    dev["sw_version"] = s_cfg.deviceSw;
    if (s_cfg.deviceHw && s_cfg.deviceHw[0] != '\0')
    {
        dev["hw"] = s_cfg.deviceHw;
        dev["hw_version"] = s_cfg.deviceHw;
    }
    dev["mf"] = DEVICE_MANUFACTURER;
}

static void addDeviceLong(JsonObject dev)
{
    dev["name"] = s_cfg.deviceName;
    dev["identifiers"] = s_cfg.deviceId;
    dev["model"] = s_cfg.deviceModel;
    dev["sw_version"] = s_cfg.deviceSw;
    if (s_cfg.deviceHw && s_cfg.deviceHw[0] != '\0')
    {
        dev["hw_version"] = s_cfg.deviceHw;
    }
    dev["manufacturer"] = DEVICE_MANUFACTURER;
}

template <typename TDoc>
static void addOriginBlock(TDoc &doc)
{
    JsonObject origin = doc.createNestedObject("origin");
    origin["name"] = ORIGIN_NAME;
    origin["sw_version"] = s_cfg.deviceSw;
    if (s_cfg.deviceHw && s_cfg.deviceHw[0] != '\0')
    {
        origin["hw_version"] = s_cfg.deviceHw;
    }
}

static bool publishSensor(const TelemetryFieldDef &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    char tpl[96];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", s.jsonPath);
    doc["val_tpl"] = tpl;
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
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

    char buf[896];
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

static bool publishBinarySensor(const TelemetryFieldDef &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    char tpl[96];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", s.jsonPath);
    doc["val_tpl"] = tpl;
    doc["pl_on"] = true;
    doc["pl_off"] = false;
    if (s.deviceClass)
        doc["dev_cla"] = s.deviceClass;
    if (s.icon)
        doc["icon"] = s.icon;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

    char buf[896];
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

static bool publishControlButton(const ControlDef &b)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/button/%s_%s/config", s_cfg.deviceId, b.objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = b.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(b.objectId, b.uniqIdOverride);
    // Use full discovery keys for MQTT button to ensure HA publishes the JSON payload,
    // not the default "PRESS".
    doc["command_topic"] = String(s_cfg.baseTopic) + "/cmd";
    doc["payload_press"] = b.payloadJson;
    if (b.cmdType && strcmp(b.cmdType, "ota_pull") == 0)
    {
        doc["entity_category"] = "config";
    }

    doc["availability_topic"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["payload_available"] = PAYLOAD_AVAILABLE;
    doc["payload_not_available"] = PAYLOAD_NOT_AVAILABLE;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

    char buf[960];
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

static bool publishNumber(const ControlDef &nSpec)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s_%s/config", s_cfg.deviceId, nSpec.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = nSpec.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(nSpec.objectId, nSpec.uniqIdOverride);

    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;

    char tpl[96];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", nSpec.statePath);
    doc["val_tpl"] = tpl;

    doc["min"] = nSpec.min;
    doc["max"] = nSpec.max;
    doc["step"] = nSpec.step;
    doc["mode"] = "box";

    char cmdTpl[160];
    snprintf(cmdTpl, sizeof(cmdTpl), "{\"schema\":1,\"type\":\"%s\",\"data\":{\"%s\":{{ value }}}}", nSpec.cmdType, nSpec.dataKey);
    doc["cmd_tpl"] = cmdTpl;

    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;

    if (nSpec.unit)
        doc["unit_of_meas"] = nSpec.unit;
    if (nSpec.icon)
        doc["icon"] = nSpec.icon;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

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

static bool publishSwitch(const ControlDef &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/switch/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    char tpl[96];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", s.statePath);
    doc["val_tpl"] = tpl;
    doc["pl_on"] = s.payloadOnJson;
    doc["pl_off"] = s.payloadOffJson;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

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

static bool publishSelect(const ControlDef &s)
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s_%s/config", s_cfg.deviceId, s.objectId);

    StaticJsonDocument<896> doc;
    doc["name"] = s.name;
    doc["uniq_id"] = String(s_cfg.deviceId) + "_" + buildUniqId(s.objectId, s.uniqIdOverride);
    doc["cmd_t"] = String(s_cfg.baseTopic) + "/cmd";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    char tpl[128];
    snprintf(tpl, sizeof(tpl), "{{ value_json.%s | string }}", s.statePath);
    doc["val_tpl"] = tpl;
    JsonArray opts = doc.createNestedArray("options");
    for (size_t i = 0; i < s.optionCount; ++i)
    {
        opts.add(s.options[i]);
    }
    if (s.cmdTemplateJson)
    {
        doc["cmd_tpl"] = s.cmdTemplateJson;
    }
    else
    {
        char cmdTpl[192];
        snprintf(cmdTpl, sizeof(cmdTpl), "{\"schema\":1,\"type\":\"%s\",\"data\":{\"%s\":\"{{ value }}\"}}", s.cmdType, s.dataKey);
        doc["cmd_tpl"] = cmdTpl;
    }
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

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

    StaticJsonDocument<640> doc;
    doc["name"] = "Device Online";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_online";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_on"] = PAYLOAD_AVAILABLE;
    doc["pl_off"] = PAYLOAD_NOT_AVAILABLE;
    doc["dev_cla"] = "connectivity";
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

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

static bool publishUpdateEntity()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/update/%s_firmware/config", s_cfg.deviceId);

    StaticJsonDocument<1024> doc;
    doc["name"] = "Firmware";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_firmware";
    doc["state_topic"] = String(s_cfg.baseTopic) + "/" + STATE_TOPIC_SUFFIX;
    doc["installed_version_template"] = "{{ value_json.installed_version | default('', true) }}";
    doc["latest_version_template"] = "{{ value_json.latest_version | default('', true) }}";
    doc["command_topic"] = String(s_cfg.baseTopic) + "/cmd";
    doc["payload_install"] = "{\"schema\":1,\"type\":\"ota_pull\",\"data\":{}}";

    doc["availability_topic"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["payload_available"] = PAYLOAD_AVAILABLE;
    doc["payload_not_available"] = PAYLOAD_NOT_AVAILABLE;

    doc["device_class"] = "firmware";
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("device");
    addDeviceLong(dev);

    char buf[896];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery update too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery update %s", topic);
    }
    return ok;
}

static bool publishOtaProgressEntity()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_ota_progress/config", s_cfg.deviceId);

    StaticJsonDocument<896> doc;
    doc["name"] = "OTA Progress";
    doc["uniq_id"] = String(s_cfg.deviceId) + "_ota_progress";
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + OTA_PROGRESS_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["unit_of_meas"] = "%";
    doc["icon"] = "mdi:progress-download";
    doc["val_tpl"] = "{% set v = value | int(0) %}{% if v == 255 %}{{ none }}{% else %}{{ v }}{% endif %}";
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

    char buf[960];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery OTA progress too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery OTA progress %s", topic);
    }
    return ok;
}

static bool publishOtaStatusEntity()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "homeassistant/sensor/%s_ota_status/config", s_cfg.deviceId);

    StaticJsonDocument<896> doc;
    doc["name"] = "OTA Status";
    doc["uniq_id"] = String("water_tank_ota_status_") + s_cfg.deviceId;
    doc["stat_t"] = String(s_cfg.baseTopic) + "/" + OTA_STATUS_TOPIC_SUFFIX;
    doc["avty_t"] = String(s_cfg.baseTopic) + "/" + AVAIL_TOPIC_SUFFIX;
    doc["pl_avail"] = PAYLOAD_AVAILABLE;
    doc["pl_not_avail"] = PAYLOAD_NOT_AVAILABLE;
    doc["entity_category"] = "diagnostic";
    doc["icon"] = "mdi:update";
    addOriginBlock(doc);

    JsonObject dev = doc.createNestedObject("dev");
    addDeviceShort(dev);

    char buf[960];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA discovery OTA status too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed HA discovery OTA status %s", topic);
    }
    return ok;
}

static bool publishDeviceInfo()
{
    char topic[192];
    snprintf(topic, sizeof(topic), "%s/%s", s_cfg.baseTopic, DEVICE_INFO_TOPIC_SUFFIX);

    StaticJsonDocument<512> doc;
    doc["device_id"] = s_cfg.deviceId;
    doc["device_name"] = s_cfg.deviceName;
    doc["device_model"] = s_cfg.deviceModel;
    doc["manufacturer"] = DEVICE_MANUFACTURER;
    doc["sw_version"] = s_cfg.deviceSw;
    if (s_cfg.deviceHw && s_cfg.deviceHw[0] != '\0')
    {
        doc["hw_version"] = s_cfg.deviceHw;
    }
    addOriginBlock(doc);

    char buf[640];
    const size_t n = serializeJson(doc, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf))
    {
        LOG_WARN(LogDomain::MQTT, "HA device_info payload too large");
        return false;
    }

    const bool ok = s_cfg.publish(topic, buf, true);
    if (!ok)
    {
        LOG_WARN(LogDomain::MQTT, "Failed device_info publish %s", topic);
    }
    return ok;
}

static bool publishOtaExtras()
{
    static const TelemetryFieldDef kOtaLastStatus{
        HaComponent::Sensor, "ota_last_status", "OTA Last Status", "ota.result.status",
        nullptr, nullptr, "mdi:update", nullptr, nullptr, nullptr};
    static const TelemetryFieldDef kOtaLastMessage{
        HaComponent::Sensor, "ota_last_message", "OTA Last Message", "ota.result.message",
        nullptr, nullptr, "mdi:message-alert-outline", nullptr, nullptr, nullptr};
    static const TelemetryFieldDef kUpdateAvailable{
        HaComponent::BinarySensor, "update_available", "Update Available", "update_available",
        "update", nullptr, "mdi:update", nullptr, nullptr, nullptr};

    bool ok = false;
    ok |= publishOtaProgressEntity();
    ok |= publishOtaStatusEntity();
    ok |= publishSensor(kOtaLastStatus);
    ok |= publishSensor(kOtaLastMessage);
    ok |= publishBinarySensor(kUpdateAvailable);
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

    anyOk |= publishDeviceInfo();
    anyOk |= publishOnlineEntity();
    anyOk |= publishUpdateEntity();
    anyOk |= publishOtaExtras();

    size_t tCount = 0;
    const TelemetryFieldDef *fields = telemetry_registry_fields(tCount);
    for (size_t i = 0; i < tCount; ++i)
    {
        if (fields[i].component == HaComponent::Sensor)
        {
            if (fields[i].objectId && strcmp(fields[i].objectId, "ota_progress") == 0)
            {
                continue;
            }
            anyOk |= publishSensor(fields[i]);
        }
        else if (fields[i].component == HaComponent::BinarySensor)
        {
            anyOk |= publishBinarySensor(fields[i]);
        }
    }

    size_t cCount = 0;
    const ControlDef *controls = telemetry_registry_controls(cCount);
    for (size_t i = 0; i < cCount; ++i)
    {
        switch (controls[i].component)
        {
        case HaComponent::Button:
            anyOk |= publishControlButton(controls[i]);
            break;
        case HaComponent::Number:
            anyOk |= publishNumber(controls[i]);
            break;
        case HaComponent::Switch:
            anyOk |= publishSwitch(controls[i]);
            break;
        case HaComponent::Select:
            anyOk |= publishSelect(controls[i]);
            break;
        default:
            break;
        }
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
