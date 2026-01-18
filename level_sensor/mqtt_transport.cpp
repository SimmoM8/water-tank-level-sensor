#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>
#include <cstring>

#include "mqtt_transport.h"
#include "ha_discovery.h"
#include "state_json.h"
#include "commands.h"
#include "logger.h"
#include "domain_strings.h"

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static MqttConfig s_cfg{};
static CommandHandlerFn s_cmdHandler = nullptr;
static bool s_haDiscoveryBegun = false;

bool mqtt_publishRaw(const char *topic, const char *payload, bool retained)
{
    if (!mqtt.connected())
        return false;
    return mqtt.publish(topic, payload, retained);
}
static bool s_initialized = false;
static bool s_statePublishRequested = true;

struct Topics
{
    char state[96];
    char cmd[96];
    char ack[96];
    char avail[96];
};
static Topics s_topics{};

static uint32_t s_lastStatePublishMs = 0;
static const uint32_t STATE_MIN_INTERVAL_MS = 1000; // no more than once per second
static const uint32_t STATE_HEARTBEAT_MS = 30000;   // periodic retained snapshot
static uint32_t s_lastAttemptMs = 0;
static const uint32_t RETRY_INTERVAL_MS = 5000;

static const char *AVAIL_ONLINE = "online";
static const char *AVAIL_OFFLINE = "offline";

static void buildTopic(char *out, size_t outSize, const char *suffix)
{
    if (outSize == 0 || s_cfg.baseTopic == nullptr)
        return;
    snprintf(out, outSize, "%s/%s", s_cfg.baseTopic, suffix);
}

static void buildTopics()
{
    buildTopic(s_topics.state, sizeof(s_topics.state), "state");
    buildTopic(s_topics.cmd, sizeof(s_topics.cmd), "cmd");
    buildTopic(s_topics.ack, sizeof(s_topics.ack), "ack");
    buildTopic(s_topics.avail, sizeof(s_topics.avail), "availability");
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    if (!s_cmdHandler)
        return;
    if (strcmp(topic, s_topics.cmd) != 0)
        return;

    s_cmdHandler(payload, length);
}

static void mqtt_subscribe()
{
    mqtt.subscribe(s_topics.cmd);
}

static bool mqtt_ensureConnected()
{
    if (!s_initialized)
    {
        return false;
    }

    const uint32_t now = millis();

    if (!mqtt.connected())
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return false;
        }

        if (!s_haDiscoveryBegun)
        {
            HaDiscoveryConfig haCfg{
                .baseTopic = s_cfg.baseTopic,
                .deviceId = s_cfg.deviceId,
                .deviceName = s_cfg.deviceName,
                .deviceModel = s_cfg.deviceModel,
                .deviceSw = s_cfg.deviceSw,
                .publish = mqtt_publishRaw};
            ha_discovery_begin(haCfg);
            s_haDiscoveryBegun = true;
        }
        if ((uint32_t)(now - s_lastAttemptMs) >= RETRY_INTERVAL_MS)
        {
            LOG_INFO(LogDomain::MQTT, "MQTT connecting host=%s port=%d clientId=%s", s_cfg.host, s_cfg.port, s_cfg.clientId);
            const bool ok = mqtt.connect(
                s_cfg.clientId,
                s_cfg.user,
                s_cfg.pass,
                s_topics.avail, 0, true, AVAIL_OFFLINE);

            s_lastAttemptMs = now;

            if (ok)
            {
                mqtt.publish(s_topics.avail, AVAIL_ONLINE, true);
                mqtt_subscribe();
                s_statePublishRequested = true; // force fresh retained snapshot after reconnect
                LOG_INFO(LogDomain::MQTT, "MQTT connected; attempting HA discovery publish");
            }
            else
            {
                LOG_WARN(LogDomain::MQTT, "MQTT connect failed state=%d", mqtt.state());
            }
        }
    }

    if (!mqtt.connected())
    {
        return false;
    }

    mqtt.loop();
    return true;
}

static bool publishState(const DeviceState &state)
{
    if (!mqtt.connected())
        return false;

    static char buf[2048]; // sized to fit expanded state payload
    static uint32_t lastFailLogMs = 0;
    if (!buildStateJson(state, buf, sizeof(buf)))
    {
        const uint32_t now = millis();
        if (now - lastFailLogMs > 5000)
        {
            LOG_WARN(LogDomain::MQTT, "Skipping state publish: buildStateJson failed");
            lastFailLogMs = now;
        }
        return false;
    }

    // safe length calc without strnlen
    size_t payloadLen = 0;
    while (payloadLen < sizeof(buf) && buf[payloadLen] != '\0')
    {
        ++payloadLen;
    }

    const bool retained = true;
    const bool ok = mqtt.publish(s_topics.state, buf, retained);
    logger_logEvery("state_publish", 5000, LogLevel::DEBUG, LogDomain::MQTT,
                    "Publish state topic=%s retained=%s bytes=%u", s_topics.state, retained ? "true" : "false", (unsigned)payloadLen);
    if (ok)
    {
        s_statePublishRequested = false;
        s_lastStatePublishMs = millis();
    }
    return ok;
}

bool mqtt_publishLog(const char *topicSuffix, const char *payload, bool retained)
{
    if (!mqtt.connected() || s_cfg.baseTopic == nullptr)
        return false;

    char topic[128];
    snprintf(topic, sizeof(topic), "%s/%s", s_cfg.baseTopic, topicSuffix ? topicSuffix : "");
    return mqtt.publish(topic, payload, retained);
}

void mqtt_begin(const MqttConfig &cfg, CommandHandlerFn cmdHandler)
{
    s_cfg = cfg;
    s_cmdHandler = cmdHandler;
    buildTopics();

    mqtt.setServer(cfg.host, cfg.port);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    mqtt.setBufferSize(2048);
    mqtt.setCallback(mqttCallback);
    s_initialized = true;

    logger_setMqttPublisher(mqtt_publishLog, mqtt_isConnected);

    LOG_INFO(LogDomain::MQTT, "MQTT init baseTopic=%s cmdTopic=%s stateTopic=%s", s_cfg.baseTopic, s_topics.cmd, s_topics.state);
}

void mqtt_reannounceDiscovery()
{
    ha_discovery_publishAll();
}

void mqtt_tick(const DeviceState &state)
{
    if (!mqtt_ensureConnected())
        return;

    if (mqtt_isConnected())
    {
        ha_discovery_publishAll();
    }

    const uint32_t now = millis();
    const uint32_t sinceLast = now - s_lastStatePublishMs;
    const bool heartbeatDue = sinceLast >= STATE_HEARTBEAT_MS;
    const bool intervalOk = sinceLast >= STATE_MIN_INTERVAL_MS;

    if ((s_statePublishRequested || heartbeatDue) && intervalOk)
    {
        publishState(state);
    }
}

void mqtt_requestStatePublish()
{
    s_statePublishRequested = true;
}

bool mqtt_publishAck(const char *reqId, const char *type, CmdStatus status, const char *msg)
{
    if (!mqtt.connected())
        return false;

    StaticJsonDocument<256> doc;
    doc["request_id"] = reqId ? reqId : "";
    doc["type"] = type ? type : "";
    doc["status"] = toString(status);
    doc["message"] = msg ? msg : "";

    char buf[256];
    const size_t written = serializeJson(doc, buf, sizeof(buf));
    if (written == 0 || written >= sizeof(buf))
        return false;

    return mqtt.publish(s_topics.ack, buf, false);
}

bool mqtt_isConnected()
{
    return mqtt.connected();
}