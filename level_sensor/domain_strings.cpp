#include "domain_strings.h"

const char *toString(SenseMode v)
{
    switch (v)
    {
    case SenseMode::TOUCH:
        return "touch";
    case SenseMode::SIM:
        return "sim";
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

const char *toString(OtaStatus s)
{
    switch (s)
    {
    case OtaStatus::IDLE:
        return "idle";
    case OtaStatus::DOWNLOADING:
        return "downloading";
    case OtaStatus::VERIFYING:
        return "verifying";
    case OtaStatus::APPLYING:
        return "applying";
    case OtaStatus::REBOOTING:
        return "rebooting";
    case OtaStatus::SUCCESS:
        return "success";
    case OtaStatus::ERROR:
        return "error";
    default:
        return "unknown";
    }
}