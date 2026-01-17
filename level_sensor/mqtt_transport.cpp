#include "mqtt_transport.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>

#include "state_json.h"
#include "commands.h"

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static MqttConfig s_cfg{};
static CommandHandlerFn s_cmdHandler = nullptr;

struct Topics
{
    char state[96];
    char cmd[96];
    char ack[96];
    char avail[96];
};
static Topics s_topics{};

static bool stateDirty = true;
static uint32_t lastStatePublishMs = 0;
static const uint32_t STATE_PUBLISH_INTERVAL_MS = 30000; // periodic retained snapshot

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

static void mqtt_ensureConnected()
{
    if (mqtt.connected() || WiFi.status() != WL_CONNECTED)
        return;

    const bool ok = mqtt.connect(
        s_cfg.clientId,
        s_cfg.user,
        s_cfg.pass,
        s_topics.avail, 0, true, AVAIL_OFFLINE);

    if (!ok)
        return;

    mqtt.publish(s_topics.avail, AVAIL_ONLINE, true);
    mqtt_subscribe();

    stateDirty = true; // force fresh retained snapshot after reconnect
}

static bool publishState(const DeviceState &state)
{
    if (!mqtt.connected())
        return false;

    static char buf[1024]; // tuned for current state payload size
    if (!buildStateJson(state, buf, sizeof(buf)))
        return false;

    const bool ok = mqtt.publish(s_topics.state, buf, true);
    if (ok)
    {
        stateDirty = false;
        lastStatePublishMs = millis();
    }
    return ok;
}

void mqtt_begin(const MqttConfig &cfg, CommandHandlerFn cmdHandler)
{
    s_cfg = cfg;
    s_cmdHandler = cmdHandler;
    buildTopics();

    mqtt.setServer(cfg.host, cfg.port);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    mqtt.setBufferSize(1024);
    mqtt.setCallback(mqttCallback);
}

void mqtt_tick(const DeviceState &state)
{
    mqtt_ensureConnected();
    if (!mqtt.connected())
        return;

    mqtt.loop();

    const uint32_t now = millis();
    if (stateDirty || (now - lastStatePublishMs) >= STATE_PUBLISH_INTERVAL_MS)
    {
        publishState(state);
    }
}

void mqtt_requestStatePublish()
{
    stateDirty = true;
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