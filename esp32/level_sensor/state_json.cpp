#include "state_json.h"
#include <ArduinoJson.h>
#include "telemetry_registry.h"

bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    // Sized to fit all telemetry fields comfortably.
    StaticJsonDocument<2048> doc;
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
    return written > 0 && written < outSize;
}