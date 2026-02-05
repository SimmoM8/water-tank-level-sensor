#pragma once

#include <stdint.h>

// Contract: logger is intended for single-threaded Arduino loop usage (not ISR-safe).

// Logging levels
enum class LogLevel : uint8_t
{
    DEBUG = 0,
    INFO,
    WARN,
    ERROR
};

// Logging domains
enum class LogDomain : uint8_t
{
    SYSTEM = 0,
    WIFI,
    MQTT,
    PROBE,
    CAL,
    CONFIG,
    COMMAND,
    OTA
};

void logger_begin(const char *baseTopic, bool serialEnabled = true, bool mqttEnabled = true);
void logger_setMqttEnabled(bool enabled);
void logger_setHighFreqEnabled(bool enabled);
bool logger_isHighFreqEnabled();
using LoggerMqttPublishFn = bool (*)(const char *topicSuffix, const char *payload, bool retained);
using LoggerMqttConnectedFn = bool (*)();
void logger_setMqttPublisher(LoggerMqttPublishFn publishFn, LoggerMqttConnectedFn isConnectedFn = nullptr);
void logger_log(LogLevel lvl, LogDomain dom, const char *fmt, ...);
void logger_logEvery(const char *key, uint32_t intervalMs, LogLevel lvl, LogDomain dom, const char *fmt, ...);

#define LOG_DEBUG(dom, fmt, ...) logger_log(LogLevel::DEBUG, dom, fmt, ##__VA_ARGS__)
#define LOG_INFO(dom, fmt, ...) logger_log(LogLevel::INFO, dom, fmt, ##__VA_ARGS__)
#define LOG_WARN(dom, fmt, ...) logger_log(LogLevel::WARN, dom, fmt, ##__VA_ARGS__)
#define LOG_ERROR(dom, fmt, ...) logger_log(LogLevel::ERROR, dom, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_EVERY(key, intervalMs, dom, fmt, ...) logger_logEvery(key, intervalMs, LogLevel::DEBUG, dom, fmt, ##__VA_ARGS__)
#define LOG_INFO_EVERY(key, intervalMs, dom, fmt, ...) logger_logEvery(key, intervalMs, LogLevel::INFO, dom, fmt, ##__VA_ARGS__)
#define LOG_WARN_EVERY(key, intervalMs, dom, fmt, ...) logger_logEvery(key, intervalMs, LogLevel::WARN, dom, fmt, ##__VA_ARGS__)
#define LOG_ERROR_EVERY(key, intervalMs, dom, fmt, ...) logger_logEvery(key, intervalMs, LogLevel::ERROR, dom, fmt, ##__VA_ARGS__)
