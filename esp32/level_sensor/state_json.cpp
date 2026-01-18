#include "state_json.h"
#include <ArduinoJson.h>
#include "telemetry_fields.h"

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
    // Sized to fit all telemetry fields comfortably.
    StaticJsonDocument<2048> doc;
    JsonObject root = doc.to<JsonObject>();

    size_t count = 0;
    const TelemetryField *fields = telemetry_getAll(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (fields[i].writeFn)
        {
            fields[i].writeFn(s, root);
        }
    }

    const size_t written = serializeJson(doc, outBuf, outSize);
    return written > 0 && written < outSize;
}