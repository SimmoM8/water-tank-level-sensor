#include "ha_discovery.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "telemetry_registry.h"

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef CFG_LOG_DEV
#define CFG_LOG_DEV 0
#endif
#ifndef CFG_OTA_DEV_LOGS
#define CFG_OTA_DEV_LOGS CFG_LOG_DEV
#endif

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
static const uint32_t HA_WARN_INTERVAL_MS = 60000;

static bool ha_devLogsEnabled()
{
    return (CFG_LOG_DEV != 0) || (CFG_OTA_DEV_LOGS != 0);
}

static void logHaPayloadTooLarge(const char *entity)
{
    if (ha_devLogsEnabled())
    {
        logger_logEvery("ha_disc_payload_too_large", HA_WARN_INTERVAL_MS, LogLevel::WARN, LogDomain::MQTT,
                        "HA discovery payload too large entity=%s", entity ? entity : "(unknown)");
    }
    else
    {
        logger_logEvery("ha_disc_payload_too_large", HA_WARN_INTERVAL_MS, LogLevel::WARN, LogDomain::MQTT,
                        "MQTT: Home Assistant discovery payload too large (enable dev logs)");
    }
}

static void logHaPublishFailed(const char *entity, const char *topic)
{
    if (ha_devLogsEnabled())
    {
        logger_logEvery("ha_disc_publish_failed", HA_WARN_INTERVAL_MS, LogLevel::WARN, LogDomain::MQTT,
                        "HA discovery publish failed entity=%s topic=%s",
                        entity ? entity : "(unknown)",
                        topic ? topic : "(null)");
    }
    else
    {
        logger_logEvery("ha_disc_publish_failed", HA_WARN_INTERVAL_MS, LogLevel::WARN, LogDomain::MQTT,
                        "MQTT: Home Assistant discovery failed (will retry)");
    }
}

static bool publishDiscoveryPayload(const char *entity, const char *topic, const char *payload, size_t payloadLen)
{
    if (!payload || payloadLen == 0)
    {
        logHaPayloadTooLarge(entity);
        return false;
    }
    const bool ok = s_cfg.publish(topic, payload, true);
    if (!ok)
    {
        logHaPublishFailed(entity, topic);
        return false;
    }
    if (ha_devLogsEnabled())
    {
        LOG_DEBUG(LogDomain::MQTT, "HA publish entity=%s topic=%s bytes=%u retained=true",
                  entity ? entity : "(unknown)",
                  topic ? topic : "(null)",
                  (unsigned)payloadLen);
    }
    return true;
}

static const char *buildUniqId(const char *objectId, const char *overrideId)
{
    return overrideId ? overrideId : objectId;
}

static const char *stateClassForSensor(const TelemetryFieldDef &s)
{
    // Keep schema stable: only telemetry that is a continuously sampled scalar should be "measurement".
    if (s.objectId && strcmp(s.objectId, "uptime_seconds") == 0)
    {
        return "measurement";
    }
    return nullptr;
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
    if (const char *stateClass = stateClassForSensor(s))
        doc["stat_cla"] = stateClass;
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
        logHaPayloadTooLarge(s.objectId);
        return false;
    }
    return publishDiscoveryPayload(s.objectId, topic, buf, n);
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
        logHaPayloadTooLarge(s.objectId);
        return false;
    }
    return publishDiscoveryPayload(s.objectId, topic, buf, n);
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
        logHaPayloadTooLarge(b.objectId);
        return false;
    }
    return publishDiscoveryPayload(b.objectId, topic, buf, n);
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
        logHaPayloadTooLarge(nSpec.objectId);
        return false;
    }
    return publishDiscoveryPayload(nSpec.objectId, topic, buf, n);
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
        logHaPayloadTooLarge(s.objectId);
        return false;
    }
    return publishDiscoveryPayload(s.objectId, topic, buf, n);
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
        logHaPayloadTooLarge(s.objectId);
        return false;
    }
    return publishDiscoveryPayload(s.objectId, topic, buf, n);
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
        logHaPayloadTooLarge("online");
        return false;
    }
    return publishDiscoveryPayload("online", topic, buf, n);
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
        logHaPayloadTooLarge("firmware_update");
        return false;
    }
    return publishDiscoveryPayload("firmware_update", topic, buf, n);
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
        logHaPayloadTooLarge("ota_progress");
        return false;
    }
    return publishDiscoveryPayload("ota_progress", topic, buf, n);
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
        logHaPayloadTooLarge("ota_status");
        return false;
    }
    return publishDiscoveryPayload("ota_status", topic, buf, n);
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
        logHaPayloadTooLarge("device_info");
        return false;
    }
    return publishDiscoveryPayload("device_info", topic, buf, n);
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
    if (ha_devLogsEnabled())
    {
        LOG_DEBUG(LogDomain::MQTT, "HA discovery begin initialized=%s baseTopic=%s deviceId=%s",
                  s_initialized ? "true" : "false",
                  (s_cfg.baseTopic ? s_cfg.baseTopic : "(null)"),
                  (s_cfg.deviceId ? s_cfg.deviceId : "(null)"));
    }
}

HaDiscoveryResult ha_discovery_publishAll()
{
    if (!s_initialized)
    {
        if (ha_devLogsEnabled())
        {
            LOG_DEBUG(LogDomain::MQTT, "HA discovery skipped: not initialized");
        }
        return HaDiscoveryResult::NOT_INITIALIZED;
    }
    if (s_published)
    {
        if (ha_devLogsEnabled())
        {
            LOG_DEBUG(LogDomain::MQTT, "HA discovery skipped: already published");
        }
        return HaDiscoveryResult::ALREADY_PUBLISHED;
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
        return HaDiscoveryResult::PUBLISHED;
    }
    // Keep false so callers can retry once MQTT is actually connected.
    return HaDiscoveryResult::FAILED;
}
