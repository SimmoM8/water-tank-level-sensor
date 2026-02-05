#include "state_json.h"
#include <ArduinoJson.h>
#include "telemetry_registry.h"
#include "domain_strings.h"
#include "logger.h"

// Contract: outBuf must be non-null; outSize must allow at least "{}\\0".
// Returns true only if the JSON is fully serialized and non-empty.
bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    static constexpr size_t kStateJsonCapacity = 4096;
    static constexpr size_t kMinJsonSize = 4; // "{}" + NUL

    if (!outBuf || outSize < kMinJsonSize)
    {
        return false;
    }
    outBuf[0] = '\0';

    // Sized to fit all telemetry fields comfortably.
    StaticJsonDocument<kStateJsonCapacity> doc;
    JsonObject root = doc.to<JsonObject>();

    size_t count = 0;
    const TelemetryFieldDef *fields = telemetry_registry_fields(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (fields[i].writeFn)
        {
            fields[i].writeFn(s, root);
        }
    }

    const size_t written = serializeJson(doc, outBuf, outSize);
    const bool emptyRoot = root.isNull() || root.size() == 0;
    const bool overflowed = doc.overflowed();
    if (written <= 2 || written >= outSize || emptyRoot || overflowed)
    {
        outBuf[0] = '\0';
        LOG_WARN(LogDomain::MQTT, "State JSON build failed bytes=%u empty_root=%s overflowed=%s",
                 (unsigned)written,
                 emptyRoot ? "true" : "false",
                 overflowed ? "true" : "false");
        return false;
    }

    // Debug small payloads only (throttled to avoid spam) without logging raw JSON.
    if (written < 256)
    {
        logger_logEvery("state_json_len", 5000, LogLevel::DEBUG, LogDomain::MQTT,
                        "State JSON len=%u", (unsigned)written);
    }

    return true;
}

