#include "ota_service.h"
#include <WiFiClientSecure.h>
#include "ota_ca_cert.h"
#include "mbedtls/sha256.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "logger.h"
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include "domain_strings.h"
#include "storage_nvs.h"
#include "mqtt_transport.h"

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef OTA_MIN_BYTES
#define OTA_MIN_BYTES 1024
#endif
#ifndef CFG_OTA_MANIFEST_URL
#define CFG_OTA_MANIFEST_URL ""
#endif

static const char *s_hostName = nullptr;
static const char *s_password = nullptr;
static bool s_started = false;

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
    uint32_t bytesTotal = 0;
    uint32_t bytesWritten = 0;
    uint32_t noDataSinceMs = 0;
    uint8_t zeroReadStreak = 0;

    // Streaming objects
    HTTPClient http;
    WiFiClientSecure client;

    mbedtls_sha256_context shaCtx;
    bool shaInit = false;

    bool httpBegun = false;
    bool updateBegun = false;
};

static PullOtaJob g_job;

bool ota_isBusy()
{
    return g_job.active;
}

void ota_begin(const char *hostName, const char *password)
{
    s_hostName = hostName;
    s_password = password;
    s_started = false;
}

void ota_handle()
{
    if (g_job.active)
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
        if (s_password)
        {
            ArduinoOTA.setPassword(s_password);
        }

        ArduinoOTA.onStart([]()
                           { LOG_INFO(LogDomain::OTA, "Update started"); });

        ArduinoOTA.onEnd([]()
                         { LOG_INFO(LogDomain::OTA, "Update finished"); });

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
    if (!state)
        return;
    strncpy(state->ota.last_status, status ? status : "", sizeof(state->ota.last_status));
    state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';

    strncpy(state->ota.last_message, message ? message : "", sizeof(state->ota.last_message));
    state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';

    state->ota.completed_ts = (uint32_t)(millis() / 1000);
}

static inline void ota_requestPublish()
{
    mqtt_requestStatePublish();
}

static inline void ota_setProgress(DeviceState *state, uint8_t progress)
{
    if (!state)
        return;
    state->ota.progress = progress;
    state->ota_progress = progress;
}

static void ota_clearActive(DeviceState *state)
{
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
        state->ota_last_ts = (uint32_t)(millis() / 1000);
    }
}

static void ota_markFailed(DeviceState *state, const char *reason)
{
    if (!state)
        return;
    state->ota.status = OtaStatus::ERROR;
    ota_setResult(state, "error", reason ? reason : "error");
    ota_setFlat(state, "failed", 0, reason ? reason : "error", state->ota.version, true);
    ota_clearActive(state);
    ota_requestPublish();
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
    if (!state)
    {
        setErr(errBuf, errBufLen, "missing_state");
        return false;
    }

    if (g_job.active)
    {
        setErr(errBuf, errBufLen, "busy");
        return false;
    }

    if (!url || url[0] == '\0')
    {
        setErr(errBuf, errBufLen, "missing_url");
        return false;
    }

    // Enforce HTTPS for pull-OTA URLs
    if (strncasecmp(url, "https://", 8) != 0)
    {
        setErr(errBuf, errBufLen, "url_not_https");
        return false;
    }

    // Idempotency: if not forcing and version matches current, no-op success.
    if (!force && version && version[0] != '\0' && state->device.fw && strcmp(version, state->device.fw) == 0)
    {
        state->ota.status = OtaStatus::SUCCESS;
        ota_setResult(state, "success", "noop_already_on_version");
        ota_setFlat(state, "success", 100, "", version ? version : "", true);
        state->update_available = false;
        ota_requestPublish();
        setErr(errBuf, errBufLen, "noop");
        return true;
    }

    // Reset job state safely (do not memset C++ objects)
    if (g_job.updateBegun)
    {
        Update.abort();
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

    // Clear primitive fields / buffers
    g_job.active = false;
    g_job.reboot = true;
    g_job.force = false;
    g_job.lastProgressMs = 0;
    g_job.lastReportMs = 0;
    g_job.bytesTotal = 0;
    g_job.bytesWritten = 0;
    g_job.noDataSinceMs = 0;
    g_job.zeroReadStreak = 0;
    g_job.request_id[0] = '\0';
    g_job.version[0] = '\0';
    g_job.url[0] = '\0';
    g_job.sha256[0] = '\0';

    g_job.active = true;
    g_job.reboot = reboot;
    g_job.force = force;

    strncpy(g_job.request_id, request_id ? request_id : "", sizeof(g_job.request_id));
    g_job.request_id[sizeof(g_job.request_id) - 1] = '\0';
    strncpy(g_job.version, version ? version : "", sizeof(g_job.version));
    g_job.version[sizeof(g_job.version) - 1] = '\0';
    strncpy(g_job.url, url, sizeof(g_job.url));
    g_job.url[sizeof(g_job.url) - 1] = '\0';
    strncpy(g_job.sha256, sha256 ? sha256 : "", sizeof(g_job.sha256));
    g_job.sha256[sizeof(g_job.sha256) - 1] = '\0';

    // Mirror into device-owned state
    state->ota.status = OtaStatus::DOWNLOADING;
    strncpy(state->ota.request_id, g_job.request_id, sizeof(state->ota.request_id));
    strncpy(state->ota.version, g_job.version, sizeof(state->ota.version));
    strncpy(state->ota.url, g_job.url, sizeof(state->ota.url));
    strncpy(state->ota.sha256, g_job.sha256, sizeof(state->ota.sha256));
    state->ota.request_id[sizeof(state->ota.request_id) - 1] = '\0';
    state->ota.version[sizeof(state->ota.version) - 1] = '\0';
    state->ota.url[sizeof(state->ota.url) - 1] = '\0';
    state->ota.sha256[sizeof(state->ota.sha256) - 1] = '\0';
    state->ota.started_ts = (uint32_t)(millis() / 1000);

    // Clear last result fields for new attempt
    state->ota.last_status[0] = '\0';
    state->ota.last_message[0] = '\0';
    state->ota.completed_ts = 0;
    ota_setFlat(state, "downloading", 0, "", g_job.version, true);
    state->update_available = (g_job.version[0] != '\0' && state->device.fw && strcmp(g_job.version, state->device.fw) != 0);

    setErr(errBuf, errBufLen, "");
    LOG_INFO(LogDomain::OTA, "Pull OTA queued url=%s version=%s", g_job.url, g_job.version);
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

    if (g_job.active)
    {
        setErr(errBuf, errBufLen, "busy");
        return false;
    }

    if (!WiFi.isConnected())
    {
        setErr(errBuf, errBufLen, "wifi_disconnected");
        ota_markFailed(state, "wifi_disconnected");
        return false;
    }

    const char *manifestUrl = CFG_OTA_MANIFEST_URL;
    if (!manifestUrl || manifestUrl[0] == '\0')
    {
        setErr(errBuf, errBufLen, "missing_manifest_url");
        ota_markFailed(state, "missing_manifest_url");
        return false;
    }
    if (strncasecmp(manifestUrl, "https://", 8) != 0)
    {
        setErr(errBuf, errBufLen, "manifest_url_not_https");
        ota_markFailed(state, "manifest_url_not_https");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(OTA_GITHUB_CA);
    client.setTimeout(12000);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(5);

    if (!http.begin(client, manifestUrl))
    {
        setErr(errBuf, errBufLen, "manifest_http_begin_failed");
        ota_markFailed(state, "manifest_http_begin_failed");
        return false;
    }

    static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
    http.collectHeaders(kHeaders, 3);
    http.setUserAgent("DadsSmartHomeWaterTank/1.0");
    http.addHeader("Accept", "application/json");
    http.useHTTP10(false);

    const int code = http.GET();
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
    if (g_job.active)
    {
        setErr(errBuf, errBufLen, "busy");
        return false;
    }
    if (!WiFi.isConnected())
    {
        setErr(errBuf, errBufLen, "wifi_disconnected");
        return false;
    }

    const char *manifestUrl = CFG_OTA_MANIFEST_URL;
    if (!manifestUrl || manifestUrl[0] == '\0')
    {
        setErr(errBuf, errBufLen, "missing_manifest_url");
        return false;
    }
    if (strncasecmp(manifestUrl, "https://", 8) != 0)
    {
        setErr(errBuf, errBufLen, "manifest_url_not_https");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(OTA_GITHUB_CA);
    client.setTimeout(12000);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setRedirectLimit(5);

    if (!http.begin(client, manifestUrl))
    {
        setErr(errBuf, errBufLen, "manifest_http_begin_failed");
        return false;
    }

    static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
    http.collectHeaders(kHeaders, 3);
    http.setUserAgent("DadsSmartHomeWaterTank/1.0");
    http.addHeader("Accept", "application/json");
    http.useHTTP10(false);

    const int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        char msg[32];
        snprintf(msg, sizeof(msg), "manifest_http_%d", code);
        http.end();
        setErr(errBuf, errBufLen, msg);
        return false;
    }

    StaticJsonDocument<768> doc;
    const DeserializationError derr = deserializeJson(doc, http.getStream());
    http.end();
    if (derr)
    {
        setErr(errBuf, errBufLen, "manifest_parse_failed");
        return false;
    }

    const char *version = doc["version"] | "";
    const char *url = doc["url"] | "";
    const char *sha256 = doc["sha256"] | "";
    if (!version || version[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_version");
        return false;
    }
    if (!url || url[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_url");
        return false;
    }
    if (!sha256 || sha256[0] == '\0')
    {
        setErr(errBuf, errBufLen, "manifest_missing_sha256");
        return false;
    }
    if (!isHex64(sha256))
    {
        setErr(errBuf, errBufLen, "bad_sha256_format");
        return false;
    }

    strncpy(state->ota_target_version, version, sizeof(state->ota_target_version));
    state->ota_target_version[sizeof(state->ota_target_version) - 1] = '\0';
    state->ota_last_ts = (uint32_t)(millis() / 1000);
    state->update_available = (state->device.fw && strcmp(version, state->device.fw) != 0);
    state->ota_error[0] = '\0';

    setErr(errBuf, errBufLen, "");
    return true;
}

static void ota_abort(DeviceState *state, const char *reason)
{
    if (state)
    {
        state->ota.status = OtaStatus::ERROR;
        ota_setResult(state, "error", reason ? reason : "error");
        ota_setFlat(state, "failed", state->ota.progress, reason ? reason : "error", state->ota.version, true);
        ota_clearActive(state);
        ota_requestPublish();
    }

    if (g_job.updateBegun)
    {
        Update.abort();
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
    LOG_WARN(LogDomain::OTA, "Pull OTA aborted reason=%s", reason ? reason : "");
}

static void ota_finishSuccess(DeviceState *state)
{
    if (state)
    {
        state->ota.status = OtaStatus::SUCCESS;
        ota_setResult(state, "success", "applied");
        ota_setFlat(state, "success", 100, "", state->ota.version, true);
        state->update_available = false;
        const uint32_t epochNow = ota_epochNow();
        if (epochNow > 0)
        {
            state->ota_last_success_ts = epochNow;
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
    LOG_INFO(LogDomain::OTA, "Pull OTA success");

    if (g_job.reboot)
    {
        if (state)
        {
            state->ota.status = OtaStatus::REBOOTING;
        }
        delay(250);
        ESP.restart();
    }
}

void ota_tick(DeviceState *state)
{
    if (!g_job.active)
        return;

    if (!WiFi.isConnected())
    {
        ota_abort(state, "wifi_disconnected");
        return;
    }

    // Step A: begin HTTP if not begun
    if (!g_job.httpBegun)
    {
        static constexpr uint32_t kHandshakeTimeoutMs = 20000;
        // Setup secure client with CA bundle
        g_job.client.setCACert(OTA_GITHUB_CA);
        g_job.client.setTimeout(12000); // 12s timeout for handshake and read

        // Follow GitHub redirects (releases/download -> objects.githubusercontent.com)
        g_job.http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        g_job.http.setRedirectLimit(5);

        // HTTPS: WiFiClientSecure + CA bundle + redirect-following (GitHub releases).
        const uint32_t hsStartMs = millis();
        bool ok = g_job.http.begin(g_job.client, g_job.url);
        const uint32_t hsElapsedMs = millis() - hsStartMs;
        if (ok)
        {
            g_job.httpBegun = true;
        }
        if (hsElapsedMs > kHandshakeTimeoutMs)
        {
            ota_abort(state, "http_handshake_timeout");
            return;
        }
        if (!ok)
        {
            ota_abort(state, "http_begin_failed");
            return;
        }

        static const char *kHeaders[] = {"Content-Type", "Content-Length", "Location"};
        g_job.http.collectHeaders(kHeaders, 3);

        g_job.http.setUserAgent("DadsSmartHomeWaterTank/1.0");
        g_job.http.addHeader("Accept", "application/octet-stream");
        g_job.http.useHTTP10(false);

        int code = g_job.http.GET();
        if (code != HTTP_CODE_OK)
        {
            char msg[32];
            snprintf(msg, sizeof(msg), "http_%d", code);
            ota_abort(state, msg);
            return;
        }

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
        }

        int len = g_job.http.getSize();
        LOG_INFO(LogDomain::OTA, "HTTP %d len=%d ctype=%s", code, len, ctype.c_str());
        if (len > 0 && len < OTA_MIN_BYTES)
        {
            ota_abort(state, "content_too_small");
            return;
        }
        if (len <= 0)
        {
            // Some servers do chunked transfer; Update can still work.
            // We'll treat unknown length as 0 and still proceed.
            len = 0;
        }
        g_job.bytesTotal = (uint32_t)len;
        g_job.bytesWritten = 0;

        // Begin update
        Update.setMD5(nullptr);
        if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN, U_FLASH))
        {
            ota_abort(state, "update_begin_failed");
            return;
        }
        mbedtls_sha256_init(&g_job.shaCtx);
        if (mbedtls_sha256_starts(&g_job.shaCtx, 0) != 0)
        {
            ota_abort(state, "sha_init_failed");
            return;
        }
        g_job.shaInit = true;
        g_job.updateBegun = true;

        if (state)
        {
            state->ota.status = OtaStatus::DOWNLOADING;
            ota_setFlat(state, "downloading", 0, "", nullptr, false);
            ota_requestPublish();
        }

        g_job.lastProgressMs = millis();
        g_job.lastReportMs = g_job.lastProgressMs;
        g_job.zeroReadStreak = 0;
        g_job.noDataSinceMs = 0;
        return; // next tick will stream
    }

    // Step B: stream a bounded chunk per tick
    WiFiClient *stream = g_job.http.getStreamPtr();
    if (!stream)
    {
        ota_abort(state, "no_stream");
        return;
    }

    const size_t kMaxChunk = 4096; // cap work per tick to keep MQTT alive
    uint8_t buf[512];
    size_t processed = 0;

    while (processed < kMaxChunk)
    {
        int avail = stream->available();
        if (avail <= 0)
        {
            // No data right now. If finished, finalize.
            if (g_job.zeroReadStreak == 0)
                g_job.noDataSinceMs = millis();
            g_job.zeroReadStreak++;
            break;
        }

        size_t toRead = (size_t)avail;
        if (toRead > sizeof(buf))
            toRead = sizeof(buf);

        int n = stream->readBytes(buf, toRead);
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

        size_t written = Update.write(buf, (size_t)n);
        if (written != (size_t)n)
        {
            ota_abort(state, "flash_write_failed");
            return;
        }

        g_job.zeroReadStreak = 0;
        g_job.noDataSinceMs = 0;
        g_job.bytesWritten += (uint32_t)written;
        processed += written;
        g_job.lastProgressMs = millis();
    }

    // Update progress every ~500ms
    uint32_t now = millis();
    if (state && (now - g_job.lastReportMs) >= 500)
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
            // unknown total size
            ota_setProgress(state, 255); // use 255 to indicate indeterminate progress
        }
        ota_requestPublish();
    }

    // Detect end of download
    bool finished = false;

    if (g_job.bytesTotal > 0)
    {
        finished = g_job.bytesWritten >= g_job.bytesTotal;
    }
    else
    {
        // If server closes connection, available() stays 0 and connected() becomes false
        finished = !stream->connected() &&
                   stream->available() == 0 &&
                   g_job.zeroReadStreak > 0 &&
                   (now - g_job.noDataSinceMs) > 200;
    }

    if (!finished)
    {
        if (g_job.updateBegun && g_job.lastProgressMs > 0 &&
            (now - g_job.lastProgressMs) > 60000)
        {
            ota_abort(state, "download_timeout");
            return;
        }
        return;
    }

    // Step C: finalize update
    if (state)
    {
        state->ota.status = OtaStatus::VERIFYING;
        ota_setFlat(state, "verifying", state->ota.progress, nullptr, nullptr, false);
        ota_requestPublish();
    }

    if (g_job.bytesWritten < OTA_MIN_BYTES)
    {
        ota_abort(state, "download_too_small");
        return;
    }

    // finalize sha
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

        // compare (expected must be exactly 64 hex chars)
        if (g_job.sha256[0] == '\0')
        {
            ota_abort(state, "missing_sha256");
            return;
        }
        if (!isHex64(g_job.sha256))
        {
            ota_abort(state, "bad_sha256_format");
            return;
        }

        for (int i = 0; i < 64; i++)
        {
            if (lowerHexChar(g_job.sha256[i]) != hex[i])
            {
                LOG_WARN(LogDomain::OTA, "Pull OTA SHA256 mismatch exp_prefix=%.12s got_prefix=%.12s",
                         g_job.sha256, hex);
                ota_abort(state, "sha256_mismatch");
                return;
            }
        }
        LOG_INFO(LogDomain::OTA, "Pull OTA SHA256 ok (prefix)=%c%c%c%c%c%c%c%c%c%c%c%c",
                 hex[0], hex[1], hex[2], hex[3], hex[4], hex[5], hex[6], hex[7], hex[8], hex[9], hex[10], hex[11]);
    }

    if (state)
    {
        state->ota.status = OtaStatus::APPLYING;
        ota_setFlat(state, "applying", state->ota.progress, nullptr, nullptr, false);
        ota_requestPublish();
    }

    bool okEnd = Update.end(true);
    if (!okEnd)
    {
        char msg[40];
        snprintf(msg, sizeof(msg), "update_end_failed_%u", (unsigned int)Update.getError());
        ota_abort(state, msg);
        return;
    }

    // Close http
    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }
    g_job.updateBegun = false;

    ota_finishSuccess(state);
}
