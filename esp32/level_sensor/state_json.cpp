#include "state_json.h"
#include <ArduinoJson.h>
#include "telemetry_registry.h"
#include "domain_strings.h"
#include "logger.h"

bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    // Sized to fit all telemetry fields comfortably.
    StaticJsonDocument<4096> doc;
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
    if (written <= 2 || written >= outSize || emptyRoot)
    {
        LOG_WARN(LogDomain::MQTT, "State JSON build failed bytes=%u empty_root=%s", (unsigned)written, emptyRoot ? "true" : "false");
        return false;
    }

    static const char *kEmptyPattern = "{\"probe\":{},\"calibration\":{},\"level\":{},\"config\":{}}";
    if (strcmp(outBuf, kEmptyPattern) == 0)
    {
        size_t regCount = 0;
        telemetry_registry_fields(regCount);
        logger_logEvery("state_json_empty_objects", 5000, LogLevel::WARN, LogDomain::MQTT,
                        "State JSON suspicious empty objects; registry fields=%u", (unsigned)regCount);
    }

    // Debug small payloads only (throttled to avoid spam)
    if (written < 256)
    {
        size_t previewLen = written < 120 ? written : 120;
        char preview[121];
        memcpy(preview, outBuf, previewLen);
        preview[previewLen] = '\0';
        logger_logEvery("state_json_preview", 5000, LogLevel::DEBUG, LogDomain::MQTT,
                        "State JSON len=%u preview=%s", (unsigned)written, preview);
    }

    return true;
}