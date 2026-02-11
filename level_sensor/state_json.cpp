#include "state_json.h"
#include <ArduinoJson.h>
#include <string.h>
#include "telemetry_registry.h"
#include "logger.h"

// Contract: outBuf must be non-null; outSize must allow at least "{}\\0".
// Returns true only if the JSON is fully serialized and non-empty.
bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize)
{
    // Capacity rationale (ArduinoJson v6):
    // - Objects created by dotted paths: root + device + wifi + time + mqtt + probe + calibration + level + config + ota + ota.active + ota.result + last_cmd = 13
    // - Leaf keys (worst case): 70 (schema/ts/uptime_seconds/boot_count/reboot_intent*/crash_*/reset_reason/device/.../last_cmd.* + time.* + ota.force + ota.reboot + ota_last_success_ts)
    // - String pool: conservative sum of max field sizes + enum labels + key bytes headroom.
    static constexpr size_t kRootMembers = 35;
    static constexpr size_t kDeviceMembers = 3;
    static constexpr size_t kWifiMembers = 2;
    static constexpr size_t kTimeMembers = 5;
    static constexpr size_t kMqttMembers = 1;
    static constexpr size_t kProbeMembers = 4;
    static constexpr size_t kCalibrationMembers = 5;
    static constexpr size_t kLevelMembers = 6;
    static constexpr size_t kConfigMembers = 4;
    static constexpr size_t kOtaMembers = 6;
    static constexpr size_t kOtaActiveMembers = 5;
    static constexpr size_t kOtaResultMembers = 3;
    static constexpr size_t kLastCmdMembers = 5;

    static constexpr size_t kJsonObjectCapacity =
        JSON_OBJECT_SIZE(kRootMembers) +
        JSON_OBJECT_SIZE(kDeviceMembers) +
        JSON_OBJECT_SIZE(kWifiMembers) +
        JSON_OBJECT_SIZE(kTimeMembers) +
        JSON_OBJECT_SIZE(kMqttMembers) +
        JSON_OBJECT_SIZE(kProbeMembers) +
        JSON_OBJECT_SIZE(kCalibrationMembers) +
        JSON_OBJECT_SIZE(kLevelMembers) +
        JSON_OBJECT_SIZE(kConfigMembers) +
        JSON_OBJECT_SIZE(kOtaMembers) +
        JSON_OBJECT_SIZE(kOtaActiveMembers) +
        JSON_OBJECT_SIZE(kOtaResultMembers) +
        JSON_OBJECT_SIZE(kLastCmdMembers);

    // Conservative maxima for string fields (buffers include NUL in DeviceState; over-allocating is ok).
    static constexpr size_t kMaxDeviceId = 32;
    static constexpr size_t kMaxDeviceName = 32;
    static constexpr size_t kMaxDeviceFw = DEVICE_FW_VERSION_MAX;
    static constexpr size_t kMaxWifiIp = 16;
    static constexpr size_t kMaxEnumStr = 32; // longest enum label e.g. "unreliable_rapid_fluctuation"
    static constexpr size_t kMaxLastCmdId = 40;
    static constexpr size_t kMaxLastCmdType = 24;
    static constexpr size_t kMaxLastCmdMsg = 64;
    static constexpr size_t kMaxIso8601Utc = 25;

    static constexpr size_t kJsonStringCapacity =
        JSON_STRING_SIZE(kMaxDeviceId) +
        JSON_STRING_SIZE(kMaxDeviceName) +
        JSON_STRING_SIZE(kMaxDeviceFw) +
        JSON_STRING_SIZE(DEVICE_FW_VERSION_MAX) +   // fw_version
        JSON_STRING_SIZE(DEVICE_FW_VERSION_MAX) +   // installed_version
        JSON_STRING_SIZE(OTA_TARGET_VERSION_MAX) +  // latest_version / ota_target_version
        JSON_STRING_SIZE(RESET_REASON_MAX) +        // reset_reason
        JSON_STRING_SIZE(REBOOT_INTENT_LABEL_MAX) + // reboot_intent_label
        JSON_STRING_SIZE(CRASH_LOOP_REASON_MAX) +   // crash_loop_reason
        JSON_STRING_SIZE(kMaxWifiIp) +
        JSON_STRING_SIZE(TIME_STATUS_MAX) +
        JSON_STRING_SIZE(kMaxEnumStr) +             // probe.quality
        JSON_STRING_SIZE(kMaxEnumStr) +             // calibration.state
        JSON_STRING_SIZE(kMaxEnumStr) +             // config.sense_mode
        JSON_STRING_SIZE(OTA_STATE_MAX) +
        JSON_STRING_SIZE(OTA_ERROR_MAX) +
        JSON_STRING_SIZE(OTA_TARGET_VERSION_MAX) +
        JSON_STRING_SIZE(kMaxIso8601Utc) +           // ota_last_ts (timestamp sensor)
        JSON_STRING_SIZE(kMaxIso8601Utc) +           // ota_last_success_ts (timestamp sensor)
        JSON_STRING_SIZE(kMaxEnumStr) +             // ota.status
        JSON_STRING_SIZE(OTA_REQUEST_ID_MAX) +
        JSON_STRING_SIZE(OTA_VERSION_MAX) +
        JSON_STRING_SIZE(OTA_URL_MAX) +
        JSON_STRING_SIZE(OTA_SHA256_MAX) +
        JSON_STRING_SIZE(OTA_STATUS_MAX) +
        JSON_STRING_SIZE(OTA_MESSAGE_MAX) +
        JSON_STRING_SIZE(kMaxLastCmdId) +
        JSON_STRING_SIZE(kMaxLastCmdType) +
        JSON_STRING_SIZE(kMaxEnumStr) +             // last_cmd.status
        JSON_STRING_SIZE(kMaxLastCmdMsg);

    static constexpr size_t kJsonKeyBytes = 760; // ~70 keys * avg 10 bytes + headroom
    static constexpr size_t kStateJsonCapacity = kJsonObjectCapacity + kJsonStringCapacity + kJsonKeyBytes;
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

    const size_t required = measureJson(doc);
    const bool fits = (required + 1) <= outSize;
    const size_t written = fits ? serializeJson(doc, outBuf, outSize) : 0;
    const bool emptyRoot = root.isNull() || root.size() == 0;
    const bool overflowed = doc.overflowed();
    const bool ok = (meaningfulWrites > 0) && (required > 2) && fits && (written == required) && !overflowed;
    if (!ok)
    {
        outBuf[0] = '\0';
        logger_logEvery("state_json_build_failed", kWarnThrottleMs, LogLevel::WARN, LogDomain::MQTT,
                        "State JSON build failed bytes=%u required=%u outSize=%u fields=%u writes=%u jsonCapacity=%u empty_root=%s overflowed=%s",
                        (unsigned)written,
                        (unsigned)required,
                        (unsigned)outSize,
                        (unsigned)fieldsCount,
                        (unsigned)meaningfulWrites,
                        (unsigned)kStateJsonCapacity,
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
