#include "state_json.h"
#include <ArduinoJson.h>

const char *toString(SenseMode v)
{
    switch (v)
    {
    case SenseMode::TOUCH:
        return "touch";
    case SenseMode::SIM:
        return "simulation";
    default:
        return "unknown";
    }
}

const char *toString(CalibrationState v)
{
    switch (v)
    {
    case CalibrationState::NEEDS:
        return "needs_calibration";
    case CalibrationState::CALIBRATING:
        return "calibrating";
    case CalibrationState::CALIBRATED:
        return "calibrated";
    default:
        return "unknown";
    }
}

const char *toString(ProbeQualityReason v)
{
    switch (v)
    {
    case ProbeQualityReason::OK:
        return "ok";
    case ProbeQualityReason::DISCONNECTED_LOW_RAW:
        return "disconnected_low_raw";
    case ProbeQualityReason::UNRELIABLE_SPIKES:
        return "unreliable_spikes";
    case ProbeQualityReason::UNRELIABLE_RAPID:
        return "unreliable_rapid_fluctuation";
    case ProbeQualityReason::UNRELIABLE_STUCK:
        return "unreliable_stuck";
    case ProbeQualityReason::OUT_OF_BOUNDS:
        return "out_of_bounds";
    case ProbeQualityReason::CALIBRATION_RECOMMENDED:
        return "calibration_recommended";
    case ProbeQualityReason::ZERO_HITS:
        return "zero_hits";
    case ProbeQualityReason::UNKNOWN:
        return "unknown";
    default:
        return "unknown";
    }
}

const char *toString(CmdStatus v)
{
    switch (v)
    {
    case CmdStatus::RECEIVED:
        return "received";
    case CmdStatus::APPLIED:
        return "applied";
    case CmdStatus::REJECTED:
        return "rejected";
    case CmdStatus::ERROR:
        return "error";
    default:
        return "unknown";
    }
}

bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    // StaticJsonDocument lives on the stack; sized to fit all fields comfortably.
    StaticJsonDocument<2048> doc;

    doc["schema"] = s.schema;
    doc["ts"] = s.ts;

    JsonObject device = doc["device"].to<JsonObject>();
    device["id"] = s.device.id;
    device["name"] = s.device.name;
    device["fw"] = s.device.fw;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["rssi"] = s.wifi.rssi;
    wifi["ip"] = s.wifi.ip;

    JsonObject mqtt = doc["mqtt"].to<JsonObject>();
    mqtt["connected"] = s.mqtt.connected;

    JsonObject probe = doc["probe"].to<JsonObject>();
    probe["connected"] = s.probe.connected;
    probe["quality"] = toString(s.probe.quality);
    probe["sense_mode"] = toString(s.probe.senseMode);
    probe["raw"] = s.probe.raw;
    probe["raw_valid"] = s.probe.rawValid;

    JsonObject cal = doc["calibration"].to<JsonObject>();
    cal["state"] = toString(s.calibration.state);
    cal["dry"] = s.calibration.dry;
    cal["wet"] = s.calibration.wet;
    cal["inverted"] = s.calibration.inverted;
    cal["min_diff"] = s.calibration.minDiff;

    JsonObject level = doc["level"].to<JsonObject>();
    level["percent"] = s.level.percent;
    level["percent_valid"] = s.level.percentValid;
    level["liters"] = s.level.liters;
    level["liters_valid"] = s.level.litersValid;
    level["centimeters"] = s.level.centimeters;
    level["centimeters_valid"] = s.level.centimetersValid;

    JsonObject cfg = doc["config"].to<JsonObject>();
    cfg["tank_volume_l"] = s.config.tankVolumeLiters;
    cfg["rod_length_cm"] = s.config.rodLengthCm;
    cfg["simulation_enabled"] = s.config.simulationEnabled;
    cfg["simulation_mode"] = s.config.simulationMode;

    JsonObject lastCmd = doc["last_cmd"].to<JsonObject>();
    lastCmd["request_id"] = s.lastCmd.requestId;
    lastCmd["type"] = s.lastCmd.type;
    lastCmd["status"] = toString(s.lastCmd.status);
    lastCmd["message"] = s.lastCmd.message;
    lastCmd["ts"] = s.lastCmd.ts;

    // Serialize to buffer
    const size_t written = serializeJson(doc, outBuf, outSize);
    return written > 0 && written < outSize;
}