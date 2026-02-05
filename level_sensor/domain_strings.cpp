#include "domain_strings.h"

#if !defined(DOMAIN_STRINGS_STRICT)
#if !defined(NDEBUG)
#define DOMAIN_STRINGS_STRICT 1
#else
#define DOMAIN_STRINGS_STRICT 0
#endif
#endif

namespace
{
#if __cplusplus >= 201703L
    constexpr domain_strings::StringView kUnknown = "unknown";
    constexpr domain_strings::StringView kError = "error";
#else
    const char *kUnknown = "unknown";
    const char *kError = "error";
#endif
}

namespace domain_strings
{
    StringView to_string(SenseMode v)
    {
        switch (v)
        {
        case SenseMode::TOUCH:
            return "touch";
        case SenseMode::SIM:
            return "sim";
        }
#if DOMAIN_STRINGS_STRICT
        __builtin_unreachable();
#else
        return kUnknown;
#endif
    }

    StringView to_string(CalibrationState v)
    {
        switch (v)
        {
        case CalibrationState::NEEDS:
            return "needs_calibration";
        case CalibrationState::CALIBRATING:
            return "calibrating";
        case CalibrationState::CALIBRATED:
            return "calibrated";
        }
#if DOMAIN_STRINGS_STRICT
        __builtin_unreachable();
#else
        return kUnknown;
#endif
    }

    StringView to_string(ProbeQualityReason v)
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
            return kUnknown;
        }
#if DOMAIN_STRINGS_STRICT
        __builtin_unreachable();
#else
        return kUnknown;
#endif
    }

    StringView to_string(CmdStatus v)
    {
        switch (v)
        {
        case CmdStatus::RECEIVED:
            return "received";
        case CmdStatus::ACCEPTED:
            return "accepted";
        case CmdStatus::APPLIED:
            return "applied";
        case CmdStatus::REJECTED:
            return "rejected";
        case CmdStatus::ERROR:
            return kError;
        }
#if DOMAIN_STRINGS_STRICT
        __builtin_unreachable();
#else
        return kUnknown;
#endif
    }

    StringView to_string(OtaStatus s)
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
            return kError;
        }
#if DOMAIN_STRINGS_STRICT
        __builtin_unreachable();
#else
        return kUnknown;
#endif
    }
}
