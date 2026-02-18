#include "ota_service.h"
#include "ota_events.h"
#include "ota_task.h"
#include <WiFiClientSecure.h>
#include "ota_ca_cert.h"
#include "mbedtls/sha256.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509.h"
#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include "esp_app_format.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "logger.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include "domain_strings.h"
#include "mqtt_transport.h"
#include "storage_nvs.h"
#include "wifi_provisioning.h"
#include "semver.h"
#include "version.h"
#include <freertos/task.h>
#include <esp_heap_caps.h>

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef OTA_MIN_BYTES
#define OTA_MIN_BYTES 1024
#endif
#ifndef CFG_OTA_MANIFEST_URL
// Manifest must be uploaded as a GitHub Release asset (latest/download/dev.json).
// Expected JSON shape: {"version":"...","url":"https://github.com/SimmoM8/water-tank-level-sensor/releases/download/<tag>/level_sensor.ino.bin","sha256":"<64 hex>"}
// Firmware binary must also be a Release asset with stable name level_sensor.ino.bin.
#define CFG_OTA_MANIFEST_URL "https://github.com/SimmoM8/water-tank-level-sensor/releases/latest/download/dev.json"
#endif
#ifndef CFG_OTA_GUARD_REQUIRE_MQTT_CONNECTED
#define CFG_OTA_GUARD_REQUIRE_MQTT_CONNECTED 0
#endif
#ifndef CFG_OTA_GUARD_MIN_WIFI_RSSI
#define CFG_OTA_GUARD_MIN_WIFI_RSSI -127
#endif
#ifndef CFG_OTA_HTTP_CONNECT_TIMEOUT_MS
#define CFG_OTA_HTTP_CONNECT_TIMEOUT_MS 8000u
#endif
#ifndef CFG_OTA_HTTP_READ_TIMEOUT_MS
#define CFG_OTA_HTTP_READ_TIMEOUT_MS 10000u
#endif
#ifndef CFG_OTA_HTTP_MAX_RETRIES
#define CFG_OTA_HTTP_MAX_RETRIES 3u
#endif
#ifndef CFG_OTA_HTTP_RETRY_BASE_MS
#define CFG_OTA_HTTP_RETRY_BASE_MS 1500u
#endif
#ifndef CFG_OTA_HTTP_RETRY_MAX_BACKOFF_MS
#define CFG_OTA_HTTP_RETRY_MAX_BACKOFF_MS 10000u
#endif
#ifndef CFG_OTA_DOWNLOAD_HEARTBEAT_MS
#define CFG_OTA_DOWNLOAD_HEARTBEAT_MS 1000u
#endif
#ifndef CFG_LOG_DEV
#define CFG_LOG_DEV 0
#endif
#ifndef CFG_OTA_DEV_LOGS
#ifdef CFG_LOG_DEV
#define CFG_OTA_DEV_LOGS CFG_LOG_DEV
#else
#define CFG_OTA_DEV_LOGS 0
#endif
#endif
#ifndef CFG_OTA_PROGRESS_NEWLINES
#define CFG_OTA_PROGRESS_NEWLINES 0
#endif
#ifndef CFG_OTA_PROGRESS_PCT_STEP
#define CFG_OTA_PROGRESS_PCT_STEP 2u
#endif
#ifndef CFG_OTA_PROGRESS_BYTES_STEP
#define CFG_OTA_PROGRESS_BYTES_STEP 49152u
#endif
#ifndef CFG_OTA_ANSI_COLORS
#ifdef CFG_LOG_COLOR
#define CFG_OTA_ANSI_COLORS CFG_LOG_COLOR
#else
#define CFG_OTA_ANSI_COLORS 1
#endif
#endif

static constexpr uint8_t MAX_OTA_RETRIES = 3u;
static constexpr uint32_t BASE_RETRY_DELAY_MS = 5000u;

static const char *s_hostName = nullptr;
static const char *s_password = nullptr;
static bool s_started = false;
static DeviceState *s_serviceState = nullptr;
static bool s_bootDiagPublished = false;

struct PullOtaJob
{
    bool active = false;
    bool reboot = true;
    bool force = false;

    // Cached pointers not safe; store copies.
    char request_id[48] = {0};
    char version[16] = {0};
    char url[256] = {0};
    char sha256[65] = {0};

    uint32_t lastProgressMs = 0;
    uint32_t lastReportMs = 0;
    uint32_t lastDiagMs = 0;
    uint32_t lastWriteLogMs = 0;
    uint32_t bytesTotal = 0;
    uint32_t bytesWritten = 0;
    uint32_t bytesAtLastWriteLog = 0;
    uint32_t progressLastBytesPrinted = 0;
    int16_t progressLastPctPrinted = -1;
    bool progressStarted = false;
    bool progressCompleted = false;
    uint32_t progressLastPrintMs = 0;
    uint32_t progressLastSampleMs = 0;
    uint32_t progressLastSampleBytes = 0;
    uint32_t progressLastLineLen = 0;
    float progressSpeedEmaBps = 0.0f;
    uint32_t startedMs = 0;
    uint32_t noDataSinceMs = 0;
    uint8_t zeroReadStreak = 0;
    uint8_t netRetryCount = 0;
    uint32_t retryAtMs = 0;
    uint8_t retryCount = 0;
    uint32_t nextRetryAtMs = 0;

    // Streaming object
    HTTPClient http;
    WiFiClientSecure client;

    mbedtls_sha256_context shaCtx;
    bool shaInit = false;

    esp_ota_handle_t otaHandle = 0;
    bool httpBegun = false;
    bool updateBegun = false;
    const esp_partition_t *targetPartition = nullptr;
};

static PullOtaJob g_job;

static const char *s_lastTlsTrustMode = "none";
static int s_lastTlsErrCode = 0;
static char s_lastTlsErrMsg[128] = {0};
#if CFG_OTA_DEV_LOGS
static bool s_otaHeartbeatEnabled = true;
#endif
static TaskHandle_t s_otaTaskHandle = nullptr;
static uint32_t s_blockAttemptId = 1u;
static uint32_t s_lastBlockedAttemptId = 0u;
static char s_lastBlockedPhase[24] = {0};
static char s_lastBlockedReason[48] = {0};
static constexpr uint8_t OTA_DEFERRED_WARN_MAX = 6u;
static constexpr size_t OTA_DEFERRED_WARN_LEN = 128u;
static char s_deferredWarn[OTA_DEFERRED_WARN_MAX][OTA_DEFERRED_WARN_LEN] = {{0}};
static uint8_t s_deferredWarnHead = 0u;
static uint8_t s_deferredWarnCount = 0u;
static bool s_flushingDeferredWarn = false;

static inline bool ota_isInOtaTaskContext()
{
    if (s_otaTaskHandle == nullptr)
    {
        return false;
    }
    return xTaskGetCurrentTaskHandle() == s_otaTaskHandle;
}

void ota_confirmRunningApp()
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        LOG_WARN(LogDomain::OTA, "OTA confirm: running partition is null");
        return;
    }

    // If rollback is enabled in the bootloader config, this is the critical call:
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    LOG_INFO(LogDomain::OTA,
             "OTA confirm: mark_app_valid_cancel_rollback() err=%d running=%s@0x%08lx",
             (int)err,
             running->label,
             (unsigned long)running->address);
}

static bool ota_isStrictUpgrade(const char *currentVersion,
                                const char *targetVersion,
                                int *cmpOut)
{
    if (cmpOut)
    {
        *cmpOut = 0;
    }
    if (!currentVersion || !targetVersion || currentVersion[0] == '\0' || targetVersion[0] == '\0')
    {
        return false;
    }

    int cmp = 0;
    if (!compareVersionStrings(targetVersion, currentVersion, &cmp))
    {
        return false;
    }
    if (cmpOut)
    {
        *cmpOut = cmp;
    }
    return cmp > 0;
}

static inline bool ota_isSystemTimeValid()
{
    return time(nullptr) >= 1600000000;
}

static bool ota_extractUrlHost(const char *url, char *out, size_t outLen)
{
    if (!url || !out || outLen == 0)
    {
        return false;
    }

    out[0] = '\0';
    const char *host = strstr(url, "://");
    host = host ? (host + 3) : url;
    if (!host || host[0] == '\0')
    {
        return false;
    }

    size_t i = 0;
    while (host[i] != '\0' && host[i] != '/' && host[i] != ':' && host[i] != '?' && host[i] != '#')
    {
        if (i + 1 >= outLen)
        {
            return false;
        }
        out[i] = (char)tolower((unsigned char)host[i]);
        ++i;
    }
    out[i] = '\0';
    return i > 0;
}

static bool ota_manifestUrlHostTrusted(const char *url)
{
    char host[128] = {0};
    if (!ota_extractUrlHost(url, host, sizeof(host)))
    {
        return false;
    }
    return (strstr(host, "github.com") != nullptr) ||
           (strstr(host, "release-assets.githubusercontent.com") != nullptr);
}

static bool ota_urlContainsNoCase(const char *url, const char *needle)
{
    if (!url || !needle || needle[0] == '\0')
    {
        return false;
    }
    const size_t needleLen = strlen(needle);
    for (const char *p = url; *p != '\0'; ++p)
    {
        size_t i = 0;
        while (i < needleLen && p[i] != '\0' &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if (i == needleLen)
        {
            return true;
        }
    }
    return false;
}

static void setErr(char *buf, size_t len, const char *msg);
static inline void ota_requestPublish();
static void ota_abort(DeviceState *state, const char *reason);
static bool ota_requireEspOk(DeviceState *state, const char *op, esp_err_t err);
static uint32_t ota_epochNow();
static inline const char *ota_configureTlsClient(WiFiClientSecure &client);
static void ota_tick(DeviceState *state);
static void ota_setResult(DeviceState *state, const char *status, const char *message);
static inline void ota_setStatus(DeviceState *state, OtaStatus status);
static void ota_setFlat(DeviceState *state,
                        const char *stateStr,
                        uint8_t progress,
                        const char *error,
                        const char *targetVersion,
                        bool stamp);
static void ota_progressReset();
static void ota_progressEnsureLineBreak();
static void ota_progressPrint(uint32_t bytesWritten, uint32_t bytesTotal, bool force, bool completed);
static void ota_progressFail(const char *reason);
static void ota_nextBlockAttempt();
static void ota_logBlocked(const char *phase, const char *reason, const char *fmt, ...);
static void ota_logWarnMaybeDeferred(const char *fmt, ...);
static void ota_flushDeferredWarnings();
static const char *ota_sourceFromRequestId(const char *requestId);
static void ota_formatSizeCompact(uint32_t bytes, char *out, size_t outLen);
static void ota_logSuccessBanner(const char *version, uint32_t elapsedMs);
static void ota_logFailBanner(const char *reason, const char *version, uint32_t elapsedMs);
static void ota_clearActive(DeviceState *state);
static void ota_emitCancelledResult(const char *reason);
static const char *ota_imgStateToString(esp_ota_img_states_t state);
static void ota_emitPartitionDiag(const char *phase);

static inline bool ota_timeReached(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

static bool ota_msgContainsNoCase(const char *haystack, const char *needle)
{
    if (!haystack || !needle || needle[0] == '\0')
    {
        return false;
    }
    const size_t needleLen = strlen(needle);
    for (const char *p = haystack; *p != '\0'; ++p)
    {
        size_t i = 0;
        while (i < needleLen && p[i] != '\0' &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i]))
        {
            ++i;
        }
        if (i == needleLen)
        {
            return true;
        }
    }
    return false;
}

static inline void ota_resetTlsError()
{
    s_lastTlsErrCode = 0;
    s_lastTlsErrMsg[0] = '\0';
}

static void ota_trace(const char *step, const char *fmt, ...)
{
#if CFG_OTA_DEV_LOGS
    char detail[256] = {0};
    if (fmt && fmt[0] != '\0')
    {
        va_list args;
        va_start(args, fmt);
        vsnprintf(detail, sizeof(detail), fmt, args);
        va_end(args);
        detail[sizeof(detail) - 1] = '\0';
    }
    LOG_INFO(LogDomain::OTA, "[TRACE] step=%s %s",
             step ? step : "",
             detail[0] ? detail : "");
#else
    (void)step;
    (void)fmt;
#endif
}

static void ota_logRuntimeHealth(const char *step)
{
#if CFG_OTA_DEV_LOGS
    const UBaseType_t hwmWords = uxTaskGetStackHighWaterMark(nullptr);
    const uint32_t hwmBytes = (uint32_t)hwmWords * (uint32_t)sizeof(StackType_t);
    const uint32_t freeHeap = (uint32_t)ESP.getFreeHeap();
    const uint32_t minHeap = (uint32_t)ESP.getMinFreeHeap();
    const uint32_t largest8Bit = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);

    LOG_INFO(LogDomain::OTA,
             "[TRACE] health step=%s stack_hwm_words=%u stack_hwm_bytes=%u heap_free=%lu heap_min=%lu heap_largest_8bit=%lu",
             step ? step : "",
             (unsigned int)hwmWords,
             (unsigned int)hwmBytes,
             (unsigned long)freeHeap,
             (unsigned long)minHeap,
             (unsigned long)largest8Bit);

    if (hwmBytes < 512u)
    {
        LOG_WARN(LogDomain::OTA,
                 "[TRACE] health_low_stack step=%s stack_hwm_bytes=%u threshold=512",
                 step ? step : "",
                 (unsigned int)hwmBytes);
    }
#else
    (void)step;
#endif
}

static void ota_progressReset()
{
    g_job.progressLastBytesPrinted = 0;
    g_job.progressLastPctPrinted = -1;
    g_job.progressStarted = false;
    g_job.progressCompleted = false;
    g_job.progressLastPrintMs = 0;
    g_job.progressLastSampleMs = 0;
    g_job.progressLastSampleBytes = 0;
    g_job.progressLastLineLen = 0;
    g_job.progressSpeedEmaBps = 0.0f;
    g_job.startedMs = 0;
}

static void ota_progressEnsureLineBreak()
{
    logger_serialEnsureLineBreak();
}

static bool ota_shouldDeferWarnings()
{
#if CFG_OTA_DEV_LOGS
    return false;
#else
    if (CFG_OTA_PROGRESS_NEWLINES != 0 || s_flushingDeferredWarn)
    {
        return false;
    }
    return g_job.active && g_job.progressStarted && !g_job.progressCompleted;
#endif
}

static void ota_logWarnMaybeDeferred(const char *fmt, ...)
{
    char msg[OTA_DEFERRED_WARN_LEN] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", args);
    va_end(args);
    msg[sizeof(msg) - 1] = '\0';

    if (!ota_shouldDeferWarnings())
    {
        LOG_WARN(LogDomain::OTA, "%s", msg);
        return;
    }

    uint8_t idx = 0u;
    if (s_deferredWarnCount < OTA_DEFERRED_WARN_MAX)
    {
        idx = (uint8_t)((s_deferredWarnHead + s_deferredWarnCount) % OTA_DEFERRED_WARN_MAX);
        ++s_deferredWarnCount;
    }
    else
    {
        idx = s_deferredWarnHead;
        s_deferredWarnHead = (uint8_t)((s_deferredWarnHead + 1u) % OTA_DEFERRED_WARN_MAX);
    }
    strncpy(s_deferredWarn[idx], msg, OTA_DEFERRED_WARN_LEN);
    s_deferredWarn[idx][OTA_DEFERRED_WARN_LEN - 1] = '\0';
}

static void ota_flushDeferredWarnings()
{
    if (s_deferredWarnCount == 0u)
    {
        return;
    }
    s_flushingDeferredWarn = true;
    for (uint8_t i = 0u; i < s_deferredWarnCount; ++i)
    {
        const uint8_t idx = (uint8_t)((s_deferredWarnHead + i) % OTA_DEFERRED_WARN_MAX);
        if (s_deferredWarn[idx][0] != '\0')
        {
            LOG_WARN(LogDomain::OTA, "%s", s_deferredWarn[idx]);
        }
        s_deferredWarn[idx][0] = '\0';
    }
    s_deferredWarnHead = 0u;
    s_deferredWarnCount = 0u;
    s_flushingDeferredWarn = false;
}

static void ota_progressPrint(uint32_t bytesWritten, uint32_t bytesTotal, bool force, bool completed)
{
    constexpr uint32_t kMinUpdateIntervalMs = 125u; // cap at ~8 Hz
    const bool newlineMode = (CFG_OTA_DEV_LOGS != 0) || (CFG_OTA_PROGRESS_NEWLINES != 0);
    const bool hasTotal = bytesTotal > 0u;
    const uint32_t nowMs = millis();
    const uint32_t clampedBytes = (hasTotal && bytesWritten > bytesTotal) ? bytesTotal : bytesWritten;
    uint32_t pct = 0u;
    if (hasTotal)
    {
        pct = (clampedBytes * 100u) / bytesTotal;
        if (pct > 100u)
        {
            pct = 100u;
        }
    }
    if (completed && hasTotal)
    {
        pct = 100u;
    }

    bool shouldPrint = force || !g_job.progressStarted;
    if (!shouldPrint)
    {
        bool pctAdvanced = false;
        if (hasTotal && g_job.progressLastPctPrinted >= 0)
        {
            const uint32_t pctStep = (uint32_t)CFG_OTA_PROGRESS_PCT_STEP;
            const uint32_t lastPct = (uint32_t)g_job.progressLastPctPrinted;
            pctAdvanced = pct >= (lastPct + pctStep);
        }
        const bool bytesAdvanced = clampedBytes >= (g_job.progressLastBytesPrinted + (uint32_t)CFG_OTA_PROGRESS_BYTES_STEP);
        shouldPrint = pctAdvanced || bytesAdvanced;
    }
    if (!force && shouldPrint && g_job.progressLastPrintMs > 0 &&
        (uint32_t)(nowMs - g_job.progressLastPrintMs) < kMinUpdateIntervalMs)
    {
        shouldPrint = false;
    }

    if (!shouldPrint)
    {
        return;
    }
    if (g_job.startedMs == 0u)
    {
        g_job.startedMs = nowMs;
    }

    if (g_job.progressLastSampleMs > 0 && nowMs > g_job.progressLastSampleMs &&
        clampedBytes >= g_job.progressLastSampleBytes)
    {
        const uint32_t deltaMs = nowMs - g_job.progressLastSampleMs;
        const uint32_t deltaBytes = clampedBytes - g_job.progressLastSampleBytes;
        if (deltaMs >= 50u && deltaBytes > 0u)
        {
            const float instBps = ((float)deltaBytes * 1000.0f) / (float)deltaMs;
            const float alpha = 0.25f;
            if (g_job.progressSpeedEmaBps <= 0.0f)
            {
                g_job.progressSpeedEmaBps = instBps;
            }
            else
            {
                g_job.progressSpeedEmaBps = (g_job.progressSpeedEmaBps * (1.0f - alpha)) + (instBps * alpha);
            }
        }
    }
    g_job.progressLastSampleMs = nowMs;
    g_job.progressLastSampleBytes = clampedBytes;

    char speedBuf[20] = {0};
    if (g_job.progressSpeedEmaBps >= (1024.0f * 1024.0f))
    {
        snprintf(speedBuf, sizeof(speedBuf), "%5.1f MB/s", g_job.progressSpeedEmaBps / (1024.0f * 1024.0f));
    }
    else if (g_job.progressSpeedEmaBps > 0.0f)
    {
        snprintf(speedBuf, sizeof(speedBuf), "%5.1f kB/s", g_job.progressSpeedEmaBps / 1024.0f);
    }
    else
    {
        snprintf(speedBuf, sizeof(speedBuf), "--.- kB/s");
    }

    char etaBuf[8] = "--:--";
    const uint32_t elapsedMs = (g_job.startedMs > 0 && nowMs >= g_job.startedMs) ? (nowMs - g_job.startedMs) : 0u;
    if (hasTotal &&
        g_job.progressSpeedEmaBps > 0.1f &&
        clampedBytes < bytesTotal &&
        elapsedMs >= 1500u &&
        clampedBytes >= 32768u)
    {
        const float speedForEta = (g_job.progressSpeedEmaBps < 128.0f) ? 128.0f : g_job.progressSpeedEmaBps;
        const float remain = (float)(bytesTotal - clampedBytes);
        uint32_t etaSec = (uint32_t)(remain / speedForEta);
        if (etaSec > 5999u)
        {
            etaSec = 5999u;
        }
        snprintf(etaBuf, sizeof(etaBuf), "%02lu:%02lu",
                 (unsigned long)(etaSec / 60u),
                 (unsigned long)(etaSec % 60u));
    }
    else if (completed)
    {
        snprintf(etaBuf, sizeof(etaBuf), "00:00");
    }

    constexpr size_t kBarWidth = 25u;
    char lineRaw[192] = {0};
    char line[192] = {0};
    char bytesField[24] = {0};
    char pctField[5] = " --%";
    if (hasTotal || completed)
    {
        snprintf(pctField, sizeof(pctField), "%3lu%%", (unsigned long)pct);
    }
    if (hasTotal)
    {
        char bar[kBarWidth + 1] = {0};
        const size_t fill = (size_t)((pct * kBarWidth) / 100u);
        for (size_t i = 0; i < kBarWidth; ++i)
        {
            bar[i] = (i < fill) ? '=' : ' ';
        }
        bar[kBarWidth] = '\0';
        float writtenMb = (float)clampedBytes / (1024.0f * 1024.0f);
        float totalMb = (float)bytesTotal / (1024.0f * 1024.0f);
        if (writtenMb > 9999.99f)
        {
            writtenMb = 9999.99f;
        }
        if (totalMb > 9999.99f)
        {
            totalMb = 9999.99f;
        }
        snprintf(bytesField, sizeof(bytesField), "%6.2f/%6.2f MB", writtenMb, totalMb);
        snprintf(lineRaw, sizeof(lineRaw),
                 "Download        [%-25s] %4s %-16s %11s ETA %5s",
                 bar,
                 pctField,
                 bytesField,
                 speedBuf,
                 etaBuf);
    }
    else
    {
        float writtenKb = (float)clampedBytes / 1024.0f;
        if (writtenKb > 999999.9f)
        {
            writtenKb = 999999.9f;
        }
        snprintf(bytesField, sizeof(bytesField), "%12.1f KB", writtenKb);
        snprintf(lineRaw, sizeof(lineRaw),
                 "Download        [%-25s] %4s %-16s %11s ETA %5s",
                 "size unknown",
                 pctField,
                 bytesField,
                 speedBuf,
                 etaBuf);
    }
    lineRaw[sizeof(lineRaw) - 1] = '\0';
    strncpy(line, lineRaw, sizeof(line));
    line[sizeof(line) - 1] = '\0';

    logger_serialLock();
    if (!newlineMode && !g_job.progressStarted)
    {
        Serial.println();
        logger_serialSetInlineActive(false);
    }
    if (newlineMode)
    {
        Serial.println(line);
        logger_serialSetInlineActive(false);
        g_job.progressLastLineLen = 0;
    }
    else
    {
        Serial.print('\r');
        Serial.print(line);
        Serial.print("\033[K");
        g_job.progressLastLineLen = (uint32_t)strlen(line);
        if (completed)
        {
            Serial.println();
            logger_serialSetInlineActive(false);
        }
        else
        {
            logger_serialSetInlineActive(true);
        }
    }

    g_job.progressStarted = true;
    g_job.progressLastPrintMs = nowMs;
    g_job.progressLastBytesPrinted = clampedBytes;
    g_job.progressLastPctPrinted = hasTotal ? (int16_t)pct : -1;

    if (completed && !g_job.progressCompleted)
    {
        Serial.println("Download done.");
        if (!newlineMode)
        {
            Serial.println();
        }
        g_job.progressCompleted = true;
        g_job.progressLastLineLen = 0u;
    }
    logger_serialUnlock();
    if (completed)
    {
        ota_progressEnsureLineBreak();
        ota_flushDeferredWarnings();
    }
}

static void ota_progressFail(const char *reason)
{
    const bool newlineMode = (CFG_OTA_DEV_LOGS != 0) || (CFG_OTA_PROGRESS_NEWLINES != 0);
    const char *msg = (reason && reason[0] != '\0') ? reason : "error";
    logger_serialEnsureLineBreak();
    logger_serialLock();
    Serial.print("Download failed: ");
    Serial.println(msg);
    if (!newlineMode)
    {
        Serial.println();
    }
    logger_serialSetInlineActive(false);
    g_job.progressLastLineLen = 0u;
    logger_serialUnlock();
    g_job.progressCompleted = true;
    ota_progressEnsureLineBreak();
    ota_flushDeferredWarnings();
}

static void ota_nextBlockAttempt()
{
    ++s_blockAttemptId;
    if (s_blockAttemptId == 0u)
    {
        s_blockAttemptId = 1u;
    }
}

static void ota_logBlocked(const char *phase, const char *reason, const char *fmt, ...)
{
    const char *safePhase = phase ? phase : "";
    const char *safeReason = reason ? reason : "";
    if (s_lastBlockedAttemptId == s_blockAttemptId &&
        strncmp(s_lastBlockedPhase, safePhase, sizeof(s_lastBlockedPhase)) == 0 &&
        strncmp(s_lastBlockedReason, safeReason, sizeof(s_lastBlockedReason)) == 0)
    {
        return;
    }

    s_lastBlockedAttemptId = s_blockAttemptId;
    strncpy(s_lastBlockedPhase, safePhase, sizeof(s_lastBlockedPhase));
    s_lastBlockedPhase[sizeof(s_lastBlockedPhase) - 1] = '\0';
    strncpy(s_lastBlockedReason, safeReason, sizeof(s_lastBlockedReason));
    s_lastBlockedReason[sizeof(s_lastBlockedReason) - 1] = '\0';

    char msg[192] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    msg[sizeof(msg) - 1] = '\0';
    ota_logWarnMaybeDeferred("%s", msg);
}

static const char *ota_sourceFromRequestId(const char *requestId)
{
    if (!requestId || requestId[0] == '\0')
    {
        return "unknown";
    }
    if (strstr(requestId, "serial") != nullptr)
    {
        return "serial";
    }
    if (strstr(requestId, "manifest") != nullptr)
    {
        return "manifest";
    }
    return "request";
}

static void ota_formatSizeCompact(uint32_t bytes, char *out, size_t outLen)
{
    if (!out || outLen == 0)
    {
        return;
    }
    if (bytes >= (1024u * 1024u))
    {
        const float mb = (float)bytes / (1024.0f * 1024.0f);
        snprintf(out, outLen, "%.2f MB", mb);
    }
    else if (bytes >= 1024u)
    {
        const float kb = (float)bytes / 1024.0f;
        snprintf(out, outLen, "%.1f kB", kb);
    }
    else
    {
        snprintf(out, outLen, "%lu B", (unsigned long)bytes);
    }
    out[outLen - 1] = '\0';
}

static void ota_logSuccessBanner(const char *version, uint32_t elapsedMs)
{
    const uint32_t seconds = elapsedMs / 1000u;
    const uint32_t mins = seconds / 60u;
    const uint32_t secs = seconds % 60u;
    const char *v = (version && version[0] != '\0') ? version : "<unknown>";
    ota_progressEnsureLineBreak();
    logger_serialLock();
#if CFG_OTA_ANSI_COLORS
    Serial.print("=====[ OTA UPDATE ");
    Serial.print("\x1B[1m\x1B[32mSUCCESS\x1B[0m");
    Serial.print(" ]===== ");
    Serial.print("\x1B[36m");
    Serial.print(v);
    Serial.print("\x1B[0m");
#else
    Serial.print("=====[ OTA UPDATE SUCCESS ]===== ");
    Serial.print(v);
#endif
    Serial.print(" in ");
    char dur[8];
    snprintf(dur, sizeof(dur), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);
    Serial.println(dur);
    logger_serialSetInlineActive(false);
    logger_serialUnlock();
}

static void ota_logFailBanner(const char *reason, const char *version, uint32_t elapsedMs)
{
    const uint32_t seconds = elapsedMs / 1000u;
    const uint32_t mins = seconds / 60u;
    const uint32_t secs = seconds % 60u;
    const char *r = (reason && reason[0] != '\0') ? reason : "error";
    (void)version;
    ota_progressEnsureLineBreak();
    logger_serialLock();
#if CFG_OTA_ANSI_COLORS
    Serial.print("=====[ OTA UPDATE ");
    Serial.print("\x1B[1m\x1B[31mFAILED\x1B[0m");
    Serial.print(" ]====== ");
    Serial.print(r);
    Serial.print(" after ");
#else
    Serial.print("=====[ OTA UPDATE FAILED ]====== ");
    Serial.print(r);
    Serial.print(" after ");
#endif
    char dur[8];
    snprintf(dur, sizeof(dur), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);
    Serial.println(dur);
    logger_serialSetInlineActive(false);
    logger_serialUnlock();
}

static void ota_logPartitionSnapshot(const char *phase)
{
#if CFG_OTA_DEV_LOGS
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);

    LOG_INFO(LogDomain::OTA,
             "OTA partition snapshot phase=%s running=%s@0x%08lx boot=%s@0x%08lx next=%s@0x%08lx",
             phase ? phase : "",
             (running && running->label) ? running->label : "<null>",
             (unsigned long)(running ? running->address : 0u),
             (boot && boot->label) ? boot->label : "<null>",
             (unsigned long)(boot ? boot->address : 0u),
             (next && next->label) ? next->label : "<null>",
             (unsigned long)(next ? next->address : 0u));
#else
    (void)phase;
#endif
}

static const char *ota_imgStateToString(esp_ota_img_states_t state)
{
    switch (state)
    {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
        return "undefined";
    }
}

static const char *ota_resetReasonToString(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "power_on";
    case ESP_RST_EXT:
        return "ext_reset";
    case ESP_RST_SW:
        return "software_reset";
#ifdef ESP_RST_PANIC
    case ESP_RST_PANIC:
        return "panic";
#endif
    case ESP_RST_INT_WDT:
        return "int_wdt";
#ifdef ESP_RST_TASK_WDT
    case ESP_RST_TASK_WDT:
        return "task_wdt";
#endif
    case ESP_RST_WDT:
        return "wdt";
    case ESP_RST_DEEPSLEEP:
        return "deep_sleep";
#ifdef ESP_RST_BROWNOUT
    case ESP_RST_BROWNOUT:
        return "brownout";
#endif
#ifdef ESP_RST_SDIO
    case ESP_RST_SDIO:
        return "sdio";
#endif
    default:
        return "other";
    }
}

static void ota_emitPartitionDiag(const char *phase)
{
#if !CFG_OTA_DEV_LOGS
    (void)phase;
    return;
#else
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    const esp_reset_reason_t resetReason = esp_reset_reason();

    char runningStateBuf[24] = "n/a";
    int runningStateErr = -1;
    if (running)
    {
        esp_ota_img_states_t runningState = ESP_OTA_IMG_UNDEFINED;
        const esp_err_t err = esp_ota_get_state_partition(running, &runningState);
        runningStateErr = (int)err;
        if (err == ESP_OK)
        {
            strncpy(runningStateBuf, ota_imgStateToString(runningState), sizeof(runningStateBuf));
            runningStateBuf[sizeof(runningStateBuf) - 1] = '\0';
        }
        else
        {
            snprintf(runningStateBuf, sizeof(runningStateBuf), "err_%d", (int)err);
        }
    }

    char payload[512];
    const int n = snprintf(
        payload,
        sizeof(payload),
        "phase=%s running=%s@0x%08lx boot=%s@0x%08lx next=%s@0x%08lx run_state=%s run_state_err=%d reset_reason=%s(%d) fw=%s build=%s %s",
        phase ? phase : "",
        (running && running->label) ? running->label : "<null>",
        (unsigned long)(running ? running->address : 0u),
        (boot && boot->label) ? boot->label : "<null>",
        (unsigned long)(boot ? boot->address : 0u),
        (next && next->label) ? next->label : "<null>",
        (unsigned long)(next ? next->address : 0u),
        runningStateBuf,
        runningStateErr,
        ota_resetReasonToString(resetReason),
        (int)resetReason,
        FW_VERSION,
        __DATE__,
        __TIME__);
    if (n < 0)
    {
        return;
    }
    payload[sizeof(payload) - 1] = '\0';

    LOG_INFO(LogDomain::OTA, "OTA diag %s", payload);
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushDiag(payload);
    }
    else
    {
        (void)mqtt_publishLog("ota/diag", payload, false);
    }
#endif
}

static bool ota_detachCurrentTaskWdt(const char *phase)
{
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    const esp_err_t err = esp_task_wdt_delete(task);
    if (err == ESP_OK)
    {
#if CFG_OTA_DEV_LOGS
        LOG_DEBUG(LogDomain::OTA, "WDT detached for phase=%s", phase ? phase : "");
#endif
        return true;
    }

    const bool benign = (err == ESP_ERR_INVALID_STATE) ||
                        (err == ESP_ERR_NOT_FOUND) ||
                        (err == ESP_ERR_INVALID_ARG);
    if (benign)
    {
#if CFG_OTA_DEV_LOGS
        LOG_DEBUG(LogDomain::OTA, "WDT detach benign skip phase=%s err=%d name=%s",
                  phase ? phase : "",
                  (int)err,
                  esp_err_to_name(err));
#endif
        return false;
    }

    ota_logWarnMaybeDeferred("WDT detach skipped phase=%s err=%d name=%s",
                             phase ? phase : "",
                             (int)err,
                             esp_err_to_name(err));
    return false;
}

static void ota_reattachCurrentTaskWdt(bool detached, const char *phase)
{
    if (!detached)
    {
        return;
    }
    TaskHandle_t task = xTaskGetCurrentTaskHandle();
    const esp_err_t err = esp_task_wdt_add(task);
    if (err == ESP_OK)
    {
#if CFG_OTA_DEV_LOGS
        LOG_DEBUG(LogDomain::OTA, "WDT reattached for phase=%s", phase ? phase : "");
#endif
        return;
    }
    const bool benign = (err == ESP_ERR_INVALID_STATE) ||
                        (err == ESP_ERR_NOT_FOUND) ||
                        (err == ESP_ERR_INVALID_ARG);
    if (benign)
    {
#if CFG_OTA_DEV_LOGS
        LOG_DEBUG(LogDomain::OTA, "WDT reattach benign skip phase=%s err=%d name=%s",
                  phase ? phase : "",
                  (int)err,
                  esp_err_to_name(err));
#endif
        return;
    }
    LOG_ERROR(LogDomain::OTA, "WDT reattach failed phase=%s err=%d name=%s",
              phase ? phase : "",
              (int)err,
              esp_err_to_name(err));
}

static inline void ota_captureTlsError(WiFiClientSecure &client)
{
    ota_resetTlsError();
    char msg[sizeof(s_lastTlsErrMsg)] = {0};
    s_lastTlsErrCode = client.lastError(msg, sizeof(msg));
    strncpy(s_lastTlsErrMsg, msg, sizeof(s_lastTlsErrMsg));
    s_lastTlsErrMsg[sizeof(s_lastTlsErrMsg) - 1] = '\0';
}

static bool ota_tlsCertVerifyFailed()
{
    bool certVerifyFailed = false;
#if defined(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED)
    certVerifyFailed = certVerifyFailed || (s_lastTlsErrCode == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
#endif
#if defined(MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED)
    certVerifyFailed = certVerifyFailed || (s_lastTlsErrCode == MBEDTLS_ERR_SSL_PEER_VERIFY_FAILED);
#endif
    if (!certVerifyFailed && s_lastTlsErrMsg[0] != '\0')
    {
        certVerifyFailed = (strstr(s_lastTlsErrMsg, "verify") != nullptr) ||
                           (strstr(s_lastTlsErrMsg, "certificate") != nullptr) ||
                           (strstr(s_lastTlsErrMsg, "x509") != nullptr);
    }
    return certVerifyFailed;
}

static const char *ota_tlsFailureReason(int httpCode)
{
    if (!ota_isSystemTimeValid())
    {
        return "time_not_set";
    }
    if (ota_tlsCertVerifyFailed())
    {
        return "cert_verify_failed";
    }
    if (httpCode == 0)
    {
        return "http_begin_failed";
    }
    if (httpCode < 0)
    {
        return "http_request_failed";
    }
    return "http_error";
}

static bool ota_tlsLikeFailure()
{
    if (ota_tlsCertVerifyFailed())
    {
        return true;
    }
    if (s_lastTlsErrMsg[0] == '\0')
    {
        return false;
    }
    return ota_msgContainsNoCase(s_lastTlsErrMsg, "tls") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "ssl") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "x509") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "certificate") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "handshake") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "verify");
}

static bool ota_dnsLikeFailure()
{
    if (s_lastTlsErrMsg[0] == '\0')
    {
        return false;
    }
    return ota_msgContainsNoCase(s_lastTlsErrMsg, "dns") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "getaddrinfo") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "name not known") ||
           ota_msgContainsNoCase(s_lastTlsErrMsg, "resolve");
}

static bool ota_httpTimeoutCode(int code)
{
#if defined(HTTPC_ERROR_READ_TIMEOUT)
    if (code == HTTPC_ERROR_READ_TIMEOUT)
    {
        return true;
    }
#endif
#if defined(HTTPC_ERROR_CONNECTION_TIMEOUT)
    if (code == HTTPC_ERROR_CONNECTION_TIMEOUT)
    {
        return true;
    }
#endif
    return false;
}

static const char *ota_classifyBeginFailure(uint32_t elapsedMs)
{
    if (elapsedMs >= (uint32_t)CFG_OTA_HTTP_CONNECT_TIMEOUT_MS)
    {
        return "http_timeout";
    }
    if (ota_dnsLikeFailure())
    {
        return "dns_fail";
    }
    if (ota_tlsLikeFailure())
    {
        return "tls_fail";
    }
    return "tls_fail";
}

static const char *ota_classifyRequestFailure(int httpCode)
{
    if (ota_httpTimeoutCode(httpCode))
    {
        return "http_timeout";
    }
    if (ota_dnsLikeFailure())
    {
        return "dns_fail";
    }
    if (ota_tlsLikeFailure())
    {
        return "tls_fail";
    }
    if (httpCode < 0)
    {
        return "tls_fail";
    }
    return "tls_fail";
}

static void ota_formatHttpCodeReason(int httpCode, char *buf, size_t len)
{
    if (!buf || len == 0)
    {
        return;
    }
    snprintf(buf, len, "http_code_%d", httpCode);
    buf[len - 1] = '\0';
}

static inline void ota_recordError(DeviceState *state, const char *reason)
{
    const char *msg = (reason && reason[0] != '\0') ? reason : "error";
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushError(msg);
        return;
    }
    if (!state)
        return;
    strncpy(state->ota_error, msg, sizeof(state->ota_error));
    state->ota_error[sizeof(state->ota_error) - 1] = '\0';
    strncpy(state->ota.last_status, "error", sizeof(state->ota.last_status));
    state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';
    strncpy(state->ota.last_message, msg, sizeof(state->ota.last_message));
    state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';
}

static bool ota_checkSafetyGuards(DeviceState *state,
                                  const char *phase,
                                  char *errBuf,
                                  size_t errBufLen)
{
    if (!state)
    {
        return true;
    }

#if (CFG_OTA_GUARD_REQUIRE_MQTT_CONNECTED)
    if (!state->mqtt.connected)
    {
        const char *reason = "mqtt_disconnected";
        ota_logBlocked("guard", reason, "OTA guard reject phase=%s reason=%s", phase ? phase : "", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
#endif

#if (CFG_OTA_GUARD_MIN_WIFI_RSSI > -127)
    if (state->wifi.rssi < (int)CFG_OTA_GUARD_MIN_WIFI_RSSI)
    {
        const char *reason = "wifi_rssi_low";
        ota_logBlocked("guard", reason, "OTA guard reject phase=%s reason=%s rssi=%d threshold=%d",
                       phase ? phase : "",
                       reason,
                       state->wifi.rssi,
                       (int)CFG_OTA_GUARD_MIN_WIFI_RSSI);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
#endif

    return true;
}

static void ota_logTlsStatus(const char *phase, const char *endpoint, bool success, int httpCode)
{
#if !CFG_OTA_DEV_LOGS
    (void)phase;
    (void)endpoint;
    (void)success;
    (void)httpCode;
    return;
#else
    const char *phaseStr = phase ? phase : "";
    const char *endpointStr = (endpoint && endpoint[0] != '\0') ? endpoint : "<none>";
    char endpointHost[128] = {0};
    const bool hostOk = ota_extractUrlHost(endpointStr, endpointHost, sizeof(endpointHost));
    const char *trustMode = s_lastTlsTrustMode ? s_lastTlsTrustMode : "none";
    const bool timeValid = ota_isSystemTimeValid();
    LOG_INFO(LogDomain::OTA,
             "TLS status phase=%s trust=%s request_ok=%s http_code=%d time_valid=%s endpoint=%s host=%s",
             phaseStr,
             trustMode,
             success ? "true" : "false",
             httpCode,
             timeValid ? "true" : "false",
             endpointStr,
             hostOk ? endpointHost : "<unparsed>");

    if (success)
    {
        return;
    }

    if (!ota_isSystemTimeValid())
    {
        LOG_ERROR(LogDomain::OTA, "TLS handshake failed: time not set endpoint=%s host=%s", endpointStr, hostOk ? endpointHost : "<unparsed>");
    }
    else
    {
        if (ota_tlsCertVerifyFailed())
        {
            LOG_ERROR(LogDomain::OTA, "TLS handshake failed: cert verify failed endpoint=%s host=%s", endpointStr, hostOk ? endpointHost : "<unparsed>");
        }
        else
        {
            if (httpCode == 0)
            {
                LOG_ERROR(LogDomain::OTA, "HTTP begin failed endpoint=%s host=%s", endpointStr, hostOk ? endpointHost : "<unparsed>");
            }
            else
            {
                LOG_ERROR(LogDomain::OTA, "HTTP request failed endpoint=%s host=%s", endpointStr, hostOk ? endpointHost : "<unparsed>");
            }
        }
    }

    if (s_lastTlsErrCode != 0 || s_lastTlsErrMsg[0] != '\0')
    {
        LOG_ERROR(LogDomain::OTA, "TLS error detail phase=%s endpoint=%s host=%s code=%d msg=%s",
                  phaseStr,
                  endpointStr,
                  hostOk ? endpointHost : "<unparsed>",
                  s_lastTlsErrCode,
                  s_lastTlsErrMsg[0] ? s_lastTlsErrMsg : "<none>");
    }
#endif
}

static inline void ota_prepareTlsClient(WiFiClientSecure &client, const char *phase, const char *endpoint)
{
    const char *trustMode = ota_configureTlsClient(client);
    char endpointHost[128] = {0};
    const bool hostOk = ota_extractUrlHost(endpoint, endpointHost, sizeof(endpointHost));

    s_lastTlsTrustMode = (trustMode && trustMode[0] != '\0') ? trustMode : "unknown";
    ota_resetTlsError();

#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "TLS trust=%s phase=%s endpoint=%s host=%s",
             s_lastTlsTrustMode,
             phase ? phase : "",
             (endpoint && endpoint[0] != '\0') ? endpoint : "<none>",
             hostOk ? endpointHost : "<unparsed>");
#else
    (void)phase;
    (void)hostOk;
    (void)endpointHost;
#endif
}

bool ota_isBusy()
{
    return g_job.active || ota_taskHasPendingWork();
}

void ota_begin(DeviceState *state, const char *hostName, const char *password)
{
    s_serviceState = state;
    s_hostName = hostName;
    s_password = password;
    s_started = false;
    s_bootDiagPublished = false;
    if (!ota_taskBegin(state))
    {
        LOG_ERROR(LogDomain::OTA, "Failed to start otaTask worker");
    }
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA, "OTA manifest url configured: %s", CFG_OTA_MANIFEST_URL);
#endif
}

void ota_handle()
{
    if (!s_bootDiagPublished && mqtt_isConnected())
    {
        ota_emitPartitionDiag("boot_post_mqtt");
        s_bootDiagPublished = true;
    }

    if (ota_isBusy())
    {
        return; // avoid concurrent flash access during pull-OTA
    }

    if (!s_started)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return;
        }

        if (s_hostName)
        {
            ArduinoOTA.setHostname(s_hostName);
        }
        if (s_password && s_password[0] != '\0')
        {
            ArduinoOTA.setPassword(s_password);
        }

        ArduinoOTA.onStart([]()
                           {
#if CFG_LOG_DEV
                               LOG_DEBUG(LogDomain::OTA, "Update started");
#endif
                           });

        ArduinoOTA.onEnd([]()
                         {
#if CFG_LOG_DEV
                             LOG_DEBUG(LogDomain::OTA, "Update finished");
#endif
                         });

        ArduinoOTA.onError([](ota_error_t error)
                           { LOG_WARN(LogDomain::OTA, "Error %u", (unsigned int)error); });

        ArduinoOTA.begin();
        s_started = true;

        IPAddress ip = WiFi.localIP();
        LOG_INFO(LogDomain::OTA, "started on %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        return;
    }

    ArduinoOTA.handle();
}

bool ota_cancel(const char *reason)
{
    const char *cancelReason = (reason && reason[0] != '\0') ? reason : "cancelled";
    const bool hadWork = ota_taskCancelAll(cancelReason);

    if (hadWork)
    {
        // Clear active identity fields through the OTA event bridge so state mutation
        // stays serialized on main-loop drain.
        ota_events_pushClearActive();
        ota_emitCancelledResult(cancelReason);
    }
    return hadWork;
}

static void ota_emitCancelledResult(const char *reason)
{
    const char *msg = (reason && reason[0] != '\0') ? reason : "cancelled";
    uint8_t progress = 0;
    if (s_serviceState)
    {
        progress = s_serviceState->ota_progress;
    }

    ota_events_pushStatus(OtaStatus::IDLE);
    ota_events_pushFlatState("cancelled", progress, msg, nullptr, true);
    ota_events_pushResult("cancelled", msg, ota_epochNow());
    ota_events_requestPublish();
}

static void setErr(char *buf, size_t len, const char *msg)
{
    if (!buf || len == 0)
        return;
    strncpy(buf, msg ? msg : "", len);
    buf[len - 1] = '\0';
}

static bool isHex64(const char *s)
{
    if (!s)
        return false;
    for (int i = 0; i < 64; i++)
    {
        char c = s[i];
        if (c == '\0')
            return false;
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return false;
    }
    return s[64] == '\0';
}

static inline char lowerHexChar(char c)
{
    if (c >= 'A' && c <= 'F')
        return (char)(c - 'A' + 'a');
    return c;
}

static void ota_setResult(DeviceState *state, const char *status, const char *message)
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushResult(status ? status : "", message ? message : "", ota_epochNow());
        return;
    }
    if (!state)
        return;
    strncpy(state->ota.last_status, status ? status : "", sizeof(state->ota.last_status));
    state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';

    strncpy(state->ota.last_message, message ? message : "", sizeof(state->ota.last_message));
    state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';

    const uint32_t epochNow = ota_epochNow();
    state->ota.completed_ts = epochNow;
}

static inline void ota_requestPublish()
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_requestPublish();
        return;
    }
    mqtt_requestStatePublish();
}

static inline void ota_setStatus(DeviceState *state, OtaStatus status)
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushStatus(status);
        return;
    }
    if (!state)
        return;
    state->ota.status = status;
}

static inline void ota_setProgress(DeviceState *state, uint8_t progress)
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushProgress(progress);
        return;
    }
    if (!state)
        return;
    state->ota.progress = progress;
    state->ota_progress = progress;
}

static void ota_clearActive(DeviceState *state)
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushClearActive();
        return;
    }
    if (!state)
        return;
    state->ota.request_id[0] = '\0';
    state->ota.version[0] = '\0';
    state->ota.url[0] = '\0';
    state->ota.sha256[0] = '\0';
    state->ota.started_ts = 0;
}

static uint32_t ota_epochNow()
{
    const time_t now = time(nullptr);
    if (now < 1600000000)
    {
        return 0;
    }
    return (uint32_t)now;
}

static void ota_setFlat(DeviceState *state,
                        const char *stateStr,
                        uint8_t progress,
                        const char *error,
                        const char *targetVersion,
                        bool stamp)
{
    if (ota_isInOtaTaskContext())
    {
        ota_events_pushFlatState(stateStr, progress, error, targetVersion, stamp);
        return;
    }
    if (!state)
        return;
    if (stateStr)
    {
        strncpy(state->ota_state, stateStr, sizeof(state->ota_state));
        state->ota_state[sizeof(state->ota_state) - 1] = '\0';
    }
    ota_setProgress(state, progress);
    if (error)
    {
        strncpy(state->ota_error, error, sizeof(state->ota_error));
        state->ota_error[sizeof(state->ota_error) - 1] = '\0';
    }
    if (targetVersion)
    {
        strncpy(state->ota_target_version, targetVersion, sizeof(state->ota_target_version));
        state->ota_target_version[sizeof(state->ota_target_version) - 1] = '\0';
    }
    if (stamp)
    {
        const uint32_t epochNow = ota_epochNow();
        if (epochNow > 0)
        {
            state->ota_last_ts = epochNow;
        }
    }
}

static bool ota_scheduleRetry(DeviceState *state, const char *reason)
{
    const char *msg = (reason && reason[0] != '\0') ? reason : "retry";
    ota_progressEnsureLineBreak();
    ota_trace("schedule_retry", "reason=%s retry_count=%u/%u",
              msg,
              (unsigned int)g_job.netRetryCount,
              (unsigned int)CFG_OTA_HTTP_MAX_RETRIES);

    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }

    if (g_job.netRetryCount >= (uint8_t)CFG_OTA_HTTP_MAX_RETRIES)
    {
        ota_abort(state, msg);
        return false;
    }

    g_job.netRetryCount++;
    uint32_t backoffMs = (uint32_t)CFG_OTA_HTTP_RETRY_BASE_MS * (uint32_t)g_job.netRetryCount;
    if (backoffMs > (uint32_t)CFG_OTA_HTTP_RETRY_MAX_BACKOFF_MS)
    {
        backoffMs = (uint32_t)CFG_OTA_HTTP_RETRY_MAX_BACKOFF_MS;
    }
    g_job.retryAtMs = millis() + backoffMs;
    ota_progressReset();

    ota_logWarnMaybeDeferred("OTA network retry scheduled reason=%s attempt=%u/%u backoff_ms=%lu",
                             msg,
                             (unsigned int)g_job.netRetryCount,
                             (unsigned int)CFG_OTA_HTTP_MAX_RETRIES,
                             (unsigned long)backoffMs);

    if (state)
    {
        ota_recordError(state, msg);
        ota_setFlat(state, "downloading", state->ota.progress, msg, state->ota.version, true);
        ota_requestPublish();
    }

    return true;
}

static void ota_markFailed(DeviceState *state, const char *reason)
{
    if (!state)
        return;
    ota_setStatus(state, OtaStatus::ERROR);
    ota_setResult(state, "error", reason ? reason : "error");
    ota_setFlat(state, "failed", 0, reason ? reason : "error", state->ota.version, true);
    ota_clearActive(state);
    ota_requestPublish();
}

static void ota_releaseJobResources()
{
    if (g_job.updateBegun)
    {
        (void)esp_ota_abort(g_job.otaHandle);
        g_job.otaHandle = 0;
        g_job.updateBegun = false;
    }
    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }
    g_job.client.stop();
    if (g_job.shaInit)
    {
        mbedtls_sha256_free(&g_job.shaCtx);
        g_job.shaInit = false;
    }
}

static void ota_resetRuntimeJob()
{
    ota_releaseJobResources();

    g_job.active = false;
    g_job.reboot = true;
    g_job.force = false;
    g_job.lastProgressMs = 0;
    g_job.lastReportMs = 0;
    g_job.lastDiagMs = 0;
    g_job.lastWriteLogMs = 0;
    g_job.bytesTotal = 0;
    g_job.bytesWritten = 0;
    g_job.bytesAtLastWriteLog = 0;
    ota_progressReset();
    g_job.noDataSinceMs = 0;
    g_job.zeroReadStreak = 0;
    g_job.netRetryCount = 0;
    g_job.retryAtMs = 0;
    g_job.retryCount = 0;
    g_job.nextRetryAtMs = 0;
    g_job.otaHandle = 0;
    g_job.targetPartition = nullptr;
    g_job.request_id[0] = '\0';
    g_job.version[0] = '\0';
    g_job.url[0] = '\0';
    g_job.sha256[0] = '\0';
}

static void ota_primeRuntimeJob(const OtaTaskJob &job)
{
    ota_resetRuntimeJob();
    ota_nextBlockAttempt();

    g_job.active = true;
    g_job.reboot = job.reboot;
    g_job.force = job.force;

    strncpy(g_job.request_id, job.request_id, sizeof(g_job.request_id));
    g_job.request_id[sizeof(g_job.request_id) - 1] = '\0';
    strncpy(g_job.version, job.version, sizeof(g_job.version));
    g_job.version[sizeof(g_job.version) - 1] = '\0';
    strncpy(g_job.url, job.url, sizeof(g_job.url));
    g_job.url[sizeof(g_job.url) - 1] = '\0';
    strncpy(g_job.sha256, job.sha256, sizeof(g_job.sha256));
    g_job.sha256[sizeof(g_job.sha256) - 1] = '\0';
}

bool ota_pullStart(DeviceState *state,
                   const char *request_id,
                   const char *version,
                   const char *url,
                   const char *sha256,
                   bool force,
                   bool reboot,
                   char *errBuf,
                   size_t errBufLen)
{
    ota_nextBlockAttempt();

    if (!state)
    {
        setErr(errBuf, errBufLen, "missing_state");
        return false;
    }

    if (ota_isBusy())
    {
        setErr(errBuf, errBufLen, "busy");
        ota_recordError(state, "busy");
        ota_requestPublish();
        return false;
    }

    if (!url || url[0] == '\0')
    {
        const char *reason = "missing_url";
        ota_logBlocked("pull_start", reason, "Pull OTA blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    const char *source = ota_sourceFromRequestId(request_id);
    LOG_INFO(LogDomain::OTA, "OTA Update request (%s) -> target %s",
             source,
             (version && version[0] != '\0') ? version : "<unknown>");

    // Enforce HTTPS for pull-OTA URLs
    if (strncasecmp(url, "https://", 8) != 0)
    {
        const char *reason = "url_not_https";
        ota_logBlocked("pull_start", reason, "Pull OTA blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    if (version && version[0] != '\0')
    {
        LOG_INFO(LogDomain::OTA, "Checking update... (%s -> %s)",
                 (state->device.fw && state->device.fw[0] != '\0') ? state->device.fw : "<unknown>",
                 version);
        Version targetVersion;
        if (!parseVersion(version, &targetVersion))
        {
            const char *reason = "invalid_version";
            ota_logBlocked("pull_start", reason, "Pull OTA blocked: %s target=%s", reason, version);
            setErr(errBuf, errBufLen, reason);
            ota_recordError(state, reason);
            ota_requestPublish();
            return false;
        }

        const char *currentFw = (state->device.fw && state->device.fw[0] != '\0') ? state->device.fw : "";
        Version currentVersion;
        bool currentValid = false;
        int cmp = 0;
        if (currentFw[0] != '\0')
        {
            currentValid = parseVersion(currentFw, &currentVersion);
        }
        if (currentValid)
        {
            cmp = compareVersion(targetVersion, currentVersion);
#if CFG_OTA_DEV_LOGS
            LOG_INFO(LogDomain::OTA,
                     "OTA version compare current=%s target=%s cmp=%d force=%s",
                     currentFw,
                     version,
                     cmp,
                     force ? "true" : "false");
#endif

            if (!force && cmp < 0)
            {
                const char *reason = "downgrade_blocked";
                ota_logBlocked("pull_start", reason, "Pull OTA blocked: %s current=%s target=%s",
                               reason, currentFw, version);
                setErr(errBuf, errBufLen, reason);
                ota_recordError(state, reason);
                ota_requestPublish();
                return false;
            }

            if (!force && cmp == 0)
            {
                ota_setStatus(state, OtaStatus::SUCCESS);
                ota_setResult(state, "success", "noop_already_on_version");
                ota_setFlat(state, "success", 100, "", version, true);
                state->update_available = false;
                ota_requestPublish();
                setErr(errBuf, errBufLen, "noop");
                return true;
            }
        }
        else
        {
#if CFG_OTA_DEV_LOGS
            LOG_INFO(LogDomain::OTA,
                     "OTA version compare skipped current=%s target=%s cmp=na force=%s",
                     currentFw[0] ? currentFw : "<empty>",
                     version,
                     force ? "true" : "false");
#endif
            if (!force && currentFw[0] != '\0' && strcmp(version, currentFw) == 0)
            {
                ota_setStatus(state, OtaStatus::SUCCESS);
                ota_setResult(state, "success", "noop_already_on_version");
                ota_setFlat(state, "success", 100, "", version, true);
                state->update_available = false;
                ota_requestPublish();
                setErr(errBuf, errBufLen, "noop");
                return true;
            }
            if (!force)
            {
                const char *reason = "current_version_invalid";
                ota_logBlocked("pull_start", reason, "Pull OTA blocked: %s current=%s target=%s",
                               reason,
                               currentFw[0] ? currentFw : "<empty>",
                               version);
                setErr(errBuf, errBufLen, reason);
                ota_recordError(state, reason);
                ota_requestPublish();
                return false;
            }
        }
    }

    OtaTaskJob taskJob{};
    strncpy(taskJob.request_id, request_id ? request_id : "", sizeof(taskJob.request_id));
    taskJob.request_id[sizeof(taskJob.request_id) - 1] = '\0';
    strncpy(taskJob.version, version ? version : "", sizeof(taskJob.version));
    taskJob.version[sizeof(taskJob.version) - 1] = '\0';
    strncpy(taskJob.url, url, sizeof(taskJob.url));
    taskJob.url[sizeof(taskJob.url) - 1] = '\0';
    strncpy(taskJob.sha256, sha256 ? sha256 : "", sizeof(taskJob.sha256));
    taskJob.sha256[sizeof(taskJob.sha256) - 1] = '\0';
    taskJob.force = force;
    taskJob.reboot = reboot;
#if CFG_OTA_DEV_LOGS
    char shaPrefix[13] = {0};
    strncpy(shaPrefix, taskJob.sha256, 12);
    shaPrefix[sizeof(shaPrefix) - 1] = '\0';
    LOG_INFO(LogDomain::OTA,
             "Pull OTA enqueue request request_id=%s version=%s url_len=%u sha_prefix=%s force=%s reboot=%s",
             taskJob.request_id[0] ? taskJob.request_id : "<none>",
             taskJob.version[0] ? taskJob.version : "<none>",
             (unsigned)strlen(taskJob.url),
             taskJob.sha256[0] ? shaPrefix : "<none>",
             taskJob.force ? "true" : "false",
             taskJob.reboot ? "true" : "false");
#endif

    if (!ota_taskEnqueue(taskJob))
    {
        const char *reason = "queue_rejected";
        LOG_WARN(LogDomain::OTA,
                 "Pull OTA queue rejected reason=%s request_id=%s version=%s",
                 reason,
                 taskJob.request_id[0] ? taskJob.request_id : "<none>",
                 taskJob.version[0] ? taskJob.version : "<none>");
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    // Mirror into device-owned state
    ota_setStatus(state, OtaStatus::DOWNLOADING);
    strncpy(state->ota.request_id, taskJob.request_id, sizeof(state->ota.request_id));
    strncpy(state->ota.version, taskJob.version, sizeof(state->ota.version));
    strncpy(state->ota.url, taskJob.url, sizeof(state->ota.url));
    strncpy(state->ota.sha256, taskJob.sha256, sizeof(state->ota.sha256));
    state->ota.request_id[sizeof(state->ota.request_id) - 1] = '\0';
    state->ota.version[sizeof(state->ota.version) - 1] = '\0';
    state->ota.url[sizeof(state->ota.url) - 1] = '\0';
    state->ota.sha256[sizeof(state->ota.sha256) - 1] = '\0';
    state->ota.started_ts = ota_epochNow();

    // Clear last result fields for new attempt
    state->ota.last_status[0] = '\0';
    state->ota.last_message[0] = '\0';
    state->ota.completed_ts = 0;
    ota_setFlat(state, "downloading", 0, "", taskJob.version, true);
    int queuedCmp = 0;
    state->update_available = ota_isStrictUpgrade(state->device.fw, taskJob.version, &queuedCmp);
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "OTA queued version relation current=%s target=%s cmp=%d update_available=%s",
             (state->device.fw && state->device.fw[0]) ? state->device.fw : "<empty>",
             taskJob.version[0] ? taskJob.version : "<empty>",
             queuedCmp,
             state->update_available ? "true" : "false");
#else
    (void)queuedCmp;
#endif

    setErr(errBuf, errBufLen, "");
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "Pull OTA queued request_id=%s url=%s version=%s force=%s reboot=%s",
             taskJob.request_id[0] ? taskJob.request_id : "<none>",
             taskJob.url,
             taskJob.version,
             taskJob.force ? "true" : "false",
             taskJob.reboot ? "true" : "false");
#endif
    return true;
}

bool ota_pullStartFromManifest(DeviceState *state,
                               const char *request_id,
                               bool force,
                               bool reboot,
                               char *errBuf,
                               size_t errBufLen)
{
    if (!state)
    {
        setErr(errBuf, errBufLen, "missing_state");
        return false;
    }

    if (ota_isBusy())
    {
        setErr(errBuf, errBufLen, "busy");
        ota_recordError(state, "busy");
        ota_requestPublish();
        return false;
    }

    if (!WiFi.isConnected())
    {
        const char *reason = "wifi_disconnected";
        ota_logBlocked("manifest_pull", reason, "Manifest pull blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, "wifi_disconnected");
        return false;
    }
    if (!wifi_timeIsValid())
    {
        const char *reason = "time_not_set";
        ota_logBlocked("manifest_pull", reason, "Manifest pull blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }

    if (!ota_checkSafetyGuards(state, "manifest_pull", errBuf, errBufLen))
    {
        return false;
    }

    // Manifest must be served from GitHub Releases (latest/download) because
    // raw.githubusercontent.com may use CDN chains that do not match the pinned root CA.
    const char *manifestUrl = CFG_OTA_MANIFEST_URL;
    if (!manifestUrl || manifestUrl[0] == '\0')
    {
        const char *reason = "missing_manifest_url";
        ota_logBlocked("manifest_pull", reason, "Manifest pull blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }
    if (strncasecmp(manifestUrl, "https://", 8) != 0)
    {
        const char *reason = "manifest_url_not_https";
        ota_logBlocked("manifest_pull", reason, "Manifest pull blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }
    if (ota_urlContainsNoCase(manifestUrl, "raw.githubusercontent.com"))
    {
        const char *reason = "manifest_raw_disallowed";
        ota_logBlocked("manifest_pull", reason, "Manifest pull blocked: %s url=%s", reason, manifestUrl);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }

    WiFiClientSecure client;
    ota_prepareTlsClient(client, "manifest_pull", manifestUrl);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(10);

    const bool beginOk = http.begin(client, manifestUrl);
    if (!beginOk)
    {
        ota_captureTlsError(client);
        ota_logTlsStatus("manifest_pull", manifestUrl, false, 0);
        const char *reason = ota_tlsFailureReason(0);
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }

    static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
    http.collectHeaders(kHeaders, 3);
    http.setUserAgent("DadsSmartHomeWaterTank/1.0");
    http.addHeader("Accept", "application/json");
    http.useHTTP10(false);

    const int code = http.GET();
    const bool requestOk = (code > 0);
    if (!requestOk)
    {
        ota_captureTlsError(client);
        ota_logTlsStatus("manifest_pull", manifestUrl, false, code);
        const char *reason = ota_tlsFailureReason(code);
        http.end();
        setErr(errBuf, errBufLen, reason);
        ota_markFailed(state, reason);
        return false;
    }
    ota_logTlsStatus("manifest_pull", manifestUrl, true, code);
    if (code != HTTP_CODE_OK)
    {
        char msg[32];
        snprintf(msg, sizeof(msg), "manifest_http_%d", code);
        http.end();
        setErr(errBuf, errBufLen, msg);
        ota_markFailed(state, msg);
        return false;
    }

    String ctype = http.header("Content-Type");
    if (ctype.length() > 0)
    {
        String lower = ctype;
        lower.toLowerCase();
        if (lower.indexOf("text/html") >= 0)
        {
            http.end();
            setErr(errBuf, errBufLen, "manifest_bad_content_type");
            ota_markFailed(state, "manifest_bad_content_type");
            return false;
        }
    }

    StaticJsonDocument<768> doc;
    const DeserializationError derr = deserializeJson(doc, http.getStream());
    http.end();
    if (derr)
    {
        setErr(errBuf, errBufLen, "manifest_parse_failed");
        ota_markFailed(state, "manifest_parse_failed");
        return false;
    }

    const char *version = doc["version"] | "";
    const char *url = doc["url"] | "";
    const char *sha256 = doc["sha256"] | "";

    if (!version || version[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_version");
        ota_markFailed(state, "manifest_missing_version");
        return false;
    }
    if (!url || url[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_url");
        ota_markFailed(state, "manifest_missing_url");
        return false;
    }
    if (!ota_manifestUrlHostTrusted(url))
    {
        setErr(errBuf, errBufLen, "manifest_url_untrusted_host");
        ota_markFailed(state, "manifest_url_untrusted_host");
        return false;
    }
    if (!sha256 || sha256[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_sha256");
        ota_markFailed(state, "manifest_missing_sha256");
        return false;
    }
    if (!isHex64(sha256))
    {
        setErr(errBuf, errBufLen, "bad_sha256_format");
        ota_markFailed(state, "bad_sha256_format");
        return false;
    }

    const bool ok = ota_pullStart(state, request_id, version, url, sha256, force, reboot, errBuf, errBufLen);
    if (!ok && errBuf && errBuf[0] && strcmp(errBuf, "busy") != 0)
    {
        ota_markFailed(state, errBuf);
    }
    return ok;
}

bool ota_checkManifest(DeviceState *state, char *errBuf, size_t errBufLen)
{
    if (!state)
    {
        setErr(errBuf, errBufLen, "missing_state");
        return false;
    }
    if (ota_isBusy())
    {
        setErr(errBuf, errBufLen, "busy");
        ota_recordError(state, "busy");
        ota_requestPublish();
        return false;
    }
    if (!WiFi.isConnected())
    {
        const char *reason = "wifi_disconnected";
        ota_logBlocked("manifest_check", reason, "Manifest check blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (!wifi_timeIsValid())
    {
        const char *reason = "time_not_set";
        ota_logBlocked("manifest_check", reason, "Manifest check blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    // Manifest must be served from GitHub Releases (latest/download) because
    // raw.githubusercontent.com may use CDN chains that do not match the pinned root CA.
    const char *manifestUrl = CFG_OTA_MANIFEST_URL;
    if (!manifestUrl || manifestUrl[0] == '\0')
    {
        const char *reason = "missing_manifest_url";
        ota_logBlocked("manifest_check", reason, "Manifest check blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (strncasecmp(manifestUrl, "https://", 8) != 0)
    {
        const char *reason = "manifest_url_not_https";
        ota_logBlocked("manifest_check", reason, "Manifest check blocked: %s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (ota_urlContainsNoCase(manifestUrl, "raw.githubusercontent.com"))
    {
        const char *reason = "manifest_raw_disallowed";
        ota_logBlocked("manifest_check", reason, "Manifest check blocked: %s url=%s", reason, manifestUrl);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    WiFiClientSecure client;
    ota_prepareTlsClient(client, "manifest_check", manifestUrl);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(10);

    const bool beginOk = http.begin(client, manifestUrl);
    if (!beginOk)
    {
        ota_captureTlsError(client);
        ota_logTlsStatus("manifest_check", manifestUrl, false, 0);
        const char *reason = ota_tlsFailureReason(0);
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
    http.collectHeaders(kHeaders, 3);
    http.setUserAgent("DadsSmartHomeWaterTank/1.0");
    http.addHeader("Accept", "application/json");
    http.useHTTP10(false);

    const int code = http.GET();
    const bool requestOk = (code > 0);
    if (!requestOk)
    {
        ota_captureTlsError(client);
        ota_logTlsStatus("manifest_check", manifestUrl, false, code);
        const char *reason = ota_tlsFailureReason(code);
        http.end();
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    ota_logTlsStatus("manifest_check", manifestUrl, true, code);
    if (code != HTTP_CODE_OK)
    {
        char msg[32];
        snprintf(msg, sizeof(msg), "manifest_http_%d", code);
        http.end();
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", msg);
        setErr(errBuf, errBufLen, msg);
        ota_recordError(state, msg);
        ota_requestPublish();
        return false;
    }

    StaticJsonDocument<768> doc;
    const DeserializationError derr = deserializeJson(doc, http.getStream());
    http.end();
    if (derr)
    {
        const char *reason = "manifest_parse_failed";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    const char *version = doc["version"] | "";
    const char *url = doc["url"] | "";
    const char *sha256 = doc["sha256"] | "";
    if (!version || version[0] == '\0')
    {
        const char *reason = "manifest_missing_version";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (!url || url[0] == '\0')
    {
        const char *reason = "manifest_missing_url";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (!ota_manifestUrlHostTrusted(url))
    {
        const char *reason = "manifest_url_untrusted_host";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (!sha256 || sha256[0] == '\0')
    {
        const char *reason = "manifest_missing_sha256";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    if (!isHex64(sha256))
    {
        const char *reason = "bad_sha256_format";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s", reason);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }
    Version parsedManifestVersion;
    if (!parseVersion(version, &parsedManifestVersion))
    {
        const char *reason = "manifest_invalid_version";
        LOG_ERROR(LogDomain::OTA, "Manifest check failed reason=%s version=%s", reason, version);
        setErr(errBuf, errBufLen, reason);
        ota_recordError(state, reason);
        ota_requestPublish();
        return false;
    }

    strncpy(state->ota_target_version, version, sizeof(state->ota_target_version));
    state->ota_target_version[sizeof(state->ota_target_version) - 1] = '\0';
    const uint32_t manifestEpoch = ota_epochNow();
    if (manifestEpoch > 0)
    {
        state->ota_last_ts = manifestEpoch;
    }
    int manifestCmp = 0;
    bool manifestCmpValid = false;
    if (state->device.fw && state->device.fw[0] != '\0')
    {
        Version parsedCurrentVersion;
        if (parseVersion(state->device.fw, &parsedCurrentVersion))
        {
            manifestCmp = compareVersion(parsedManifestVersion, parsedCurrentVersion);
            state->update_available = (manifestCmp > 0);
            manifestCmpValid = true;
        }
        else
        {
            state->update_available = false;
        }
    }
    else
    {
        state->update_available = false;
    }
    char manifestCmpBuf[8] = "na";
    if (manifestCmpValid)
    {
        snprintf(manifestCmpBuf, sizeof(manifestCmpBuf), "%d", manifestCmp);
    }
    LOG_INFO(LogDomain::OTA,
             "Manifest version relation current=%s target=%s cmp=%s update_available=%s",
             (state->device.fw && state->device.fw[0]) ? state->device.fw : "<empty>",
             version,
             manifestCmpBuf,
             state->update_available ? "true" : "false");
    state->ota_error[0] = '\0';

    setErr(errBuf, errBufLen, "");
    return true;
}

static void ota_abort(DeviceState *state, const char *reason)
{
    ota_progressEnsureLineBreak();
    ota_trace("abort", "reason=%s", reason ? reason : "");
#if CFG_OTA_DEV_LOGS
    LOG_WARN(LogDomain::OTA,
             "OTA abort detail reason=%s bytes_written=%lu bytes_total=%lu update_begun=%s http_begun=%s free_heap=%lu",
             reason ? reason : "",
             (unsigned long)g_job.bytesWritten,
             (unsigned long)g_job.bytesTotal,
             g_job.updateBegun ? "true" : "false",
             g_job.httpBegun ? "true" : "false",
             (unsigned long)ESP.getFreeHeap());
    ota_logPartitionSnapshot("abort");
#endif

    if (g_job.retryCount < MAX_OTA_RETRIES)
    {
        const uint8_t nextRetryCount = (uint8_t)(g_job.retryCount + 1u);
        const uint32_t backoffMs = (uint32_t)(BASE_RETRY_DELAY_MS << nextRetryCount);
        g_job.retryCount = nextRetryCount;
        g_job.nextRetryAtMs = millis() + backoffMs;

        if (g_job.updateBegun)
        {
            (void)esp_ota_abort(g_job.otaHandle);
            g_job.otaHandle = 0;
            g_job.updateBegun = false;
        }
        if (g_job.httpBegun)
        {
            g_job.http.end();
            g_job.httpBegun = false;
        }
        if (g_job.shaInit)
        {
            mbedtls_sha256_free(&g_job.shaCtx);
            g_job.shaInit = false;
        }
        g_job.bytesTotal = 0;
        g_job.bytesWritten = 0;
        g_job.lastProgressMs = 0;
        g_job.lastReportMs = 0;
        g_job.lastDiagMs = 0;
        g_job.noDataSinceMs = 0;
        g_job.zeroReadStreak = 0;
        g_job.netRetryCount = 0;
        g_job.retryAtMs = 0;
        ota_progressReset();

        if (state)
        {
            ota_setStatus(state, OtaStatus::RETRYING);
            ota_setResult(state, "error", reason ? reason : "retrying");
            ota_setFlat(state, "retrying", state->ota.progress, reason ? reason : "retrying", state->ota.version, true);
            ota_requestPublish();
        }

        ota_logWarnMaybeDeferred("Pull OTA retry scheduled reason=%s attempt=%u/%u backoff_ms=%lu",
                                 reason ? reason : "",
                                 (unsigned int)g_job.retryCount,
                                 (unsigned int)MAX_OTA_RETRIES,
                                 (unsigned long)backoffMs);
        return;
    }

    if (state)
    {
        ota_setStatus(state, OtaStatus::ERROR);
        ota_setResult(state, "error", reason ? reason : "error");
        ota_setFlat(state, "failed", state->ota.progress, reason ? reason : "error", state->ota.version, true);
        ota_clearActive(state);
        ota_requestPublish();
    }

    ota_progressFail(reason);

    if (g_job.updateBegun)
    {
        (void)esp_ota_abort(g_job.otaHandle);
        g_job.otaHandle = 0;
        g_job.updateBegun = false;
    }

    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }

    if (g_job.shaInit)
    {
        mbedtls_sha256_free(&g_job.shaCtx);
        g_job.shaInit = false;
    }

    g_job.active = false;
    const uint32_t elapsedMs = (g_job.startedMs > 0u) ? (millis() - g_job.startedMs) : 0u;
    ota_progressReset();
#if CFG_OTA_DEV_LOGS
    LOG_ERROR(LogDomain::OTA, "ota_failed_no_reboot reason=%s", reason ? reason : "");
    LOG_WARN(LogDomain::OTA, "Pull OTA aborted reason=%s", reason ? reason : "");
#else
    ota_logFailBanner(reason ? reason : "error",
                      g_job.version[0] ? g_job.version : nullptr,
                      elapsedMs);
    LOG_ERROR(LogDomain::OTA, "OTA failed: %s", reason ? reason : "error");
#endif
}

static bool ota_requireEspOk(DeviceState *state, const char *op, esp_err_t err)
{
    if (err == ESP_OK)
    {
        return true;
    }

    char reason[OTA_ERROR_MAX];
    snprintf(reason, sizeof(reason), "%s_err_%d", op ? op : "esp_ota", (int)err);
    reason[sizeof(reason) - 1] = '\0';
    const char *errName = esp_err_to_name(err);
    LOG_ERROR(LogDomain::OTA, "%s failed err=%d err_name=%s",
              op ? op : "esp_ota",
              (int)err,
              errName ? errName : "<unknown>");

    // Flash/boot partition API failures are terminal for this pull attempt.
    g_job.retryCount = MAX_OTA_RETRIES;
    ota_abort(state, reason);
    return false;
}

static void ota_finishSuccess(DeviceState *state)
{
    ota_trace("finish_success_enter", "bytes=%lu/%lu",
              (unsigned long)g_job.bytesWritten,
              (unsigned long)g_job.bytesTotal);
    ota_logPartitionSnapshot("finish_success_pre_state");

    if (state)
    {
        ota_setStatus(state, OtaStatus::SUCCESS);
        ota_setResult(state, "success", "applied");
        ota_setFlat(state, "success", 100, "", state->ota.version, true);
        if (ota_isInOtaTaskContext())
        {
            ota_events_pushUpdateAvailable(false);
        }
        else
        {
            state->update_available = false;
        }
        const uint32_t epochNow = ota_epochNow();
        if (epochNow > 0)
        {
            if (ota_isInOtaTaskContext())
            {
                ota_events_pushLastSuccessTs(epochNow);
            }
            else
            {
                state->ota_last_success_ts = epochNow;
            }
            storage_saveOtaLastSuccess(epochNow);
        }
        ota_requestPublish();
    }

    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }

    g_job.active = false;
    ota_trace("finish_success_complete", "reboot=%s", g_job.reboot ? "true" : "false");
    const uint32_t elapsedMs = (g_job.startedMs > 0u) ? (millis() - g_job.startedMs) : 0u;
    ota_logSuccessBanner(g_job.version[0] ? g_job.version : nullptr, elapsedMs);
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA, "Pull OTA success");
    s_otaHeartbeatEnabled = false;
    LOG_INFO(LogDomain::OTA, "OTA heartbeat diagnostics auto-disabled after successful apply");
    LOG_INFO(LogDomain::OTA,
             "OTA finalize summary bytes_written=%lu bytes_total=%lu free_heap=%lu",
             (unsigned long)g_job.bytesWritten,
             (unsigned long)g_job.bytesTotal,
             (unsigned long)ESP.getFreeHeap());
    ota_logPartitionSnapshot("finish_success_post_state");
#endif

    if (g_job.reboot)
    {
        if (state)
        {
            ota_setStatus(state, OtaStatus::REBOOTING);
        }
        storage_saveRebootIntent((uint8_t)RebootIntent::OTA);
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "Saved reboot intent=ota");
        LOG_INFO(LogDomain::OTA, "ota_success_rebooting");
        LOG_WARN(LogDomain::OTA, "REBOOTING... reason=ota_apply_success intent=ota delay_ms=2250");
#else
        LOG_INFO(LogDomain::OTA, "Rebooting into %s...",
                 g_job.version[0] ? g_job.version : "new firmware");
#endif
        delay(250);
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "Restarting into new firmware...");
#endif
        delay(2000);
        Serial.flush();
        ESP.restart();
    }
}

void ota_processPullJobInTask(DeviceState *state, const OtaTaskJob &job)
{
    if (!state)
    {
        ota_trace("task_start_skip", "state_missing");
        logger_setOtaQuietMode(false);
        return;
    }

    ota_primeRuntimeJob(job);
    if (s_otaTaskHandle == nullptr)
    {
        s_otaTaskHandle = xTaskGetCurrentTaskHandle();
    }
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "otaTask processing request_id=%s target=%s force=%s reboot=%s",
             g_job.request_id[0] ? g_job.request_id : "<none>",
             g_job.version[0] ? g_job.version : "<none>",
             g_job.force ? "true" : "false",
             g_job.reboot ? "true" : "false");
#endif
    ota_trace("task_start", "url=%s sha_prefix=%.12s",
              g_job.url[0] ? g_job.url : "<none>",
              g_job.sha256[0] ? g_job.sha256 : "<none>");
    ota_logRuntimeHealth("task_start");

    // Validate runtime guards in task context before the pull starts.
    if (!ota_checkSafetyGuards(state, "pull_task_start", nullptr, 0))
    {
        const char *reason = "guard_rejected";
        ota_trace("guard_fail", "reason=%s", reason);
        ota_markFailed(state, reason);
        ota_resetRuntimeJob();
        return;
    }

    if (!WiFi.isConnected())
    {
        ota_trace("preflight_fail", "wifi_disconnected");
        ota_abort(state, "wifi_disconnected");
    }
    else if (!wifi_timeIsValid())
    {
        ota_trace("preflight_fail", "time_not_set");
        ota_abort(state, "time_not_set");
    }

    if (g_job.active)
    {
        logger_setOtaQuietMode(true);
    }

    while (g_job.active)
    {
#if CFG_OTA_DEV_LOGS
        logger_logEvery("ota_task_loop", 2000u, LogLevel::DEBUG, LogDomain::OTA,
                        "[TRACE] step=task_loop active=%s http=%s update=%s written=%lu total=%lu retry=%u",
                        g_job.active ? "true" : "false",
                        g_job.httpBegun ? "true" : "false",
                        g_job.updateBegun ? "true" : "false",
                        (unsigned long)g_job.bytesWritten,
                        (unsigned long)g_job.bytesTotal,
                        (unsigned int)g_job.retryCount);
#endif
        char cancelReason[OTA_ERROR_MAX] = {0};
        if (ota_taskTakeCancelReason(cancelReason, sizeof(cancelReason)))
        {
            ota_trace("cancel_received", "reason=%s", cancelReason[0] ? cancelReason : "cancelled");
            g_job.retryCount = MAX_OTA_RETRIES;
            ota_abort(state, cancelReason[0] ? cancelReason : "cancelled");
            continue;
        }

        ota_tick(state);
        if (g_job.active)
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    logger_setOtaQuietMode(false);
    ota_trace("task_exit", "request_id=%s", g_job.request_id[0] ? g_job.request_id : "<none>");
}

static void ota_tick(DeviceState *state)
{
    if (!g_job.active)
    {
#if CFG_OTA_DEV_LOGS
        logger_logEvery("ota_tick_idle", 3000u, LogLevel::DEBUG, LogDomain::OTA, "[TRACE] step=tick_idle");
#endif
        return;
    }

    const uint32_t nowMs = millis();
#if CFG_OTA_DEV_LOGS
    logger_logEvery("ota_tick_active", 1500u, LogLevel::DEBUG, LogDomain::OTA,
                    "[TRACE] step=tick active=true http=%s update=%s bytes=%lu/%lu retry_at=%lu next_retry_at=%lu",
                    g_job.httpBegun ? "true" : "false",
                    g_job.updateBegun ? "true" : "false",
                    (unsigned long)g_job.bytesWritten,
                    (unsigned long)g_job.bytesTotal,
                    (unsigned long)g_job.retryAtMs,
                    (unsigned long)g_job.nextRetryAtMs);
#endif
    if (g_job.nextRetryAtMs != 0 && !ota_timeReached(nowMs, g_job.nextRetryAtMs))
    {
#if CFG_OTA_DEV_LOGS
        logger_logEvery("ota_wait_next_retry", 1000u, LogLevel::DEBUG, LogDomain::OTA,
                        "[TRACE] step=wait_next_retry now=%lu target=%lu",
                        (unsigned long)nowMs,
                        (unsigned long)g_job.nextRetryAtMs);
#endif
        return;
    }
    if (g_job.nextRetryAtMs != 0 && ota_timeReached(nowMs, g_job.nextRetryAtMs))
    {
        ota_trace("retry_window_reached", "now=%lu target=%lu",
                  (unsigned long)nowMs, (unsigned long)g_job.nextRetryAtMs);
        g_job.nextRetryAtMs = 0;
        if (g_job.updateBegun)
        {
            (void)esp_ota_abort(g_job.otaHandle);
            g_job.otaHandle = 0;
            g_job.updateBegun = false;
        }
        if (g_job.httpBegun)
        {
            g_job.http.end();
            g_job.httpBegun = false;
        }
        if (g_job.shaInit)
        {
            mbedtls_sha256_free(&g_job.shaCtx);
            g_job.shaInit = false;
        }
        g_job.bytesTotal = 0;
        g_job.bytesWritten = 0;
        g_job.lastProgressMs = 0;
        g_job.lastReportMs = 0;
        g_job.lastDiagMs = 0;
        g_job.noDataSinceMs = 0;
        g_job.zeroReadStreak = 0;
        g_job.netRetryCount = 0;
        g_job.retryAtMs = 0;
        if (state)
        {
            ota_setStatus(state, OtaStatus::DOWNLOADING);
            ota_setFlat(state, "downloading", 0, "", state->ota.version, false);
            ota_requestPublish();
        }
        LOG_INFO(LogDomain::OTA, "Pull OTA retrying download attempt=%u/%u",
                 (unsigned int)g_job.retryCount,
                 (unsigned int)MAX_OTA_RETRIES);
    }

    if (!WiFi.isConnected())
    {
        ota_trace("guard_fail", "wifi_disconnected");
        ota_abort(state, "wifi_disconnected");
        return;
    }
    if (!wifi_timeIsValid())
    {
        ota_logBlocked("download", "time_not_set", "Firmware download blocked: time_not_set");
        ota_trace("guard_fail", "time_not_set");
        ota_abort(state, "time_not_set");
        return;
    }

    if (!g_job.httpBegun && g_job.retryAtMs != 0 && !ota_timeReached(nowMs, g_job.retryAtMs))
    {
#if CFG_OTA_DEV_LOGS
        logger_logEvery("ota_wait_http_retry", 1000u, LogLevel::DEBUG, LogDomain::OTA,
                        "[TRACE] step=wait_http_retry now=%lu target=%lu",
                        (unsigned long)nowMs,
                        (unsigned long)g_job.retryAtMs);
#endif
        return;
    }
    if (!g_job.httpBegun && g_job.retryAtMs != 0 && ota_timeReached(nowMs, g_job.retryAtMs))
    {
        ota_trace("http_retry_window_reached", "now=%lu target=%lu",
                  (unsigned long)nowMs, (unsigned long)g_job.retryAtMs);
        g_job.retryAtMs = 0;
    }

    // Step A: begin HTTP if not begun
    if (!g_job.httpBegun)
    {
        ota_trace("http_begin_prepare", "url=%s", g_job.url[0] ? g_job.url : "<none>");
        ota_logRuntimeHealth("http_begin_prepare");
        const uint32_t connectTimeoutMs = (uint32_t)CFG_OTA_HTTP_CONNECT_TIMEOUT_MS;
        const uint32_t readTimeoutMs = (uint32_t)CFG_OTA_HTTP_READ_TIMEOUT_MS;
        const uint16_t readTimeoutMsClamped = (readTimeoutMs > 65535u) ? 65535u : (uint16_t)readTimeoutMs;

        g_job.http.end();
        g_job.client.stop();
        g_job.client = WiFiClientSecure();
        ota_prepareTlsClient(g_job.client, "firmware_download", g_job.url);

#ifdef HTTPC_FORCE_FOLLOW_REDIRECTS
        g_job.http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        ota_trace("http_redirect_policy", "mode=force_follow limit=%u", 10u);
#else
        g_job.http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        ota_trace("http_redirect_policy", "mode=strict_follow limit=%u", 10u);
#endif
        g_job.http.setRedirectLimit(10);
        g_job.http.setConnectTimeout((int32_t)connectTimeoutMs);
        g_job.http.setTimeout(readTimeoutMsClamped);

        const uint32_t hsStartMs = millis();
        const bool beginOk = g_job.http.begin(g_job.client, g_job.url);
        const uint32_t hsElapsedMs = millis() - hsStartMs;
        if (!beginOk)
        {
            ota_trace("http_begin_fail", "elapsed_ms=%lu", (unsigned long)hsElapsedMs);
            ota_captureTlsError(g_job.client);
            ota_logTlsStatus("firmware_download", g_job.url, false, 0);
            const char *reason = ota_classifyBeginFailure(hsElapsedMs);
            ota_scheduleRetry(state, reason);
            return;
        }
        ota_trace("http_begin_ok", "elapsed_ms=%lu", (unsigned long)hsElapsedMs);
        ota_logRuntimeHealth("http_begin_ok");
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA,
                 "HTTP begin ok url=%s announced_len_pre_get=%d",
                 g_job.url,
                 g_job.http.getSize());
#endif
        g_job.httpBegun = true;

        static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
        g_job.http.collectHeaders(kHeaders, 3);
        g_job.http.setUserAgent("DadsSmartHomeWaterTank/1.0");
        g_job.http.addHeader("Accept", "application/octet-stream");
        g_job.http.useHTTP10(false);

        ota_trace("http_get_start", "url=%s", g_job.url);
        const uint32_t getStartMs = millis();
        const int code = g_job.http.GET();
        const uint32_t getElapsedMs = millis() - getStartMs;
        const int responseLen = g_job.http.getSize();
        String location = g_job.http.header("Location");
        ota_trace("http_get_done", "code=%d len=%d location=%s requested_url=%s",
                  code,
                  responseLen,
                  location.length() > 0 ? location.c_str() : "<none>",
                  g_job.url);
        const bool requestOk = (code > 0);
        if (!requestOk)
        {
            ota_trace("http_get_fail", "code=%d elapsed_ms=%lu", code, (unsigned long)getElapsedMs);
            ota_captureTlsError(g_job.client);
            ota_logTlsStatus("firmware_download", g_job.url, false, code);
            const char *reason = ota_classifyRequestFailure(code);
            if (getElapsedMs >= readTimeoutMs)
            {
                reason = "http_timeout";
            }
            ota_scheduleRetry(state, reason);
            return;
        }
        ota_logTlsStatus("firmware_download", g_job.url, true, code);
        if (code != HTTP_CODE_OK)
        {
            ota_trace("http_status_fail", "code=%d", code);
            char msg[32];
            ota_formatHttpCodeReason(code, msg, sizeof(msg));
            ota_scheduleRetry(state, msg);
            return;
        }
        ota_trace("http_status_ok", "code=%d", code);
        ota_logRuntimeHealth("http_status_ok");

#if CFG_OTA_DEV_LOGS
        if (location.length() > 0)
        {
            LOG_INFO(LogDomain::OTA, "HTTP Location header=%s", location.c_str());
        }
        else
        {
            LOG_DEBUG(LogDomain::OTA, "HTTP Location header=<none>");
        }
#endif
        g_job.netRetryCount = 0;
        g_job.retryAtMs = 0;

        String ctype = g_job.http.header("Content-Type");
        if (ctype.length() > 0)
        {
            String lower = ctype;
            lower.toLowerCase();
            if (lower.indexOf("text/html") >= 0 || lower.indexOf("application/json") >= 0)
            {
                ota_abort(state, "bad_content_type");
                return;
            }
            if (lower.indexOf("application/octet-stream") < 0)
            {
#if CFG_OTA_DEV_LOGS
                LOG_WARN(LogDomain::OTA, "Unexpected content-type for firmware: %s", ctype.c_str());
#endif
            }
        }
        else
        {
#if CFG_OTA_DEV_LOGS
            LOG_WARN(LogDomain::OTA, "Missing Content-Type header for firmware response");
#endif
        }

        int len = g_job.http.getSize();
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "HTTP %d len=%d ctype=%s", code, len, ctype.c_str());
        LOG_INFO(LogDomain::OTA, "Partition free space approx=%u", (unsigned)ESP.getFreeSketchSpace());
#endif
        ota_trace("http_headers", "len=%d ctype=%s", len, ctype.c_str());
        ota_logRuntimeHealth("http_headers");

        if (len <= 0)
        {
            ota_abort(state, "missing_content_length");
            return;
        }
        g_job.bytesTotal = (uint32_t)len;
        if (len < OTA_MIN_BYTES)
        {
            ota_abort(state, "content_too_small");
            return;
        }

        if (g_job.bytesTotal > ESP.getFreeSketchSpace())
        {
            ota_abort(state, "not_enough_space");
            return;
        }

        char sizeBuf[24] = {0};
        char freeBuf[24] = {0};
        ota_formatSizeCompact(g_job.bytesTotal, sizeBuf, sizeof(sizeBuf));
        ota_formatSizeCompact((uint32_t)ESP.getFreeSketchSpace(), freeBuf, sizeof(freeBuf));
        LOG_INFO(LogDomain::OTA, "Updating to %s (%s) • Free storage: %s",
                 g_job.version[0] ? g_job.version : "<unknown>",
                 sizeBuf,
                 freeBuf);
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "HTTP len=%d -> bytesTotal=%lu", len, (unsigned long)g_job.bytesTotal);
#endif

        WiFiClient *stream = g_job.http.getStreamPtr();
        if (!stream)
        {
            ota_abort(state, "no_stream");
            return;
        }

        uint8_t headerProbe[32] = {0};
        size_t headerProbeLen = 0;
        const uint32_t probeStartMs = millis();
        while (headerProbeLen == 0)
        {
            const int avail = stream->available();
            if (avail > 0)
            {
                size_t toRead = (size_t)avail;
                if (toRead > sizeof(headerProbe))
                {
                    toRead = sizeof(headerProbe);
                }
                const int n = stream->read(headerProbe, toRead);
                if (n > 0)
                {
                    headerProbeLen = (size_t)n;
                    break;
                }
            }

            if (!stream->connected() || (uint32_t)(millis() - probeStartMs) > 2000u)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (headerProbeLen == 0)
        {
            ota_trace("header_probe_fail", "empty");
            ota_abort(state, "invalid image header (empty)");
            return;
        }
        if (headerProbe[0] != 0xE9)
        {
            ota_trace("header_probe_fail", "magic=0x%02X", (unsigned int)headerProbe[0]);
            LOG_ERROR(LogDomain::OTA, "Invalid image header first_byte=0x%02X", (unsigned int)headerProbe[0]);
            ota_abort(state, "invalid image header (magic != 0xE9)");
            return;
        }
        ota_trace("header_probe_ok", "magic=0x%02X bytes=%u",
                  (unsigned int)headerProbe[0],
                  (unsigned int)headerProbeLen);
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "Image header probe ok first_byte=0x%02X bytes=%u",
                 (unsigned int)headerProbe[0],
                 (unsigned int)headerProbeLen);
#endif

        g_job.targetPartition = esp_ota_get_next_update_partition(nullptr);
        if (g_job.targetPartition == nullptr)
        {
            (void)ota_requireEspOk(state, "esp_ota_get_next_update_partition", ESP_ERR_NOT_FOUND);
            return;
        }
        ota_logPartitionSnapshot("before_update_begin");
        ota_emitPartitionDiag("before_update_begin");
        const size_t updateSize =
            (g_job.bytesTotal > 0) ? (size_t)g_job.bytesTotal : (size_t)OTA_SIZE_UNKNOWN;

#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA, "esp_ota_begin partition=%s@0x%08lx size=%s (%lu)",
                 g_job.targetPartition->label,
                 (unsigned long)g_job.targetPartition->address,
                 (g_job.bytesTotal > 0) ? "known" : "unknown",
                 (unsigned long)updateSize);
#endif
        ota_trace("ota_begin_start", "partition=%s@0x%08lx size=%lu",
                  g_job.targetPartition->label,
                  (unsigned long)g_job.targetPartition->address,
                  (unsigned long)updateSize);

        const bool wdtDetachedBegin = ota_detachCurrentTaskWdt("update_begin");
        const esp_err_t otaBeginErr = esp_ota_begin(g_job.targetPartition, updateSize, &g_job.otaHandle);
        ota_reattachCurrentTaskWdt(wdtDetachedBegin, "update_begin");
        if (!ota_requireEspOk(state, "esp_ota_begin", otaBeginErr))
        {
            return;
        }
        ota_trace("ota_begin_ok", "handle=%lu", (unsigned long)g_job.otaHandle);
        ota_logRuntimeHealth("ota_begin_ok");
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA,
                 "esp_ota_begin ok expected_len=%lu handle=%lu free_heap=%lu",
                 (unsigned long)g_job.bytesTotal,
                 (unsigned long)g_job.otaHandle,
                 (unsigned long)ESP.getFreeHeap());
#endif
        ota_logPartitionSnapshot("after_update_begin");
        mbedtls_sha256_init(&g_job.shaCtx);
        mbedtls_sha256_starts(&g_job.shaCtx, 0);
        g_job.shaInit = true;
        g_job.updateBegun = true;

        if (g_job.shaInit)
        {
            mbedtls_sha256_update(&g_job.shaCtx, headerProbe, headerProbeLen);
        }
        if (!ota_requireEspOk(state, "esp_ota_write", esp_ota_write(g_job.otaHandle, headerProbe, headerProbeLen)))
        {
            return;
        }
        g_job.bytesWritten = (uint32_t)headerProbeLen;
        g_job.bytesAtLastWriteLog = g_job.bytesWritten;
        ota_trace("ota_write_header_ok", "bytes=%u", (unsigned int)headerProbeLen);
        ota_progressPrint(g_job.bytesWritten, g_job.bytesTotal, false, false);

        if (state)
        {
            ota_setStatus(state, OtaStatus::DOWNLOADING);
            ota_setFlat(state, "downloading", 0, "", nullptr, false);
            ota_requestPublish();
        }

        g_job.lastProgressMs = millis();
        g_job.lastReportMs = g_job.lastProgressMs;
        g_job.lastDiagMs = g_job.lastProgressMs;
        g_job.lastWriteLogMs = g_job.lastProgressMs;
        g_job.zeroReadStreak = 0;
        g_job.noDataSinceMs = 0;
        return;
    }

    // Step B: stream a bounded chunk per tick
    WiFiClient *stream = g_job.http.getStreamPtr();
    if (!stream)
    {
        ota_trace("stream_fail", "stream_null");
        ota_abort(state, "no_stream");
        return;
    }

    const size_t kMaxChunk = 4096;
    uint8_t buf[512];
    size_t processed = 0;

    while (processed < kMaxChunk)
    {
        int avail = stream->available();
        if (avail <= 0)
        {
            if (g_job.zeroReadStreak == 0)
                g_job.noDataSinceMs = millis();
            g_job.zeroReadStreak++;
            break;
        }

        size_t toRead = (size_t)avail;
        if (toRead > sizeof(buf))
            toRead = sizeof(buf);

        int n = stream->read(buf, toRead);
        if (n <= 0)
        {
            if (g_job.zeroReadStreak == 0)
                g_job.noDataSinceMs = millis();
            g_job.zeroReadStreak++;
            break;
        }

        if (g_job.shaInit)
        {
            mbedtls_sha256_update(&g_job.shaCtx, buf, (size_t)n);
        }

        const esp_err_t otaWriteErr = esp_ota_write(g_job.otaHandle, buf, (size_t)n);
        if (!ota_requireEspOk(state, "esp_ota_write", otaWriteErr))
        {
            return;
        }

        g_job.zeroReadStreak = 0;
        g_job.noDataSinceMs = 0;
        g_job.bytesWritten += (uint32_t)n;
        processed += (size_t)n;
        g_job.lastProgressMs = millis();
        ota_progressPrint(g_job.bytesWritten, g_job.bytesTotal, false, false);

#if CFG_OTA_DEV_LOGS
        const uint32_t nowWriteMs = millis();
        const uint32_t bytesSinceLog = g_job.bytesWritten - g_job.bytesAtLastWriteLog;
        if ((uint32_t)(nowWriteMs - g_job.lastWriteLogMs) >= 250u || bytesSinceLog >= 32768u)
        {
            uint32_t pct = 0u;
            if (g_job.bytesTotal > 0u)
            {
                pct = (g_job.bytesWritten * 100u) / g_job.bytesTotal;
                if (pct > 100u)
                {
                    pct = 100u;
                }
            }
            LOG_INFO(LogDomain::OTA,
                     "[TRACE] step=write_chunk read=%d total_written=%lu total_size=%lu pct=%lu",
                     n,
                     (unsigned long)g_job.bytesWritten,
                     (unsigned long)g_job.bytesTotal,
                     (unsigned long)pct);
            ota_logRuntimeHealth("write_chunk");
            g_job.lastWriteLogMs = nowWriteMs;
            g_job.bytesAtLastWriteLog = g_job.bytesWritten;
        }
#endif
    }

    uint32_t now = millis();

#if CFG_OTA_DEV_LOGS
    if (s_otaHeartbeatEnabled && (uint32_t)(now - g_job.lastDiagMs) >= (uint32_t)CFG_OTA_DOWNLOAD_HEARTBEAT_MS)
    {
        g_job.lastDiagMs = now;
        uint32_t pct = 255u;
        if (g_job.bytesTotal > 0)
        {
            pct = (g_job.bytesWritten * 100u) / g_job.bytesTotal;
            if (pct > 100u)
            {
                pct = 100u;
            }
        }
        LOG_INFO(LogDomain::OTA,
                 "OTA heartbeat progress=%lu%% bytes=%lu/%lu zero_reads=%u stream_connected=%s stream_avail=%d retries=%u/%u free_heap=%lu",
                 (unsigned long)pct,
                 (unsigned long)g_job.bytesWritten,
                 (unsigned long)g_job.bytesTotal,
                 (unsigned int)g_job.zeroReadStreak,
                 stream->connected() ? "true" : "false",
                 stream->available(),
                 (unsigned int)g_job.netRetryCount,
                 (unsigned int)CFG_OTA_HTTP_MAX_RETRIES,
                 (unsigned long)ESP.getFreeHeap());
    }
#endif

    if (state && (now - g_job.lastReportMs) >= 500u)
    {
        g_job.lastReportMs = now;

        if (g_job.bytesTotal > 0)
        {
            uint32_t pct = (g_job.bytesWritten * 100u) / g_job.bytesTotal;
            if (pct > 100u)
                pct = 100u;
            ota_setProgress(state, (uint8_t)pct);
        }
        else
        {
            ota_setProgress(state, 255);
        }
        ota_requestPublish();
    }

    bool finished = false;
    if (g_job.bytesTotal > 0)
    {
        finished = g_job.bytesWritten >= g_job.bytesTotal;
    }
    else
    {
        finished = !stream->connected() &&
                   stream->available() == 0 &&
                   g_job.zeroReadStreak > 0 &&
                   (now - g_job.noDataSinceMs) > 200u;
    }

    if (!finished)
    {
        if (g_job.updateBegun && g_job.lastProgressMs > 0 &&
            (now - g_job.lastProgressMs) > 60000u)
        {
            ota_trace("download_timeout", "idle_ms=%lu", (unsigned long)(now - g_job.lastProgressMs));
            ota_abort(state, "download_timeout");
            return;
        }
        return;
    }
    ota_progressPrint(g_job.bytesWritten, g_job.bytesTotal, true, true);
    ota_trace("download_complete", "bytes=%lu/%lu", (unsigned long)g_job.bytesWritten, (unsigned long)g_job.bytesTotal);
    ota_logRuntimeHealth("download_complete");

#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "OTA stream complete bytes_written=%lu bytes_total=%lu stream_connected=%s stream_avail=%d",
             (unsigned long)g_job.bytesWritten,
             (unsigned long)g_job.bytesTotal,
             stream->connected() ? "true" : "false",
             stream->available());
#endif

    // Step C: finalize update
    if (state)
    {
        ota_setStatus(state, OtaStatus::VERIFYING);
        ota_setFlat(state, "verifying", state->ota.progress, nullptr, nullptr, false);
        ota_requestPublish();
    }

    if (g_job.bytesWritten < OTA_MIN_BYTES)
    {
        ota_trace("finalize_fail", "download_too_small bytes=%lu", (unsigned long)g_job.bytesWritten);
        ota_abort(state, "download_too_small");
        return;
    }

    uint8_t digest[32];
    char hex[65];
    hex[64] = '\0';

    if (g_job.shaInit)
    {
        mbedtls_sha256_finish(&g_job.shaCtx, digest);
        mbedtls_sha256_free(&g_job.shaCtx);
        g_job.shaInit = false;

        static const char *kHex = "0123456789abcdef";
        for (int i = 0; i < 32; i++)
        {
            hex[i * 2] = kHex[(digest[i] >> 4) & 0xF];
            hex[i * 2 + 1] = kHex[digest[i] & 0xF];
        }

        if (g_job.sha256[0] == '\0')
        {
            ota_trace("sha_fail", "missing_sha256");
#if !CFG_OTA_DEV_LOGS
            LOG_ERROR(LogDomain::OTA, "Verifying update integrity... FAILED");
#endif
            ota_abort(state, "missing_sha256");
            return;
        }
        if (!isHex64(g_job.sha256))
        {
            ota_trace("sha_fail", "bad_sha256_format");
#if !CFG_OTA_DEV_LOGS
            LOG_ERROR(LogDomain::OTA, "Verifying update integrity... FAILED");
#endif
            ota_abort(state, "bad_sha256_format");
            return;
        }

        for (int i = 0; i < 64; i++)
        {
            if (lowerHexChar(g_job.sha256[i]) != hex[i])
            {
#if CFG_OTA_DEV_LOGS
                LOG_WARN(LogDomain::OTA, "Pull OTA SHA256 mismatch exp_prefix=%.12s got_prefix=%.12s",
                         g_job.sha256, hex);
#else
                LOG_ERROR(LogDomain::OTA, "Verifying update integrity... FAILED");
#endif
                ota_trace("sha_fail", "sha_mismatch");
                ota_abort(state, "sha_mismatch");
                return;
            }
        }
        ota_trace("sha_ok", "prefix=%.12s", hex);
#if CFG_OTA_DEV_LOGS
        LOG_INFO(LogDomain::OTA,
                 "Pull OTA SHA256 verify result=match expected_prefix=%.12s got_prefix=%.12s",
                 g_job.sha256,
                 hex);
        LOG_INFO(LogDomain::OTA, "Pull OTA SHA256 ok (prefix)=%c%c%c%c%c%c%c%c%c%c%c%c",
                 hex[0], hex[1], hex[2], hex[3], hex[4], hex[5], hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
#else
        LOG_INFO(LogDomain::OTA, "Verifying update integrity... OK");
#endif
    }

    if (state)
    {
        ota_setStatus(state, OtaStatus::APPLYING);
        ota_setFlat(state, "applying", state->ota.progress, nullptr, nullptr, false);
        ota_requestPublish();
    }

    const bool wdtDetachedEnd = ota_detachCurrentTaskWdt("update_end");
    const esp_err_t otaEndErr = esp_ota_end(g_job.otaHandle);
    ota_reattachCurrentTaskWdt(wdtDetachedEnd, "update_end");
    g_job.otaHandle = 0;
    g_job.updateBegun = false;
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA,
             "esp_ota_end err=%d bytes_written=%lu bytes_total=%lu free_heap=%lu",
             (int)otaEndErr,
             (unsigned long)g_job.bytesWritten,
             (unsigned long)g_job.bytesTotal,
             (unsigned long)ESP.getFreeHeap());
#endif
    ota_logPartitionSnapshot("after_update_end");
    const esp_partition_t *bootAfter = esp_ota_get_boot_partition();
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA, "Boot partition AFTER esp_ota_end: %s@0x%08lx",
             bootAfter ? bootAfter->label : "<null>",
             (unsigned long)(bootAfter ? bootAfter->address : 0));
#else
    (void)bootAfter;
#endif
    if (!ota_requireEspOk(state, "esp_ota_end", otaEndErr))
    {
        return;
    }
    ota_trace("ota_end_ok", "err=%d", (int)otaEndErr);
    ota_logRuntimeHealth("ota_end_ok");

    if (g_job.targetPartition == nullptr)
    {
        (void)ota_requireEspOk(state, "esp_ota_set_boot_partition_target", ESP_ERR_INVALID_ARG);
        return;
    }
    const esp_err_t setBootErr = esp_ota_set_boot_partition(g_job.targetPartition);
#if CFG_OTA_DEV_LOGS
    LOG_INFO(LogDomain::OTA, "esp_ota_set_boot_partition target=%s@0x%08lx err=%d",
             g_job.targetPartition->label,
             (unsigned long)g_job.targetPartition->address,
             (int)setBootErr);
#endif
    if (!ota_requireEspOk(state, "esp_ota_set_boot_partition", setBootErr))
    {
        return;
    }
    ota_trace("set_boot_ok", "target=%s@0x%08lx",
              g_job.targetPartition->label,
              (unsigned long)g_job.targetPartition->address);
    ota_logPartitionSnapshot("after_set_boot_partition");
    ota_emitPartitionDiag("after_set_boot_partition");

    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }

    ota_finishSuccess(state);
    return;
}
