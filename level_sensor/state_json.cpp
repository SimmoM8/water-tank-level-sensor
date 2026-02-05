#include "state_json.h"
#include <ArduinoJson.h>
#include <string.h>
#include "telemetry_registry.h"
#include "domain_strings.h"
#include "logger.h"

// Contract: outBuf must be non-null; outSize must allow at least "{}\\0".
// Returns true only if the JSON is fully serialized and non-empty.
bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    static constexpr size_t kStateJsonCapacity = 4096;
    static constexpr size_t kMinJsonSize = 4; // "{}" + NUL
    static constexpr uint32_t kWarnThrottleMs = 5000;
    static constexpr uint32_t kPreviewThrottleMs = 5000;

    if (!outBuf || outSize < kMinJsonSize)
    {
        return false;
    }
    outBuf[0] = '\0';

    // Sized to fit all telemetry fields comfortably.
    StaticJsonDocument<kStateJsonCapacity> doc;
    JsonObject root = doc.to<JsonObject>();

    size_t fieldsCount = 0;
    size_t meaningfulWrites = 0;
    const TelemetryFieldDef *fields = telemetry_registry_fields(fieldsCount);
    for (size_t i = 0; i < fieldsCount; ++i)
    {
        if (fields[i].writeFn)
        {
            if (fields[i].writeFn(s, root))
            {
                meaningfulWrites++;
            }
        }
    }

    const size_t written = serializeJson(doc, outBuf, outSize);
    const bool emptyRoot = root.isNull() || root.size() == 0;
    const bool overflowed = doc.overflowed();
    const bool ok = (meaningfulWrites > 0) && (written > 2) && (written < outSize) && !overflowed;
    if (!ok)
    {
        outBuf[0] = '\0';
        logger_logEvery("state_json_build_failed", kWarnThrottleMs, LogLevel::WARN, LogDomain::MQTT,
                        "State JSON build failed bytes=%u outSize=%u fields=%u writes=%u empty_root=%s overflowed=%s",
                        (unsigned)written,
                        (unsigned)outSize,
                        (unsigned)fieldsCount,
                        (unsigned)meaningfulWrites,
                        emptyRoot ? "true" : "false",
                        overflowed ? "true" : "false");
        return false;
    }

    outBuf[written] = '\0';

    // Debug small payloads only (throttled to avoid spam) without logging raw JSON.
    if (written < 256)
    {
        const size_t previewLen = written < 120 ? written : 120;
        char preview[121];
        memcpy(preview, outBuf, previewLen);
        preview[previewLen] = '\0';
        logger_logEvery("state_json_preview", kPreviewThrottleMs, LogLevel::DEBUG, LogDomain::MQTT,
                        "State JSON len=%u preview=%s", (unsigned)written, preview);
    }

    return true;
}
