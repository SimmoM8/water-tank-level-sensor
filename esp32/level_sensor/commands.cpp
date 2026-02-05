#include "commands.h"
#include <ArduinoJson.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <WiFi.h>
#include "logger.h"

#include "device_state.h"
#include "domain_strings.h"
#include "ota_service.h"

#ifndef CMD_SCHEMA_VERSION
#define CMD_SCHEMA_VERSION 1
#endif

static CommandsContext s_ctx{};

// storage for last_cmd strings so pointers remain valid
static char s_reqId[40] = "";
static char s_type[24] = "";
static char s_msg[64] = "";

static void setLastCmd(const char *reqId, const char *type, CmdStatus st, const char *msg)
{
    if (s_ctx.state == nullptr)
    {
        return;
    }

    strncpy(s_reqId, reqId ? reqId : "", sizeof(s_reqId));
    s_reqId[sizeof(s_reqId) - 1] = 0;

    strncpy(s_type, type ? type : "", sizeof(s_type));
    s_type[sizeof(s_type) - 1] = 0;

    strncpy(s_msg, msg ? msg : "", sizeof(s_msg));
    s_msg[sizeof(s_msg) - 1] = 0;

    s_ctx.state->lastCmd.requestId = s_reqId;
    s_ctx.state->lastCmd.type = s_type;
    s_ctx.state->lastCmd.status = st;
    s_ctx.state->lastCmd.message = s_msg;
    s_ctx.state->lastCmd.ts = (uint32_t)(millis() / 1000);
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

static void appendChange(char *buf, size_t bufSize, const char *fmt, ...)
{
    if (!buf || bufSize == 0)
        return;
    size_t len = strnlen(buf, bufSize);
    if (len >= bufSize - 1)
        return;
    if (len > 0)
    {
        buf[len++] = ',';
        if (len >= bufSize - 1)
        {
            buf[bufSize - 1] = '\0';
            return;
        }
    }

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf + len, bufSize - len, fmt, args);
    va_end(args);
    buf[bufSize - 1] = '\0';
}

static void buildAutoRequestId(char *buf, size_t len)
{
    if (!buf || len == 0)
        return;
    const uint32_t now = (uint32_t)millis();
    const uint32_t rand16 = (uint32_t)random(0, 0xFFFF);
    snprintf(buf, len, "auto_%08lx_%04lx", (unsigned long)now, (unsigned long)rand16);
    buf[len - 1] = '\0';
}

static void finish(const char *reqId, const char *type, CmdStatus st, const char *msg)
{
    setLastCmd(reqId, type, st, msg);
    if (s_ctx.publishAck)
    {
        const char *stStr = toString(st);
        s_ctx.publishAck(reqId ? reqId : "", type ? type : "", stStr ? stStr : "", msg ? msg : "");
    }
    if (s_ctx.requestStatePublish)
    {
        s_ctx.requestStatePublish();
    }
}

static void handleSetConfig(JsonObject data, const char *requestId)
{
    bool appliedAny = false;
    char changes[96] = "";

    if (data.containsKey("tank_volume_l") && s_ctx.updateTankVolume)
    {
        float v = data["tank_volume_l"].as<float>();
        s_ctx.updateTankVolume(v, true);
        appliedAny = true;
        appendChange(changes, sizeof(changes), "tank_volume_l=%.2f", v);
    }
    if (data.containsKey("rod_length_cm") && s_ctx.updateRodLength)
    {
        float v = data["rod_length_cm"].as<float>();
        s_ctx.updateRodLength(v, true);
        appliedAny = true;
        appendChange(changes, sizeof(changes), "rod_length_cm=%.2f", v);
    }

    finish(requestId, "set_config",
           appliedAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
           appliedAny ? "applied" : "invalid_fields");

    if (appliedAny)
    {
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=set_config request_id=%s changes=%s",
                 requestId ? requestId : "", changes[0] ? changes : "none");
    }
    else
    {
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=no_fields type=set_config");
    }
}

static void handleCalibrate(JsonObject data, const char *requestId)
{
    const char *point = data["point"] | "";
    if (!s_ctx.captureCalibrationPoint)
    {
        finish(requestId, "calibrate", CmdStatus::ERROR, "missing_callback");
        return;
    }

    if (strcmp(point, "dry") == 0)
    {
        s_ctx.captureCalibrationPoint(true);
        finish(requestId, "calibrate", CmdStatus::APPLIED, "dry");
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=calibrate request_id=%s changes=point=dry", requestId ? requestId : "");
        return;
    }
    if (strcmp(point, "wet") == 0)
    {
        s_ctx.captureCalibrationPoint(false);
        finish(requestId, "calibrate", CmdStatus::APPLIED, "wet");
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=calibrate request_id=%s changes=point=wet", requestId ? requestId : "");
        return;
    }

    finish(requestId, "calibrate", CmdStatus::REJECTED, "invalid_point");
    LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=invalid_point type=calibrate");
}

static void handleClearCalibration(const char *requestId)
{
    if (s_ctx.clearCalibration)
    {
        s_ctx.clearCalibration();
        finish(requestId, "clear_calibration", CmdStatus::APPLIED, "cleared");
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=clear_calibration request_id=%s changes=cleared", requestId ? requestId : "");
    }
    else
    {
        finish(requestId, "clear_calibration", CmdStatus::ERROR, "missing_callback");
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=missing_callback type=clear_calibration");
    }
}

static void handleWipeWifi(const char *requestId)
{
    if (s_ctx.wipeWifiCredentials)
    {
        finish(requestId, "wipe_wifi", CmdStatus::APPLIED, "rebooting");
        LOG_WARN(LogDomain::COMMAND, "Applied cmd type=wipe_wifi request_id=%s changes=wipe_wifi", requestId ? requestId : "");
        s_ctx.wipeWifiCredentials();
    }
    else
    {
        finish(requestId, "wipe_wifi", CmdStatus::ERROR, "missing_callback");
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=missing_callback type=wipe_wifi");
    }
}

static void handleSetCalibration(JsonObject data, const char *requestId)
{
    bool okAny = false;
    char changes[96] = "";

    if (data.containsKey("cal_dry_set") && s_ctx.setCalibrationDryValue)
    {
        int32_t v = data["cal_dry_set"].as<int32_t>();
        s_ctx.setCalibrationDryValue(v, "cmd");
        okAny = true;
        appendChange(changes, sizeof(changes), "dry=%ld", (long)v);
    }
    if (data.containsKey("cal_wet_set") && s_ctx.setCalibrationWetValue)
    {
        int32_t v = data["cal_wet_set"].as<int32_t>();
        s_ctx.setCalibrationWetValue(v, "cmd");
        okAny = true;
        appendChange(changes, sizeof(changes), "wet=%ld", (long)v);
    }

    finish(requestId, "set_calibration",
           okAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
           okAny ? "applied" : "invalid_fields");

    if (okAny)
    {
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=set_calibration request_id=%s changes=%s",
                 requestId ? requestId : "", changes[0] ? changes : "none");
    }
    else
    {
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=no_fields type=set_calibration");
    }
}

static void handleOtaPull(JsonObject data, const char *requestId)
{
    char err[48] = {0};

    if (ota_isBusy())
    {
        finish(requestId, "ota_pull", CmdStatus::REJECTED, "busy");
        LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=busy", requestId ? requestId : "");
        return;
    }
    if (!WiFi.isConnected())
    {
        finish(requestId, "ota_pull", CmdStatus::REJECTED, "wifi_disconnected");
        LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=wifi_disconnected", requestId ? requestId : "");
        return;
    }

    const char *version = data["version"] | "";
    const char *url = data["url"] | "";
    const char *sha256 = data["sha256"] | "";

    const bool rebootDefault = true;
    const bool forceDefault = false;
    bool reboot = data["reboot"].is<bool>() ? data["reboot"].as<bool>() : rebootDefault;
    bool force = data["force"].is<bool>() ? data["force"].as<bool>() : forceDefault;

    const bool hasUrl = url && url[0] != '\0';
    const bool hasSha = sha256 && sha256[0] != '\0';
    const bool hasVersion = version && version[0] != '\0';
    const bool useManifest = (!hasUrl || !hasSha) && (force || (!hasUrl && !hasSha && !hasVersion));

    bool ok = false;
    if (useManifest)
    {
        ok = ota_pullStartFromManifest(s_ctx.state, requestId, force, reboot, err, sizeof(err));
    }
    else
    {
        if (!hasUrl)
        {
            finish(requestId, "ota_pull", CmdStatus::REJECTED, "missing_url");
            LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=missing_url", requestId ? requestId : "");
            return;
        }
        if (!hasSha)
        {
            finish(requestId, "ota_pull", CmdStatus::REJECTED, "missing_sha256");
            LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=missing_sha256", requestId ? requestId : "");
            return;
        }
        if (!isHex64(sha256))
        {
            finish(requestId, "ota_pull", CmdStatus::REJECTED, "bad_sha256_format");
            LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=bad_sha256_format", requestId ? requestId : "");
            return;
        }
        ok = ota_pullStart(s_ctx.state, requestId, version, url, sha256, force, reboot, err, sizeof(err));
    }
    if (!ok)
    {
        const char *reason = err[0] ? err : "start_failed";
        finish(requestId, "ota_pull", CmdStatus::REJECTED, reason);
        LOG_WARN(LogDomain::COMMAND, "OTA pull rejected request_id=%s reason=%s", requestId ? requestId : "", reason);
        return;
    }

    finish(requestId, "ota_pull", CmdStatus::APPLIED, "queued");
    LOG_INFO(LogDomain::COMMAND, "OTA pull accepted request_id=%s reason=queued", requestId ? requestId : "");
}

static SenseMode parseSenseMode(JsonVariant value)
{
    if (value.is<const char *>())
    {
        const char *s = value.as<const char *>();
        if (strcasecmp(s, "sim") == 0)
        {
            return SenseMode::SIM;
        }
        return SenseMode::TOUCH;
    }

    if (value.is<int>())
    {
        int v = value.as<int>();
        return v == 1 ? SenseMode::SIM : SenseMode::TOUCH;
    }

    return SenseMode::TOUCH;
}

static void handleSetSimulation(JsonObject data, const char *requestId)
{
    bool okAny = false;
    bool appliedSense = false;
    bool appliedMode = false;
    char changes[96] = "";

    const bool handlerSense = s_ctx.setSenseMode != nullptr;
    const bool handlerMode = s_ctx.setSimulationModeInternal != nullptr;

    const bool hasSense = data.containsKey("sense_mode");
    const bool hasMode = data.containsKey("mode");
    const char *senseVal = hasSense ? data["sense_mode"].as<const char *>() : "";
    const int modeVal = hasMode ? data["mode"].as<int>() : -1;

    LOG_INFO(LogDomain::COMMAND, "Command applied type=set_simulation request_id=%s sense_mode=%s mode=%d",
             requestId ? requestId : "",
             hasSense ? (senseVal ? senseVal : "(null)") : "(none)",
             modeVal);

    if (data.containsKey("sense_mode") && handlerSense)
    {
        SenseMode mode = parseSenseMode(data["sense_mode"]);
        s_ctx.setSenseMode(mode, true, "cmd");
        okAny = true;
        appliedSense = true;
        appendChange(changes, sizeof(changes), "sense_mode=%s", mode == SenseMode::SIM ? "sim" : "touch");
    }
    if (data.containsKey("mode") && handlerMode)
    {
        int m = data["mode"].as<int>();
        if (m < 0)
            m = 0;
        if (m > 5)
            m = 5;
        s_ctx.setSimulationModeInternal((uint8_t)m, true, "cmd");
        okAny = true;
        appliedMode = true;
        appendChange(changes, sizeof(changes), "mode=%d", m);
    }

    if (!handlerSense && hasSense)
    {
        finish(requestId, "set_simulation", CmdStatus::ERROR, "no_handler");
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=no_handler type=set_simulation");
        return;
    }
    if (!handlerMode && hasMode)
    {
        finish(requestId, "set_simulation", CmdStatus::ERROR, "no_handler");
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=no_handler type=set_simulation");
        return;
    }

    finish(requestId, "set_simulation",
           okAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
           okAny ? "applied" : "invalid_fields");

    if (okAny)
    {
        LOG_INFO(LogDomain::COMMAND, "Applied cmd type=set_simulation request_id=%s changes=%s",
                 requestId ? requestId : "", changes[0] ? changes : "none");
        if (appliedMode)
        {
            LOG_INFO(LogDomain::COMMAND, "Persisted simulation_mode to NVS");
        }
        if (appliedSense)
        {
            LOG_INFO(LogDomain::COMMAND, "Persisted sense_mode to NVS");
        }
    }
    else
    {
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=invalid_fields type=set_simulation");
    }
}

static void handleReannounce(const char *requestId)
{
    if (s_ctx.reannounce)
    {
        s_ctx.reannounce();
        finish(requestId, "reannounce", CmdStatus::APPLIED, "reannounced");
    }
    else
    {
        finish(requestId, "reannounce", CmdStatus::ERROR, "missing_callback");
    }
}

void commands_begin(const CommandsContext &ctx)
{
    s_ctx = ctx;
}

void commands_handle(const uint8_t *payload, size_t len)
{
    // Trim trailing nulls + whitespace (HA sometimes appends \n, and buffers can contain \0)

    static char json[4096];

    if (len == 0 || len >= sizeof(json))
    {
        finish("", "unknown", CmdStatus::REJECTED, "invalid_json");
        return;
    }

    memcpy(json, payload, len);
    json[len] = '\0';

    // Try to parse incoming JSON payload
    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, json);

    bool hasNull = false;
    for (size_t i = 0; i < len; i++)
    {
        if (json[i] == 0)
        {
            hasNull = true;
            break;
        }
    }

    LOG_DEBUG(LogDomain::COMMAND, "[CMD] len=%u json_first=0x%02X json_last=0x%02X hasNull=%s",
              (unsigned)len, (unsigned)(uint8_t)json[0], (unsigned)(uint8_t)json[len - 1], hasNull ? "true" : "false");

    if (err)
    {
        LOG_WARN(LogDomain::COMMAND, "Command rejected: invalid_json err=%s payload='%s'", err.c_str(), json);
        finish("", "unknown", CmdStatus::REJECTED, "invalid_json");
        return;
    }

    // Extract mandatory fields
    const int schema = doc["schema"] | 0;
    const char *requestId = doc["request_id"] | "";
    const char *type = doc["type"] | "";

    // Validate schema version and command type
    if (schema != CMD_SCHEMA_VERSION || strlen(type) == 0)
    {
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=invalid_schema_or_type type=%s", (type && strlen(type) ? type : "(none)"));
        finish(requestId, type, CmdStatus::REJECTED, "invalid_schema_or_type");
        return;
    }

    char autoReqId[32] = {0};
    if (!requestId || requestId[0] == '\0')
    {
        if (strcmp(type, "ota_pull") == 0)
        {
            buildAutoRequestId(autoReqId, sizeof(autoReqId));
            requestId = autoReqId;
        }
        else
        {
            finish("", type, CmdStatus::REJECTED, "missing_request_id");
            return;
        }
    }

    // Mark command as received
    setLastCmd(requestId, type, CmdStatus::RECEIVED, "received");

    // Extract optional data object
    const bool hasDataObj = doc["data"].is<JsonObject>();
    JsonObject data;
    if (hasDataObj)
    {
        data = doc["data"].as<JsonObject>();
    }

    LOG_INFO(LogDomain::COMMAND, "Command received type=%s request_id=%s", type ? type : "", requestId ? requestId : "");

    // Dispatch to the appropriate handler
    if (strcmp(type, "set_config") == 0)
    {
        if (!hasDataObj)
        {
            finish(requestId, type, CmdStatus::REJECTED, "missing_data");
            return;
        }
        handleSetConfig(data, requestId);
    }
    else if (strcmp(type, "set_calibration") == 0)
    {
        if (!hasDataObj)
        {
            finish(requestId, type, CmdStatus::REJECTED, "missing_data");
            return;
        }
        handleSetCalibration(data, requestId);
    }
    else if (strcmp(type, "calibrate") == 0)
    {
        if (!hasDataObj)
        {
            finish(requestId, type, CmdStatus::REJECTED, "missing_data");
            return;
        }
        handleCalibrate(data, requestId);
    }
    else if (strcmp(type, "clear_calibration") == 0)
    {
        handleClearCalibration(requestId);
    }
    else if (strcmp(type, "wipe_wifi") == 0)
    {
        handleWipeWifi(requestId);
    }
    else if (strcmp(type, "set_simulation") == 0)
    {
        if (!hasDataObj)
        {
            finish(requestId, type, CmdStatus::REJECTED, "missing_data");
            return;
        }
        handleSetSimulation(data, requestId);
    }
    else if (strcmp(type, "reannounce") == 0)
    {
        handleReannounce(requestId);
    }
    else if (strcmp(type, "ota_pull") == 0)
    {
        if (!hasDataObj)
        {
            finish(requestId, type, CmdStatus::REJECTED, "missing_data");
            return;
        }
        handleOtaPull(data, requestId);
    }
    else
    {
        // Unknown or unsupported command
        LOG_WARN(LogDomain::COMMAND, "Command rejected: reason=unknown_type type=%s", type ? type : "(null)");
        finish(requestId, type, CmdStatus::REJECTED, "unknown_type");
    }
}
