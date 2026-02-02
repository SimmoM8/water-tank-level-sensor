#include "ota_service.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include "logger.h"
#include <HTTPClient.h>
#include <Update.h>
#include "domain_strings.h"

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
    uint32_t bytesTotal = 0;
    uint32_t bytesWritten = 0;

    // Streaming objects
    HTTPClient http;
    WiFiClient client;

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

    // Idempotency: if not forcing and version matches current, no-op success.
    if (!force && version && version[0] != '\0' && state->device.fw && strcmp(version, state->device.fw) == 0)
    {
        state->ota.status = OtaStatus::SUCCESS;
        state->ota.progress = 100;
        ota_setResult(state, "success", "noop_already_on_version");
        setErr(errBuf, errBufLen, "noop");
        return true;
    }

    // Initialize job
    memset(&g_job, 0, sizeof(g_job));
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
    state->ota.progress = 0;
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

    setErr(errBuf, errBufLen, "");
    LOG_INFO(LogDomain::OTA, "Pull OTA queued url=%s version=%s", g_job.url, g_job.version);
    return true;
}

static void ota_abort(DeviceState *state, const char *reason)
{
    if (state)
    {
        state->ota.status = OtaStatus::ERROR;
        ota_setResult(state, "error", reason ? reason : "error");
    }

    if (g_job.httpBegun)
    {
        g_job.http.end();
        g_job.httpBegun = false;
    }
    if (g_job.updateBegun)
    {
        Update.abort();
        g_job.updateBegun = false;
    }

    g_job.active = false;
    LOG_WARN(LogDomain::OTA, "Pull OTA aborted reason=%s", reason ? reason : "");
}

static void ota_finishSuccess(DeviceState *state)
{
    if (state)
    {
        state->ota.status = OtaStatus::SUCCESS;
        state->ota.progress = 100;
        ota_setResult(state, "success", "applied");
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
        // NOTE: HTTP only (v1). HTTPS requires WiFiClientSecure handling.
        bool ok = g_job.http.begin(g_job.client, g_job.url);
        if (!ok)
        {
            ota_abort(state, "http_begin_failed");
            return;
        }
        g_job.httpBegun = true;

        int code = g_job.http.GET();
        if (code != HTTP_CODE_OK)
        {
            char msg[32];
            snprintf(msg, sizeof(msg), "http_%d", code);
            ota_abort(state, msg);
            return;
        }

        int len = g_job.http.getSize();
        if (len <= 0)
        {
            // Some servers do chunked transfer; Update can still work.
            // We'll treat unknown length as 0 and still proceed.
            len = 0;
        }
        g_job.bytesTotal = (uint32_t)len;
        g_job.bytesWritten = 0;

        // Begin update
        if (!Update.begin(len > 0 ? (size_t)len : UPDATE_SIZE_UNKNOWN))
        {
            ota_abort(state, "update_begin_failed");
            return;
        }
        g_job.updateBegun = true;

        if (state)
        {
            state->ota.status = OtaStatus::DOWNLOADING;
            state->ota.progress = 0;
        }

        g_job.lastProgressMs = millis();
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
            break;
        }

        size_t toRead = (size_t)avail;
        if (toRead > sizeof(buf))
            toRead = sizeof(buf);

        int n = stream->readBytes(buf, toRead);
        if (n <= 0)
            break;

        size_t written = Update.write(buf, (size_t)n);
        if (written != (size_t)n)
        {
            ota_abort(state, "flash_write_failed");
            return;
        }

        g_job.bytesWritten += (uint32_t)written;
        processed += written;
    }

    // Update progress every ~500ms
    uint32_t now = millis();
    if (state && (now - g_job.lastProgressMs) >= 500)
    {
        g_job.lastProgressMs = now;

        if (g_job.bytesTotal > 0)
        {
            uint32_t pct = (g_job.bytesWritten * 100u) / g_job.bytesTotal;
            if (pct > 100u)
                pct = 100u;
            state->ota.progress = (uint8_t)pct;
        }
        else
        {
            // unknown total size
            state->ota.progress = 0;
        }
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
        finished = !stream->connected() && stream->available() == 0;
    }

    if (!finished)
        return;

    // Step C: finalize update
    if (state)
    {
        state->ota.status = OtaStatus::APPLYING;
    }

    bool okEnd = Update.end(true);
    if (!okEnd)
    {
        ota_abort(state, "update_end_failed");
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