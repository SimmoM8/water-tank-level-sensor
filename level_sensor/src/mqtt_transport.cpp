#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <string.h>
#include <cstring>
#include <ctype.h>
#include <freertos/FreeRTOS.h>

#include "mqtt_transport.h"
#include "ha_discovery.h"
#include "state_json.h"
#include "commands.h"
#include "logger.h"
#include "domain_strings.h"

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef CFG_LOG_DEV
#define CFG_LOG_DEV 0
#endif

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
static portMUX_TYPE s_statePublishMux = portMUX_INITIALIZER_UNLOCKED;

struct Topics
{
    char state[96];
    char cmd[96];
    char ack[96];
    char avail[96];
    char otaProgress[128];
    char otaStatus[128];
};
static Topics s_topics{};

static uint32_t s_lastStatePublishMs = 0;
static const uint32_t STATE_MIN_INTERVAL_MS = 1000; // no more than once per second
static const uint32_t STATE_HEARTBEAT_MS = 30000;   // periodic retained snapshot
static uint32_t s_lastAttemptMs = 0;
static const uint32_t RETRY_INTERVAL_MS = 5000;
static bool s_loggedFirstConnectAttempt = false;
static bool s_seenConnectFailure = false;
static bool s_lastConnected = false;

static const char *AVAIL_ONLINE = "online";
static const char *AVAIL_OFFLINE = "offline";

const char *mqtt_stateToString(int state)
{
    switch (state)
    {
    case -4:
        return "MQTT_CONNECTION_TIMEOUT";
    case -3:
        return "MQTT_CONNECTION_LOST";
    case -2:
        return "MQTT_CONNECT_FAILED";
    case -1:
        return "MQTT_DISCONNECTED";
    case 0:
        return "MQTT_CONNECTED";
    case 1:
        return "MQTT_CONNECT_BAD_PROTOCOL";
    case 2:
        return "MQTT_CONNECT_BAD_CLIENT_ID";
    case 3:
        return "MQTT_CONNECT_UNAVAILABLE";
    case 4:
        return "MQTT_CONNECT_BAD_CREDENTIALS";
    case 5:
        return "MQTT_CONNECT_UNAUTHORIZED";
    default:
        return "unknown";
    }
}

static void buildPayloadPreview(const uint8_t *payload, size_t len, char *out, size_t outSize)
{
    if (!out || outSize == 0)
        return;
    const size_t cap = outSize - 1;
    size_t n = len < cap ? len : cap;
    for (size_t i = 0; i < n; ++i)
    {
        char c = static_cast<char>(payload[i]);
        if (isprint(static_cast<unsigned char>(c)) && c != '\n' && c != '\r')
            out[i] = c;
        else
            out[i] = '.';
    }
    out[n] = '\0';
}

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
    buildTopic(s_topics.otaProgress, sizeof(s_topics.otaProgress), "ota/progress");
    buildTopic(s_topics.otaStatus, sizeof(s_topics.otaStatus), "ota/status");
}

static const char *otaStatusTopicValue(const DeviceState &state)
{
    switch (state.ota.status)
    {
    case OtaStatus::IDLE:
        return "idle";
    case OtaStatus::DOWNLOADING:
        return "downloading";
    case OtaStatus::VERIFYING:
        return "verifying";
    case OtaStatus::APPLYING:
        return "applying";
    case OtaStatus::REBOOTING:
        return "rebooting";
    case OtaStatus::SUCCESS:
        return "success";
    case OtaStatus::ERROR:
        return "failed";
    }
    return "idle";
}

static void publishOtaShadowTopics(const DeviceState &state)
{
    char progressBuf[8];
    snprintf(progressBuf, sizeof(progressBuf), "%u", (unsigned int)state.ota_progress);
    mqtt.publish(s_topics.otaProgress, progressBuf, true);
    mqtt.publish(s_topics.otaStatus, otaStatusTopicValue(state), true);
}

// MQTT callback for incoming messages
/**
 * @brief Handles incoming MQTT messages for the command topic.
 *
 * Processes only command topic messages when a command handler is registered.
 * Rejects unexpected "PRESS" payloads to prevent false triggers, logs a preview
 * of the received payload (RX = received data), and dispatches valid payloads
 * to the registered command handler.
 *
 * @param topic    The MQTT topic of the incoming message.
 * @param payload  Pointer to the received payload bytes (RX data).
 * @param length   Length of the received payload in bytes.
 */
static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    static char topicBuf[128];
    if (topic)
    {
        strncpy(topicBuf, topic, sizeof(topicBuf));
        topicBuf[sizeof(topicBuf) - 1] = '\0';
    }
    else
    {
        topicBuf[0] = '\0';
    }

    // Only handle commands on the cmd topic and if a handler is set
    if (!s_cmdHandler || strcmp(topicBuf, s_topics.cmd) != 0)
        return;

    // Ignore unexpected PRESS message to avoid false triggers
    constexpr const char *PRESS = "PRESS";
    if (length == strlen(PRESS) && memcmp(payload, PRESS, length) == 0)
    {
        LOG_WARN(LogDomain::COMMAND, "[MQTT] Command rejected: unexpected PRESS payload topic=%s", topicBuf);
        return;
    }

    // IMPORTANT: PubSubClient reuses an internal buffer for incoming payloads.
    // Any mqtt.publish() (including via our logger) can overwrite `payload` while we are still using it.
    // So we must copy the bytes before any logging or further processing.
    static uint8_t cmdBuf[768];
    if (length == 0 || length >= sizeof(cmdBuf))
    {
        LOG_WARN(LogDomain::COMMAND, "[MQTT] Command rejected: payload too large len=%u", length);
        return;
    }

    memcpy(cmdBuf, payload, length);

    // Dispatch to application command handler FIRST.
    // Our logger may publish over MQTT; doing that inside the callback can cause
    // confusing re-entrancy / buffer reuse issues. We already copied the payload.
    s_cmdHandler(cmdBuf, length);

    // From here on, we only log using the copied buffer.

    // Build printable preview for logging
    char preview[121];
    buildPayloadPreview(cmdBuf, length, preview, sizeof(preview));

    bool hasNull = false;
    for (size_t i = 0; i < length; i++)
    {
        if (cmdBuf[i] == 0)
        {
            hasNull = true;
            break;
        }
    }
    LOG_DEBUG(LogDomain::MQTT, "[MQTT] Received topic=%s len=%u first=0x%02X last=0x%02X hasNull=%s payload_preview='%s'",
              topicBuf, length, cmdBuf[0], cmdBuf[length - 1], hasNull ? "true" : "false", preview);

    LOG_INFO(LogDomain::COMMAND, "[MQTT] Received command on %s (len=%u): %s", topicBuf, length, preview);

    LOG_DEBUG(LogDomain::COMMAND, "[MQTT] cmdBuf bytes: first=0x%02X last=0x%02X hasNull=%s",
              (unsigned)cmdBuf[0], (unsigned)cmdBuf[length - 1], hasNull ? "true" : "false");
}

static bool mqtt_subscribe()
{
    const bool cmdOk = mqtt.subscribe(s_topics.cmd);
#if CFG_LOG_DEV
    LOG_INFO(LogDomain::MQTT, "MQTT subscribe topic=%s result=%s", s_topics.cmd, cmdOk ? "ok" : "fail");
#endif
    if (!cmdOk)
    {
        LOG_WARN(LogDomain::MQTT, "MQTT subscribe failed topic=%s", s_topics.cmd);
    }
    return cmdOk;
}

static bool mqtt_ensureConnected()
{
    if (!s_initialized)
    {
        return false;
    }

    const uint32_t now = millis();
    const bool currentlyConnected = mqtt.connected();
    if (!currentlyConnected && s_lastConnected)
    {
        const int state = mqtt.state();
        LOG_WARN(LogDomain::MQTT, "MQTT disconnected state=%d (%s)", state, mqtt_stateToString(state));
        s_lastConnected = false;
    }

    if (!currentlyConnected)
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
                .deviceHw = s_cfg.deviceHw,
                .publish = mqtt_publishRaw};
            ha_discovery_begin(haCfg);
            s_haDiscoveryBegun = true;
        }
        if ((uint32_t)(now - s_lastAttemptMs) >= RETRY_INTERVAL_MS)
        {
            const bool hasUser = (s_cfg.user && s_cfg.user[0] != '\0');
            const char *authMode = hasUser ? "user" : "none";
            if (!s_loggedFirstConnectAttempt)
            {
                s_loggedFirstConnectAttempt = true;
                if (CFG_LOG_DEV == 0)
                {
                    LOG_INFO(LogDomain::MQTT,
                             "MQTT connecting host=%s port=%d clientId=%s auth=%s willTopic=%s",
                             s_cfg.host,
                             s_cfg.port,
                             s_cfg.clientId,
                             authMode,
                             s_topics.avail);
                }
            }
            logger_logEvery("mqtt_connecting", 30000, LogLevel::INFO, LogDomain::MQTT,
                            "MQTT connecting host=%s port=%d clientId=%s auth=%s willTopic=%s",
                            s_cfg.host, s_cfg.port, s_cfg.clientId, authMode, s_topics.avail);
            const bool ok = mqtt.connect(
                s_cfg.clientId,
                s_cfg.user,
                s_cfg.pass,
                s_topics.avail, 0, true, AVAIL_OFFLINE);

            s_lastAttemptMs = now;

            if (ok)
            {
                s_seenConnectFailure = false;
                s_lastConnected = true;
                mqtt.publish(s_topics.avail, AVAIL_ONLINE, true);
                const bool subOk = mqtt_subscribe();
                mqtt_requestStatePublish(); // force fresh retained snapshot after reconnect
                LOG_INFO(LogDomain::MQTT, "MQTT connected; presence=%s (online retained)", s_topics.avail);
                if (subOk)
                {
                    LOG_INFO(LogDomain::MQTT, "MQTT subscribed cmdTopic=%s", s_topics.cmd);
                }
#if CFG_LOG_DEV
                LOG_INFO(LogDomain::MQTT, "MQTT connected session host=%s port=%d clientId=%s auth=%s subscribe=%s",
                         s_cfg.host, s_cfg.port, s_cfg.clientId, authMode, subOk ? "ok" : "partial_fail");
#endif
            }
            else
            {
                const int state = mqtt.state();
                const char *stateStr = mqtt_stateToString(state);
                if (!s_seenConnectFailure)
                {
                    s_seenConnectFailure = true;
                    LOG_WARN(LogDomain::MQTT, "MQTT connect failed state=%d (%s)", state, stateStr);
                    if (state == 4 || state == 5)
                    {
                        LOG_WARN(LogDomain::MQTT, "Check MQTT_USER/MQTT_PASS (secrets.h) and broker ACL");
                    }
                }
                logger_logEvery("mqtt_connect_fail", 30000, LogLevel::WARN, LogDomain::MQTT,
                                "MQTT connect failed state=%d (%s)", state, stateStr);
                if (state == 4 || state == 5)
                {
                    logger_logEvery("mqtt_connect_auth_hint", 30000, LogLevel::WARN, LogDomain::MQTT,
                                    "Check MQTT_USER/MQTT_PASS (secrets.h) and broker ACL");
                }
            }
        }
    }

    if (!mqtt.connected())
    {
        return false;
    }

    s_lastConnected = true;
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
        publishOtaShadowTopics(state);
        s_lastStatePublishMs = millis();
    }
    else
    {
        const int stateCode = mqtt.state();
        logger_logEvery("mqtt_publish_state_fail", 5000, LogLevel::WARN, LogDomain::MQTT,
                        "MQTT publish failed topic=%s bytes=%u state=%d (%s)",
                        s_topics.state,
                        (unsigned)payloadLen,
                        stateCode,
                        mqtt_stateToString(stateCode));
    }
    return ok;
}

bool mqtt_publishLog(const char *topicSuffix, const char *payload, bool retained)
{
    if (!mqtt.connected() || s_cfg.baseTopic == nullptr)
        return false;

    char topic[128];
    int n = 0;
    if (topicSuffix == nullptr || topicSuffix[0] == '\0')
    {
        n = snprintf(topic, sizeof(topic), "%s", s_cfg.baseTopic);
    }
    else
    {
        n = snprintf(topic, sizeof(topic), "%s/%s", s_cfg.baseTopic, topicSuffix);
    }
    if (n < 0 || n >= (int)sizeof(topic))
    {
        return false;
    }
    const bool ok = mqtt.publish(topic, payload, retained);
    if (!ok)
    {
        size_t payloadLen = 0;
        if (payload)
        {
            while (payload[payloadLen] != '\0')
            {
                ++payloadLen;
            }
        }
        const int stateCode = mqtt.state();
        logger_logEvery("mqtt_publish_log_fail", 5000, LogLevel::WARN, LogDomain::MQTT,
                        "MQTT publish failed topic=%s bytes=%u state=%d (%s)",
                        topic,
                        (unsigned)payloadLen,
                        stateCode,
                        mqtt_stateToString(stateCode));
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
    mqtt.setBufferSize(2048);
    mqtt.setCallback(mqttCallback);
    s_initialized = true;

    logger_setMqttPublisher(mqtt_publishLog, mqtt_isConnected);

    LOG_INFO(LogDomain::MQTT, "MQTT init baseTopic=%s cmdTopic=%s stateTopic=%s", s_cfg.baseTopic, s_topics.cmd, s_topics.state);
    if (CFG_LOG_DEV == 0 && (!s_cfg.user || s_cfg.user[0] == '\0'))
    {
        LOG_WARN(LogDomain::MQTT, "MQTT credentials not set (MQTT_USER empty). Broker may reject connection.");
    }
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
    const bool requested = mqtt_takeStatePublishRequested();

    if ((requested || heartbeatDue) && intervalOk)
    {
        if (!publishState(state) && requested)
        {
            // Retry on next loop if this explicit request failed.
            mqtt_requestStatePublish();
        }
    }
    else if (requested)
    {
        // Keep explicit requests pending until publish interval permits sending.
        mqtt_requestStatePublish();
    }
}

void mqtt_requestStatePublish()
{
    portENTER_CRITICAL(&s_statePublishMux);
    s_statePublishRequested = true;
    portEXIT_CRITICAL(&s_statePublishMux);
}

bool mqtt_takeStatePublishRequested()
{
    portENTER_CRITICAL(&s_statePublishMux);
    const bool requested = s_statePublishRequested;
    s_statePublishRequested = false;
    portEXIT_CRITICAL(&s_statePublishMux);
    return requested;
}

bool mqtt_publishAck(const char *reqId, const char *type, const char *status, const char *msg)
{
    if (!mqtt.connected())
        return false;

    StaticJsonDocument<256> doc;
    doc["request_id"] = reqId ? reqId : "";
    doc["type"] = type ? type : "";
    doc["status"] = status ? status : "";
    doc["message"] = msg ? msg : "";

    char buf[256];
    const size_t written = serializeJson(doc, buf, sizeof(buf));
    if (written == 0 || written >= sizeof(buf))
        return false;

    const bool ok = mqtt.publish(s_topics.ack, buf, false);
    if (!ok)
    {
        const int stateCode = mqtt.state();
        logger_logEvery("mqtt_publish_ack_fail", 5000, LogLevel::WARN, LogDomain::MQTT,
                        "MQTT publish failed topic=%s bytes=%u state=%d (%s)",
                        s_topics.ack,
                        (unsigned)written,
                        stateCode,
                        mqtt_stateToString(stateCode));
    }
    return ok;
}

bool mqtt_isConnected()
{
    return mqtt.connected();
}
