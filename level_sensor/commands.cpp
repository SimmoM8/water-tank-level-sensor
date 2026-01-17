#include "commands.h"
#include <ArduinoJson.h>
#include <string.h>

#include "device_state.h"

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
    s_ctx.state->lastCmd.ts = s_ctx.state->ts; // align to latest state snapshot time
}

static void finish(const char *reqId, const char *type, CmdStatus st, const char *msg)
{
    setLastCmd(reqId, type, st, msg);
    if (s_ctx.publishAck)
    {
        s_ctx.publishAck(reqId ? reqId : "", type ? type : "", st, msg ? msg : "");
    }
    if (s_ctx.requestStatePublish)
    {
        s_ctx.requestStatePublish();
    }
}

static void handleSetConfig(JsonObject data, const char *requestId)
{
    bool appliedAny = false;

    if (data.containsKey("tank_volume_l") && s_ctx.updateTankVolume)
    {
        float v = data["tank_volume_l"].as<float>();
        s_ctx.updateTankVolume(v, true);
        appliedAny = true;
    }
    if (data.containsKey("rod_length_cm") && s_ctx.updateRodLength)
    {
        float v = data["rod_length_cm"].as<float>();
        s_ctx.updateRodLength(v, true);
        appliedAny = true;
    }

    finish(requestId, "set_config",
           appliedAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
           appliedAny ? "applied" : "no_fields");
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
        return;
    }
    if (strcmp(point, "wet") == 0)
    {
        s_ctx.captureCalibrationPoint(false);
        finish(requestId, "calibrate", CmdStatus::APPLIED, "wet");
        return;
    }

    finish(requestId, "calibrate", CmdStatus::REJECTED, "invalid_point");
}

static void handleClearCalibration(const char *requestId)
{
    if (s_ctx.clearCalibration)
    {
        s_ctx.clearCalibration();
        finish(requestId, "clear_calibration", CmdStatus::APPLIED, "cleared");
    }
    else
    {
        finish(requestId, "clear_calibration", CmdStatus::ERROR, "missing_callback");
    }
}

static void handleSetSimulation(JsonObject data, const char *requestId)
{
    bool okAny = false;

    if (data.containsKey("enabled") && s_ctx.setSimulationEnabled)
    {
        bool enabled = data["enabled"].as<bool>();
        s_ctx.setSimulationEnabled(enabled, true, "cmd");
        okAny = true;
    }
    if (data.containsKey("mode") && s_ctx.setSimulationModeInternal)
    {
        int m = data["mode"].as<int>();
        if (m < 0)
            m = 0;
        if (m > 5)
            m = 5;
        s_ctx.setSimulationModeInternal((uint8_t)m, true, "cmd");
        okAny = true;
    }

    finish(requestId, "set_simulation",
           okAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
           okAny ? "applied" : "no_fields");
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
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err)
    {
        finish("", "unknown", CmdStatus::REJECTED, "invalid_json");
        return;
    }

    const int schema = doc["schema"] | 0;
    const char *requestId = doc["request_id"] | "";
    const char *type = doc["type"] | "";

    if (schema != STATE_SCHEMA_VERSION || strlen(type) == 0)
    {
        finish(requestId, type, CmdStatus::REJECTED, "invalid_schema_or_type");
        return;
    }

    setLastCmd(requestId, type, CmdStatus::RECEIVED, "received");

    JsonObject data = doc["data"].as<JsonObject>();

    if (strcmp(type, "set_config") == 0)
    {
        handleSetConfig(data, requestId);
        return;
    }

    if (strcmp(type, "calibrate") == 0)
    {
        handleCalibrate(data, requestId);
        return;
    }

    if (strcmp(type, "clear_calibration") == 0)
    {
        handleClearCalibration(requestId);
        return;
    }

    if (strcmp(type, "set_simulation") == 0)
    {
        handleSetSimulation(data, requestId);
        return;
    }

    if (strcmp(type, "reannounce") == 0)
    {
        handleReannounce(requestId);
        return;
    }

    finish(requestId, type, CmdStatus::REJECTED, "unknown_type");
}