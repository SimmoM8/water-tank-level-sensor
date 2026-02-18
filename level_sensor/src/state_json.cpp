#include "state_json.h"
#include <ArduinoJson.h>
#include <string.h>
#include "telemetry_registry.h"

namespace
{
static uint16_t clampToU16(size_t value)
{
    return value > 0xFFFFu ? 0xFFFFu : static_cast<uint16_t>(value);
}
} // namespace

// Contract: outBuf must be non-null and outSize must allow a null-terminated JSON payload.
StateJsonError buildStateJson(const DeviceState &s, char *outBuf, size_t outSize, StateJsonDiag *diag)
{
    // Capacity rationale (ArduinoJson v6):
    // - Objects created by dotted paths: root + device + wifi + time + mqtt + probe + calibration + level + config + ota + ota.active + ota.result + last_cmd = 13
    // - Leaf keys (worst case): 74 (schema/ts/uptime_seconds/boot_count/reboot_intent*/safe_mode*/crash_*/reset_reason/device/.../last_cmd.* + time.* + ota.force + ota.reboot + ota_last_success_ts)
    // - String pool: conservative sum of max field sizes + enum labels + key bytes headroom.
    static constexpr size_t kRootMembers = 39;
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
        JSON_STRING_SIZE(SAFE_MODE_REASON_MAX) +    // safe_mode_reason
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

    static constexpr size_t kJsonKeyBytes = 840; // ~80 keys * avg 10 bytes + headroom
    static constexpr size_t kStateJsonCapacity = kJsonObjectCapacity + kJsonStringCapacity + kJsonKeyBytes;
    static constexpr size_t kMinJsonSize = 2; // "{}"

    if (diag)
    {
        memset(diag, 0, sizeof(*diag));
        diag->outSize = clampToU16(outSize);
        diag->jsonCapacity = clampToU16(kStateJsonCapacity);
    }

    if (!outBuf || outSize == 0)
    {
        return StateJsonError::OUT_TOO_SMALL;
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

    if (diag)
    {
        diag->fields = fieldsCount > 0xFFu ? 0xFFu : static_cast<uint8_t>(fieldsCount);
        diag->writes = meaningfulWrites > 0xFFu ? 0xFFu : static_cast<uint8_t>(meaningfulWrites);
    }

    const bool emptyRoot = root.isNull() || root.size() == 0 || meaningfulWrites == 0;
    const size_t required = measureJson(doc);
    const bool overflowed = doc.overflowed();
    if (diag)
    {
        diag->required = clampToU16(required);
        diag->empty_root = emptyRoot;
        diag->overflowed = overflowed;
    }

    if (emptyRoot || required < kMinJsonSize)
    {
        return StateJsonError::EMPTY;
    }

    if (overflowed)
    {
        return StateJsonError::DOC_OVERFLOW;
    }

    if (required >= outSize)
    {
        return StateJsonError::OUT_TOO_SMALL;
    }

    const size_t written = serializeJson(doc, outBuf, outSize);
    if (diag)
    {
        diag->bytes = clampToU16(written);
    }

    if (written == 0)
    {
        outBuf[0] = '\0';
        return StateJsonError::SERIALIZE_FAILED;
    }

    if (written >= outSize)
    {
        outBuf[0] = '\0';
        return StateJsonError::OUT_TOO_SMALL;
    }

    outBuf[written] = '\0';
    if (outBuf[0] != '{')
    {
        outBuf[0] = '\0';
        return StateJsonError::INTERNAL_MISMATCH;
    }

    return StateJsonError::OK;
}
