#include "logger.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *s_baseTopic = nullptr;
static bool s_serialEnabled = true;
static bool s_mqttEnabled = true;
static bool s_highFreqEnabled = true;
static LoggerMqttPublishFn s_mqttPublisher = nullptr;
static LoggerMqttConnectedFn s_mqttConnectedFn = nullptr;

struct ThrottleEntry
{
    const char *key;
    uint32_t lastMs;
};
static ThrottleEntry s_throttle[16] = {};

static const char *levelToString(LogLevel lvl)
{
    switch (lvl)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    default:
        return "UNK";
    }
}

static const char *domainToString(LogDomain dom)
{
    switch (dom)
    {
    case LogDomain::SYSTEM:
        return "SYSTEM";
    case LogDomain::WIFI:
        return "WIFI";
    case LogDomain::MQTT:
        return "MQTT";
    case LogDomain::PROBE:
        return "PROBE";
    case LogDomain::CAL:
        return "CAL";
    case LogDomain::CONFIG:
        return "CONFIG";
    case LogDomain::COMMAND:
        return "COMMAND";
    case LogDomain::OTA:
        return "OTA";
    default:
        return "UNK";
    }
}

static void sanitizeMessage(char *buf, size_t bufSize)
{
    for (size_t i = 0; i < bufSize && buf[i] != '\0'; ++i)
    {
        if (buf[i] == '"')
        {
            buf[i] = '\'';
        }
    }
}

void logger_begin(const char *baseTopic, bool serialEnabled, bool mqttEnabled)
{
    s_baseTopic = baseTopic;
    s_serialEnabled = serialEnabled;
    s_mqttEnabled = mqttEnabled;
}

void logger_setMqttEnabled(bool enabled)
{
    s_mqttEnabled = enabled;
}

void logger_setMqttPublisher(LoggerMqttPublishFn publishFn, LoggerMqttConnectedFn isConnectedFn)
{
    s_mqttPublisher = publishFn;
    s_mqttConnectedFn = isConnectedFn;
}

void logger_setHighFreqEnabled(bool enabled)
{
    if (s_highFreqEnabled == enabled)
    {
        return;
    }
    s_highFreqEnabled = enabled;
    logger_log(LogLevel::INFO, LogDomain::SYSTEM, "High-frequency logging %s", enabled ? "enabled" : "disabled");
}

bool logger_isHighFreqEnabled()
{
    return s_highFreqEnabled;
}

static void logToSerial(uint32_t tsSec, LogLevel lvl, LogDomain dom, const char *msg)
{
    if (!s_serialEnabled)
        return;

    Serial.print("[");
    Serial.print(tsSec);
    Serial.print("] ");
    Serial.print(levelToString(lvl));
    Serial.print(" ");
    Serial.print(domainToString(dom));
    Serial.print(": ");
    Serial.println(msg);
}

static void logToMqtt(uint32_t tsSec, LogLevel lvl, LogDomain dom, const char *msg)
{
    if (!s_mqttEnabled || s_baseTopic == nullptr || s_mqttPublisher == nullptr)
        return;

    if (s_mqttConnectedFn && !s_mqttConnectedFn())
        return;

    char safeMsg[192];
    strncpy(safeMsg, msg, sizeof(safeMsg));
    safeMsg[sizeof(safeMsg) - 1] = '\0';
    sanitizeMessage(safeMsg, sizeof(safeMsg));

    char jsonBuf[256];
    snprintf(jsonBuf, sizeof(jsonBuf), "{\"ts\":%lu,\"lvl\":\"%s\",\"dom\":\"%s\",\"msg\":\"%s\"}",
             (unsigned long)tsSec, levelToString(lvl), domainToString(dom), safeMsg);

    s_mqttPublisher("event/log", jsonBuf, false);
}

void logger_log(LogLevel lvl, LogDomain dom, const char *fmt, ...)
{
    char msgBuf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    uint32_t tsSec = millis() / 1000;
    logToSerial(tsSec, lvl, dom, msgBuf);
    logToMqtt(tsSec, lvl, dom, msgBuf);
}

void logger_logEvery(const char *key, uint32_t intervalMs, LogLevel lvl, LogDomain dom, const char *fmt, ...)
{
    if (!s_highFreqEnabled)
    {
        return;
    }

    uint32_t now = millis();
    ThrottleEntry *slot = nullptr;
    for (size_t i = 0; i < (sizeof(s_throttle) / sizeof(s_throttle[0])); ++i)
    {
        if (s_throttle[i].key == key)
        {
            slot = &s_throttle[i];
            break;
        }
        if (slot == nullptr && s_throttle[i].key == nullptr)
        {
            slot = &s_throttle[i];
        }
    }

    if (slot == nullptr)
    {
        slot = &s_throttle[0];
    }

    if (intervalMs > 0 && slot->key != nullptr)
    {
        uint32_t delta = now - slot->lastMs;
        if (delta < intervalMs)
        {
            return;
        }
    }

    slot->key = key;
    slot->lastMs = now;

    char msgBuf[192];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);

    uint32_t tsSec = now / 1000;
    logToSerial(tsSec, lvl, dom, msgBuf);
    logToMqtt(tsSec, lvl, dom, msgBuf);
}
