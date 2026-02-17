#pragma once
#include "device_state.h"

#if __cplusplus >= 201703L
#include <string_view>
#endif

// Domain enum string conversions (schema-stable strings used in MQTT/state payloads).
namespace domain_strings
{
#if __cplusplus >= 201703L
    using StringView = std::string_view;
#else
    using StringView = const char *;
#endif

    StringView to_string(SenseMode v);
    StringView to_string(CalibrationState v);
    StringView to_string(ProbeQualityReason v);
    StringView to_string(CmdStatus v);
    StringView to_string(OtaStatus s);

    inline const char *c_str(StringView v)
    {
#if __cplusplus >= 201703L
        return v.data();
#else
        return v;
#endif
    }
}

// Legacy wrappers (keep temporarily for existing call sites).
inline const char *toString(SenseMode v) { return domain_strings::c_str(domain_strings::to_string(v)); }
inline const char *toString(CalibrationState v) { return domain_strings::c_str(domain_strings::to_string(v)); }
inline const char *toString(ProbeQualityReason v) { return domain_strings::c_str(domain_strings::to_string(v)); }
inline const char *toString(CmdStatus v) { return domain_strings::c_str(domain_strings::to_string(v)); }
inline const char *toString(OtaStatus v) { return domain_strings::c_str(domain_strings::to_string(v)); }
