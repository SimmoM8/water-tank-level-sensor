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
#ifndef CFG_OTA_DEV_LOGS
#define CFG_OTA_DEV_LOGS CFG_LOG_DEV
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
static bool s_rxConfirmedForSession = false;
static bool s_stateBuildPaused = false;
static uint32_t s_stateBuildLastLogMs = 0;
static const uint32_t STATE_BUILD_STILL_PAUSED_MS = 60000;
static bool s_discoveryPending = false;
static uint32_t s_discoveryRetryAtMs = 0;
static const uint32_t DISCOVERY_RETRY_MS = 60000;
static bool s_connectionSubscribed = false;
static bool s_connectionOnlinePublished = false;
static bool s_readyLogged = false;

static const char *AVAIL_ONLINE = "online";
static const char *AVAIL_OFFLINE = "offline";

const char *mqtt_stateToString(int state)
{
    switch (state)
    {
    case -4:
        return "timeout";
    case -3:
        return "connection_lost";
    case -2:
        return "connect_failed";
    case -1:
        return "disconnected";
    case 0:
        return "connected";
    case 1:
        return "bad_protocol";
    case 2:
        return "bad_client_id";
    case 3:
        return "unavailable";
    case 4:
        return "bad_credentials";
    case 5:
        return "not_authorized";
    default:
        return "unknown";
    }
}

static bool mqtt_devLogsEnabled()
{
    return (CFG_LOG_DEV != 0) || (CFG_OTA_DEV_LOGS != 0);
}

static bool mqtt_nonDevMode()
{
    return !mqtt_devLogsEnabled();
}

static const char *mqtt_stateHint(int state)
{
    switch (state)
    {
    case 4:
    case 5:
        return "check MQTT username/password";
    case -4:
    case -3:
    case -2:
        return "check broker IP/network";
    case 2:
        return "check clientId";
    default:
        return "check broker/network";
    }
}

static bool mqtt_extractCommandType(const uint8_t *payload, size_t len, char *out, size_t outSize)
{
    if (!payload || !out || outSize == 0)
    {
        return false;
    }
    out[0] = '\0';
    StaticJsonDocument<256> doc;
    const DeserializationError err = deserializeJson(doc, payload, len);
    if (err)
    {
        return false;
    }
    const char *type = doc["type"];
    if (!type || type[0] == '\0')
    {
        return false;
    }
    strncpy(out, type, outSize);
    out[outSize - 1] = '\0';
    return true;
}

static bool mqtt_isReadyForSession()
{
    return s_connectionSubscribed && s_connectionOnlinePublished && !s_discoveryPending;
}

static void mqtt_logReadyIfComplete()
{
    if (s_readyLogged || !mqtt_isReadyForSession())
    {
        return;
    }
    if (mqtt_nonDevMode())
    {
        LOG_INFO(LogDomain::MQTT, "MQTT: Ready \xE2\x9C\x93");
    }
    else
    {
        LOG_INFO(LogDomain::MQTT, "MQTT ready connected=%s subscribed=%s online=%s discovery_pending=%s",
                 mqtt.connected() ? "true" : "false",
                 s_connectionSubscribed ? "true" : "false",
                 s_connectionOnlinePublished ? "true" : "false",
                 s_discoveryPending ? "true" : "false");
    }
    s_readyLogged = true;
}

static const char *stateJsonErrorToShort(StateJsonError err)
{
    switch (err)
    {
    case StateJsonError::OK:
        return "ok";
    case StateJsonError::EMPTY:
        return "empty";
    case StateJsonError::DOC_OVERFLOW:
        return "doc_overflow";
    case StateJsonError::OUT_TOO_SMALL:
        return "out_too_small";
    case StateJsonError::SERIALIZE_FAILED:
        return "serialize_failed";
    case StateJsonError::INTERNAL_MISMATCH:
        return "internal_mismatch";
    default:
        return "unknown";
    }
}

static void logStateJsonDiag(const char *prefix, StateJsonError err, const StateJsonDiag &diag)
{
    if (!mqtt_devLogsEnabled())
    {
        return;
    }
    LOG_DEBUG(LogDomain::MQTT,
              "%s reason=%s bytes=%u required=%u outSize=%u jsonCapacity=%u fields=%u writes=%u empty_root=%s overflowed=%s",
              prefix,
              stateJsonErrorToShort(err),
              (unsigned)diag.bytes,
              (unsigned)diag.required,
              (unsigned)diag.outSize,
              (unsigned)diag.jsonCapacity,
              (unsigned)diag.fields,
              (unsigned)diag.writes,
              diag.empty_root ? "true" : "false",
              diag.overflowed ? "true" : "false");
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

    const bool justConfirmedRx = !s_rxConfirmedForSession;
    if (justConfirmedRx)
    {
        s_rxConfirmedForSession = true;
    }

    if (mqtt_nonDevMode())
    {
        char typeBuf[40];
        const bool hasType = mqtt_extractCommandType(cmdBuf, length, typeBuf, sizeof(typeBuf));
        if (hasType)
        {
            LOG_INFO(LogDomain::COMMAND, "MQTT: Command received: %s%s",
                     typeBuf,
                     justConfirmedRx ? " (RX confirmed)" : "");
        }
        else
        {
            LOG_INFO(LogDomain::COMMAND, "MQTT: Command received (%u bytes)%s",
                     length,
                     justConfirmedRx ? " (RX confirmed)" : "");
        }
        return;
    }

    // Build printable preview for logging (dev only).
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
    if (mqtt_devLogsEnabled())
    {
        LOG_INFO(LogDomain::MQTT, "MQTT subscribe topic=%s result=%s", s_topics.cmd, cmdOk ? "ok" : "fail");
    }
    return cmdOk;
}

static void mqtt_handleDiscoveryResult(HaDiscoveryResult result, bool fromRetry)
{
    switch (result)
    {
    case HaDiscoveryResult::PUBLISHED:
        s_discoveryPending = false;
        if (mqtt_nonDevMode())
        {
            LOG_INFO(LogDomain::MQTT, "MQTT: Home Assistant discovery: published");
        }
        else
        {
            LOG_INFO(LogDomain::MQTT, "HA discovery published%s", fromRetry ? " (retry)" : "");
        }
        mqtt_logReadyIfComplete();
        break;
    case HaDiscoveryResult::ALREADY_PUBLISHED:
        s_discoveryPending = false;
        if (mqtt_nonDevMode())
        {
            LOG_INFO(LogDomain::MQTT, "MQTT: Home Assistant discovery: already published");
        }
        else
        {
            LOG_DEBUG(LogDomain::MQTT, "HA discovery already published");
        }
        mqtt_logReadyIfComplete();
        break;
    case HaDiscoveryResult::NOT_INITIALIZED:
    case HaDiscoveryResult::FAILED:
        s_discoveryPending = true;
        s_discoveryRetryAtMs = millis() + DISCOVERY_RETRY_MS;
        if (mqtt_nonDevMode())
        {
            logger_logEvery("mqtt_ha_discovery_failed", DISCOVERY_RETRY_MS, LogLevel::WARN, LogDomain::MQTT,
                            "MQTT: Home Assistant discovery failed (will retry)");
        }
        else
        {
            logger_logEvery("mqtt_ha_discovery_failed_dev", DISCOVERY_RETRY_MS, LogLevel::WARN, LogDomain::MQTT,
                            "HA discovery failed result=%d (will retry)", static_cast<int>(result));
        }
        break;
    }
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
        if (mqtt_nonDevMode())
        {
            LOG_WARN(LogDomain::MQTT, "MQTT: Disconnected (%s)", mqtt_stateToString(state));
        }
        else
        {
            LOG_WARN(LogDomain::MQTT, "MQTT disconnected state=%d (%s)", state, mqtt_stateToString(state));
        }
        s_lastConnected = false;
        s_loggedFirstConnectAttempt = false;
        s_readyLogged = false;
        s_connectionSubscribed = false;
        s_connectionOnlinePublished = false;
        s_discoveryPending = false;
        s_discoveryRetryAtMs = 0;
        s_rxConfirmedForSession = false;
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
            const char *authMode = hasUser ? "yes" : "no";
            const bool firstConnectAttempt = !s_loggedFirstConnectAttempt;
            if (firstConnectAttempt)
            {
                s_loggedFirstConnectAttempt = true;
                if (mqtt_nonDevMode())
                {
                    LOG_INFO(LogDomain::MQTT, "MQTT: Connecting...");
                }
                else
                {
                    LOG_INFO(LogDomain::MQTT,
                             "MQTT connecting host=%s port=%d clientId=%s auth=%s",
                             s_cfg.host,
                             s_cfg.port,
                             s_cfg.clientId,
                             authMode);
                }
            }
            else
            {
                if (mqtt_nonDevMode())
                {
                    logger_logEvery("mqtt_connecting", 30000, LogLevel::INFO, LogDomain::MQTT,
                                    "MQTT: Connecting...");
                }
                else
                {
                    logger_logEvery("mqtt_connecting", 30000, LogLevel::INFO, LogDomain::MQTT,
                                    "MQTT connecting host=%s port=%d clientId=%s auth=%s",
                                    s_cfg.host, s_cfg.port, s_cfg.clientId, authMode);
                }
            }
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
                s_rxConfirmedForSession = false;
                s_loggedFirstConnectAttempt = false;
                s_readyLogged = false;
                const bool availOk = mqtt.publish(s_topics.avail, AVAIL_ONLINE, true);
                const bool subOk = mqtt_subscribe();
                mqtt_requestStatePublish(); // force fresh retained snapshot after reconnect
                if (mqtt_nonDevMode())
                {
                    LOG_INFO(LogDomain::MQTT, "MQTT: Connected \xE2\x9C\x93");
                }
                else
                {
                    LOG_INFO(LogDomain::MQTT, "MQTT connected");
                }
                if (subOk)
                {
                    if (mqtt_nonDevMode())
                    {
                        LOG_INFO(LogDomain::MQTT, "MQTT: Subscribed to commands \xE2\x9C\x93");
                    }
                    else
                    {
                        LOG_INFO(LogDomain::MQTT, "MQTT subscribed cmd=%s", s_topics.cmd);
                    }
                }
                else
                {
                    if (mqtt_nonDevMode())
                    {
                        LOG_WARN(LogDomain::MQTT, "MQTT: Subscribe to commands failed");
                    }
                    else
                    {
                        LOG_WARN(LogDomain::MQTT, "MQTT subscribe failed cmd=%s", s_topics.cmd);
                    }
                }
                s_connectionSubscribed = subOk;
                s_connectionOnlinePublished = availOk;

                if (!availOk)
                {
                    if (mqtt_nonDevMode())
                    {
                        LOG_WARN(LogDomain::MQTT, "MQTT: Online status publish failed");
                    }
                    else
                    {
                        LOG_WARN(LogDomain::MQTT, "MQTT online publish failed topic=%s", s_topics.avail);
                    }
                }
                else
                {
                    if (mqtt_devLogsEnabled())
                    {
                        LOG_DEBUG(LogDomain::MQTT, "MQTT online published topic=%s retained=true", s_topics.avail);
                    }
                }
                HaDiscoveryResult discResult = ha_discovery_publishAll();
                mqtt_handleDiscoveryResult(discResult, false);

                if (mqtt_devLogsEnabled())
                {
                    LOG_INFO(LogDomain::MQTT, "MQTT connected details host=%s port=%d clientId=%s auth=%s subscribe=%s online=%s",
                             s_cfg.host, s_cfg.port, s_cfg.clientId, authMode, subOk ? "ok" : "fail", availOk ? "ok" : "fail");
                }
                mqtt_logReadyIfComplete();
            }
            else
            {
                const int state = mqtt.state();
                const char *stateStr = mqtt_stateToString(state);
                const char *hint = mqtt_stateHint(state);
                if (!s_seenConnectFailure)
                {
                    s_seenConnectFailure = true;
                    if (mqtt_nonDevMode())
                    {
                        if (state == 4 || state == 5)
                        {
                            LOG_WARN(LogDomain::MQTT, "MQTT: Connect failed: bad credentials (check MQTT username/password)");
                        }
                        else if (state == -4 || state == -3 || state == -2)
                        {
                            LOG_WARN(LogDomain::MQTT, "MQTT: Connect failed: timeout/unreachable (check broker IP/network)");
                        }
                        else
                        {
                            LOG_WARN(LogDomain::MQTT, "MQTT: Connect failed: %s (%s)", stateStr, hint);
                        }
                    }
                    else
                    {
                        LOG_WARN(LogDomain::MQTT, "MQTT connect failed rc=%d (%s) hint=%s", state, stateStr, hint);
                    }
                }
                else
                {
                    if (mqtt_nonDevMode())
                    {
                        if (state == 4 || state == 5)
                        {
                            logger_logEvery("mqtt_connect_fail", 30000, LogLevel::WARN, LogDomain::MQTT,
                                            "MQTT: Connect failed: bad credentials (check MQTT username/password)");
                        }
                        else if (state == -4 || state == -3 || state == -2)
                        {
                            logger_logEvery("mqtt_connect_fail", 30000, LogLevel::WARN, LogDomain::MQTT,
                                            "MQTT: Connect failed: timeout/unreachable (check broker IP/network)");
                        }
                        else
                        {
                            logger_logEvery("mqtt_connect_fail", 30000, LogLevel::WARN, LogDomain::MQTT,
                                            "MQTT: Connect failed: %s (%s)", stateStr, hint);
                        }
                    }
                    else
                    {
                        logger_logEvery("mqtt_connect_fail", 30000, LogLevel::WARN, LogDomain::MQTT,
                                        "MQTT connect failed rc=%d (%s) hint=%s", state, stateStr, hint);
                    }
                }
                if (mqtt_devLogsEnabled())
                {
                    LOG_DEBUG(LogDomain::MQTT, "MQTT connect fail rc=%d (%s)", state, stateStr);
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
    StateJsonDiag diag{};
    const StateJsonError jsonErr = buildStateJson(state, buf, sizeof(buf), &diag);
    if (jsonErr != StateJsonError::OK)
    {
        const uint32_t now = millis();
        if (!s_stateBuildPaused)
        {
            s_stateBuildPaused = true;
            s_stateBuildLastLogMs = now;
            LOG_WARN(LogDomain::MQTT, "MQTT: State publish paused (payload too large) - enable dev logs for details");
            logStateJsonDiag("State JSON diag", jsonErr, diag);
        }
        else if ((uint32_t)(now - s_stateBuildLastLogMs) >= STATE_BUILD_STILL_PAUSED_MS)
        {
            s_stateBuildLastLogMs = now;
            LOG_WARN(LogDomain::MQTT, "MQTT: State publish paused (payload too large) - enable dev logs for details");
            logStateJsonDiag("State JSON diag", jsonErr, diag);
        }
        return false;
    }

    if (s_stateBuildPaused)
    {
        LOG_INFO(LogDomain::MQTT, "MQTT: State publish resumed");
        s_stateBuildPaused = false;
        s_stateBuildLastLogMs = 0;
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

    const bool hasUser = (s_cfg.user && s_cfg.user[0] != '\0');
    if (mqtt_nonDevMode())
    {
        LOG_INFO(LogDomain::MQTT, "MQTT: Initiating (broker=%s:%d, auth=%s)", s_cfg.host, s_cfg.port, hasUser ? "yes" : "no");
    }
    else
    {
        LOG_INFO(LogDomain::MQTT, "MQTT init baseTopic=%s broker=%s:%d clientId=%s auth=%s cmdTopic=%s availTopic=%s",
                 s_cfg.baseTopic, s_cfg.host, s_cfg.port, s_cfg.clientId, hasUser ? "yes" : "no", s_topics.cmd, s_topics.avail);
    }
    if (mqtt_nonDevMode() && (!s_cfg.user || s_cfg.user[0] == '\0'))
    {
        LOG_WARN(LogDomain::MQTT, "MQTT: Credentials not set (username empty); broker may reject connection.");
    }
}

void mqtt_reannounceDiscovery()
{
    const HaDiscoveryResult result = ha_discovery_publishAll();
    mqtt_handleDiscoveryResult(result, false);
}

void mqtt_tick(const DeviceState &state)
{
    if (!mqtt_ensureConnected())
        return;

    if (mqtt_isConnected() && s_discoveryPending)
    {
        const uint32_t nowMs = millis();
        if (s_discoveryRetryAtMs == 0 || (int32_t)(nowMs - s_discoveryRetryAtMs) >= 0)
        {
            const HaDiscoveryResult result = ha_discovery_publishAll();
            mqtt_handleDiscoveryResult(result, true);
        }
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
