#include "logger.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "config.h"

#ifndef CFG_LOG_COLOR
// Serial logger emits ANSI escape codes when enabled; terminal should support ANSI colors/styles.
#define CFG_LOG_COLOR 1
#endif

static const char *s_baseTopic = nullptr;
static bool s_serialEnabled = true;
static bool s_mqttEnabled = true;
static bool s_highFreqEnabled = true;
static LoggerMqttPublishFn s_mqttPublisher = nullptr;
static LoggerMqttConnectedFn s_mqttConnectedFn = nullptr;

static constexpr size_t kThrottleSlots = 16;
static constexpr size_t kKeyTagLen = 12;
static constexpr size_t kMsgBufSize = 256;
static constexpr size_t kJsonBufSize = 512;
static constexpr const char *kLogTopicSuffix = "event/log";
static constexpr const char *kAnsiReset = "\x1B[0m";
static constexpr const char *kAnsiDim = "\x1B[2m";

enum class AnsiColor : uint8_t
{
    RED = 0,
    YELLOW,
    GREEN,
    CYAN,
    BLUE,
    MAGENTA,
    GRAY
};

static constexpr const char *kAnsiCodes[] = {
    "\x1B[31m", // RED
    "\x1B[33m", // YELLOW
    "\x1B[32m", // GREEN
    "\x1B[36m", // CYAN
    "\x1B[34m", // BLUE
    "\x1B[35m", // MAGENTA
    "\x1B[90m"  // GRAY
};

static const char *ansiCode(AnsiColor color)
{
    return kAnsiCodes[(uint8_t)color];
}

struct ThrottleEntry
{
    uint32_t hash = 0;
    uint32_t lastMs = 0;
    char keyTag[kKeyTagLen] = {0};
};
static ThrottleEntry s_throttle[kThrottleSlots] = {};

static uint32_t fnv1a32(const char *s)
{
    if (!s)
        return 0;
    uint32_t hash = 2166136261u;
    for (const uint8_t *p = (const uint8_t *)s; *p; ++p)
    {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash == 0 ? 1u : hash;
}

static void makeKeyTag(const char *key, char *outTag, size_t tagLen)
{
    if (!outTag || tagLen == 0)
        return;
    if (!key)
    {
        outTag[0] = '\0';
        return;
    }
    strncpy(outTag, key, tagLen - 1);
    outTag[tagLen - 1] = '\0';
}

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

static const char *levelToStringSerial(LogLevel lvl)
{
    switch (lvl)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARNING";
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

static const char *levelToStyle(LogLevel lvl)
{
    switch (lvl)
    {
    case LogLevel::ERROR:
        return "\x1B[1m\x1B[31m";
    case LogLevel::WARN:
        return "\x1B[1m\x1B[33m";
    case LogLevel::INFO:
        return "";
    case LogLevel::DEBUG:
        return "\x1B[2m\x1B[36m";
    default:
        return "\x1B[2m\x1B[90m";
    }
}

static const char *domainToAnsiColor(LogDomain dom)
{
    switch (dom)
    {
    case LogDomain::SYSTEM:
        return "\x1B[2m\x1B[90m";
    case LogDomain::WIFI:
        return ansiCode(AnsiColor::BLUE);
    case LogDomain::MQTT:
        return ansiCode(AnsiColor::MAGENTA);
    case LogDomain::PROBE:
        return "\x1B[2m\x1B[32m";
    case LogDomain::CAL:
        return "\x1B[2m\x1B[33m";
    case LogDomain::CONFIG:
        return ansiCode(AnsiColor::CYAN);
    case LogDomain::COMMAND:
        return ansiCode(AnsiColor::BLUE);
    case LogDomain::OTA:
        return ansiCode(AnsiColor::CYAN);
    default:
        return "\x1B[2m\x1B[90m";
    }
}

static void printPadded(const char *s, int width, bool leftAlign = true)
{
    if (width <= 0)
        return;

    char buf[24];
    const char *text = s ? s : "";
    if (leftAlign)
    {
        snprintf(buf, sizeof(buf), "%-*.*s", width, width, text);
    }
    else
    {
        snprintf(buf, sizeof(buf), "%*.*s", width, width, text);
    }
    Serial.print(buf);
}

static void appendTruncMarker(char *buf, size_t bufSize)
{
    if (!buf || bufSize < 4)
        return;
    const size_t end = bufSize - 1;
    buf[end - 3] = '.';
    buf[end - 2] = '.';
    buf[end - 1] = '.';
    buf[end] = '\0';
}

static size_t jsonEscapeString(const char *src, char *dst, size_t dstSize, bool &truncated)
{
    truncated = false;
    if (!dst || dstSize == 0)
    {
        truncated = (src && src[0] != '\0');
        return 0;
    }

    size_t out = 0;
    for (const char *p = src ? src : ""; *p; ++p)
    {
        const unsigned char c = (unsigned char)*p;
        const char *esc = nullptr;
        char scratch[7] = {0};
        size_t escLen = 0;

        switch (c)
        {
        case '\\':
            esc = "\\\\";
            escLen = 2;
            break;
        case '"':
            esc = "\\\"";
            escLen = 2;
            break;
        case '\n':
            esc = "\\n";
            escLen = 2;
            break;
        case '\r':
            esc = "\\r";
            escLen = 2;
            break;
        case '\t':
            esc = "\\t";
            escLen = 2;
            break;
        default:
            if (c < 0x20)
            {
                snprintf(scratch, sizeof(scratch), "\\u%04X", (unsigned)c);
                esc = scratch;
                escLen = 6;
            }
            else
            {
                esc = nullptr;
                escLen = 1;
            }
            break;
        }

        if (out + escLen > dstSize)
        {
            truncated = true;
            break;
        }

        if (esc)
        {
            memcpy(dst + out, esc, escLen);
            out += escLen;
        }
        else
        {
            dst[out++] = (char)c;
        }
    }

    if (truncated)
    {
        if (out + 3 <= dstSize)
        {
            dst[out++] = '.';
            dst[out++] = '.';
            dst[out++] = '.';
        }
    }

    return out;
}

static bool buildLogJson(char *out, size_t outSize, uint32_t tsSec, LogLevel lvl, LogDomain dom, const char *msg)
{
    if (!out || outSize < 8)
        return false;

    const int prefixLen = snprintf(out, outSize, "{\"ts\":%lu,\"lvl\":\"%s\",\"dom\":\"%s\",\"msg\":\"",
                                   (unsigned long)tsSec, levelToString(lvl), domainToString(dom));
    if (prefixLen <= 0 || (size_t)prefixLen >= outSize)
        return false;

    size_t pos = (size_t)prefixLen;
    if (outSize - pos < 3)
        return false;

    const size_t msgAvail = outSize - pos - 3;
    bool truncated = false;
    const size_t escapedLen = jsonEscapeString(msg, out + pos, msgAvail, truncated);
    pos += escapedLen;
    out[pos++] = '"';
    out[pos++] = '}';
    out[pos] = '\0';
    return true;
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

    char tsBuf[16];
    snprintf(tsBuf, sizeof(tsBuf), "[%6lu]", (unsigned long)tsSec);

#if CFG_LOG_COLOR
    Serial.print(kAnsiDim);
    Serial.print(ansiCode(AnsiColor::GRAY));
    Serial.print(tsBuf);
    Serial.print(kAnsiReset);
    Serial.print(" ");

    Serial.print(levelToStyle(lvl));
    printPadded(levelToStringSerial(lvl), 7, true);
    Serial.print(kAnsiReset);
    Serial.print(" ");

    Serial.print(domainToAnsiColor(dom));
    printPadded(domainToString(dom), 8, true);
    Serial.print(kAnsiReset);
    Serial.print(": ");

    Serial.print(kAnsiReset);
    Serial.println(msg ? msg : "");
#else
    Serial.print(tsBuf);
    Serial.print(" ");
    printPadded(levelToStringSerial(lvl), 7, true);
    Serial.print(" ");
    printPadded(domainToString(dom), 8, true);
    Serial.print(": ");
    Serial.println(msg ? msg : "");
#endif
}

static void logToMqtt(uint32_t tsSec, LogLevel lvl, LogDomain dom, const char *msg)
{
    if (!s_mqttEnabled || s_baseTopic == nullptr || s_mqttPublisher == nullptr)
        return;

    if (s_mqttConnectedFn && !s_mqttConnectedFn())
        return;

    char jsonBuf[kJsonBufSize];
    if (!buildLogJson(jsonBuf, sizeof(jsonBuf), tsSec, lvl, dom, msg))
    {
        return;
    }

    s_mqttPublisher(kLogTopicSuffix, jsonBuf, false);
}

void logger_log(LogLevel lvl, LogDomain dom, const char *fmt, ...)
{
    char msgBuf[kMsgBufSize];
    va_list args;
    va_start(args, fmt);
    const int needed = vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);
    if (needed < 0)
    {
        msgBuf[0] = '\0';
    }
    else if ((size_t)needed >= sizeof(msgBuf))
    {
        appendTruncMarker(msgBuf, sizeof(msgBuf));
    }

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
    uint32_t keyHash = fnv1a32(key);
    char keyTag[kKeyTagLen];
    makeKeyTag(key, keyTag, sizeof(keyTag));

    ThrottleEntry *slot = nullptr;
    ThrottleEntry *oldest = nullptr;
    uint32_t oldestAge = 0;
    if (key && key[0] != '\0' && intervalMs > 0)
    {
        for (size_t i = 0; i < kThrottleSlots; ++i)
        {
            ThrottleEntry &e = s_throttle[i];
            if (e.hash == keyHash && strncmp(e.keyTag, keyTag, sizeof(e.keyTag)) == 0)
            {
                slot = &e;
                break;
            }
            if (e.hash == 0 && slot == nullptr)
            {
                slot = &e;
            }
            if (e.hash != 0)
            {
                const uint32_t age = now - e.lastMs;
                if (!oldest || age > oldestAge)
                {
                    oldest = &e;
                    oldestAge = age;
                }
            }
        }

        if (slot == nullptr)
        {
            slot = oldest ? oldest : &s_throttle[0];
        }

        if (slot->hash != 0)
        {
            const uint32_t delta = now - slot->lastMs;
            if (delta < intervalMs)
            {
                return;
            }
        }

        slot->hash = keyHash;
        makeKeyTag(key, slot->keyTag, sizeof(slot->keyTag));
        slot->lastMs = now;
    }

    char msgBuf[kMsgBufSize];
    va_list args;
    va_start(args, fmt);
    const int needed = vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);
    va_end(args);
    if (needed < 0)
    {
        msgBuf[0] = '\0';
    }
    else if ((size_t)needed >= sizeof(msgBuf))
    {
        appendTruncMarker(msgBuf, sizeof(msgBuf));
    }

    uint32_t tsSec = now / 1000;
    logToSerial(tsSec, lvl, dom, msgBuf);
    logToMqtt(tsSec, lvl, dom, msgBuf);
}
