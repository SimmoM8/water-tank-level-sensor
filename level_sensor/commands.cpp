#include "commands.h"
#include <ArduinoJson.h>

#include "device_state.h"
#include "mqtt_transport.h"

// These are owned by main/app (single source of applied truth in RAM)
extern DeviceState g_state;

// Existing apply functions in your app (you already have these)
extern void updateTankVolume(float liters, bool forcePublish);
extern void updateRodLength(float cm, bool forcePublish);
extern void captureCalibrationPoint(bool isDry);
extern void clearCalibration();
extern void setSimulationEnabled(bool enabled, bool forcePublish, const char *sourceMsg);
extern void setSimulationModeInternal(uint8_t mode, bool forcePublish, const char *sourceMsg);

// storage for last_cmd strings so pointers remain valid
static char s_reqId[40] = "";
static char s_type[24] = "";
static char s_msg[64] = "";

static void setLastCmd(const char *reqId, const char *type, CmdStatus st, const char *msg)
{
    strncpy(s_reqId, reqId ? reqId : "", sizeof(s_reqId));
    s_reqId[sizeof(s_reqId) - 1] = 0;

    strncpy(s_type, type ? type : "", sizeof(s_type));
    s_type[sizeof(s_type) - 1] = 0;

    strncpy(s_msg, msg ? msg : "", sizeof(s_msg));
    s_msg[sizeof(s_msg) - 1] = 0;

    g_state.lastCmd.requestId = s_reqId;
    g_state.lastCmd.type = s_type;
    g_state.lastCmd.status = st;
    g_state.lastCmd.message = s_msg;
    g_state.lastCmd.ts = g_state.ts; // or millis()/1000 if that's what you're using
}

void commands_handle(const uint8_t *payload, size_t len)
{
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err)
    {
        setLastCmd("", "unknown", CmdStatus::REJECTED, "invalid_json");
        mqtt_requestStatePublish();
        return;
    }

    const int schema = doc["schema"] | 0;
    const char *requestId = doc["request_id"] | "";
    const char *type = doc["type"] | "";

    if (schema != STATE_SCHEMA_VERSION || strlen(type) == 0)
    {
        setLastCmd(requestId, type, CmdStatus::REJECTED, "invalid_schema_or_type");
        mqtt_requestStatePublish();
        return;
    }

    // Mark received (optional, but nice)
    setLastCmd(requestId, type, CmdStatus::RECEIVED, "received");

    JsonObject data = doc["data"].as<JsonObject>();

    if (strcmp(type, "set_config") == 0)
    {
        // only apply fields that exist
        bool appliedAny = false;

        if (data.containsKey("tank_volume_l"))
        {
            float v = data["tank_volume_l"].as<float>();
            updateTankVolume(v, true);
            appliedAny = true;
        }
        if (data.containsKey("rod_length_cm"))
        {
            float v = data["rod_length_cm"].as<float>();
            updateRodLength(v, true);
            appliedAny = true;
        }

        setLastCmd(requestId, type,
                   appliedAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
                   appliedAny ? "applied" : "no_fields");
        mqtt_requestStatePublish();
        return;
    }

    if (strcmp(type, "calibrate") == 0)
    {
        const char *point = data["point"] | "";
        if (strcmp(point, "dry") == 0)
        {
            captureCalibrationPoint(true);
        }
        else if (strcmp(point, "wet") == 0)
        {
            captureCalibrationPoint(false);
        }
        else
        {
            setLastCmd(requestId, type, CmdStatus::REJECTED, "invalid_point");
            mqtt_requestStatePublish();
            return;
        }

        setLastCmd(requestId, type, CmdStatus::APPLIED, point);
        mqtt_requestStatePublish();
        return;
    }

    if (strcmp(type, "clear_calibration") == 0)
    {
        clearCalibration();
        setLastCmd(requestId, type, CmdStatus::APPLIED, "cleared");
        mqtt_requestStatePublish();
        return;
    }

    if (strcmp(type, "set_simulation") == 0)
    {
        bool okAny = false;

        if (data.containsKey("enabled"))
        {
            bool enabled = data["enabled"].as<bool>();
            setSimulationEnabled(enabled, true, "cmd");
            okAny = true;
        }
        if (data.containsKey("mode"))
        {
            int m = data["mode"].as<int>();
            if (m < 0)
                m = 0;
            if (m > 5)
                m = 5;
            setSimulationModeInternal((uint8_t)m, true, "cmd");
            okAny = true;
        }

        setLastCmd(requestId, type,
                   okAny ? CmdStatus::APPLIED : CmdStatus::REJECTED,
                   okAny ? "applied" : "no_fields");
        mqtt_requestStatePublish();
        return;
    }

    setLastCmd(requestId, type, CmdStatus::REJECTED, "unknown_type");
    mqtt_requestStatePublish();
}