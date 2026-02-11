#pragma once
#include <stdint.h>
#include <stddef.h>

// Keep schema version explicit so consumers can evolve safely
static constexpr uint8_t STATE_SCHEMA_VERSION = 1;
static constexpr size_t DEVICE_FW_VERSION_MAX = 16;
static constexpr size_t OTA_STATE_MAX = 16;
static constexpr size_t OTA_ERROR_MAX = 64;
static constexpr size_t OTA_TARGET_VERSION_MAX = 16;
static constexpr size_t TIME_STATUS_MAX = 16;
static constexpr size_t OTA_REQUEST_ID_MAX = 48;
static constexpr size_t OTA_VERSION_MAX = 16;
static constexpr size_t OTA_URL_MAX = 256;
static constexpr size_t OTA_SHA256_MAX = 65; // 64 hex chars + NUL
static constexpr size_t OTA_STATUS_MAX = 16;
static constexpr size_t OTA_MESSAGE_MAX = 64;
static constexpr size_t RESET_REASON_MAX = 24;

// --- C++ enums (stronger than magic ints/strings) ---
enum class SenseMode : uint8_t
{
    TOUCH = 0,
    SIM = 1
};

enum class CalibrationState : uint8_t
{
    NEEDS = 0,
    CALIBRATING = 1,
    CALIBRATED = 2
};

enum class ProbeQualityReason : uint8_t
{
    OK = 0,
    DISCONNECTED_LOW_RAW,
    UNRELIABLE_SPIKES,
    UNRELIABLE_RAPID,
    UNRELIABLE_STUCK,
    OUT_OF_BOUNDS,
    CALIBRATION_RECOMMENDED,
    ZERO_HITS,
    UNKNOWN
};

enum class CmdStatus : uint8_t
{
    RECEIVED = 0,
    ACCEPTED = 1,
    APPLIED = 2,
    REJECTED = 3,
    ERROR = 4
};

static_assert(static_cast<uint8_t>(SenseMode::SIM) == 1, "SenseMode values must be stable");
static_assert(static_cast<uint8_t>(CalibrationState::CALIBRATED) == 2, "CalibrationState values must be stable");
static_assert(static_cast<uint8_t>(CmdStatus::ERROR) == 4, "CmdStatus values must be stable");
static_assert(OTA_SHA256_MAX == 65, "SHA256 buffer must fit 64 hex chars + NUL");

// --- Nested structs (composition) ---
struct DeviceInfo
{
    const char *id;
    const char *name;
    // Canonical installed firmware version (used for OTA comparisons).
    const char *fw;
};

struct WifiInfo
{
    int rssi;       // e.g. -55
    const char *ip; // "192.168.x.x" (we’ll fill from a buffer)
};

struct MqttInfo
{
    bool connected;
};

struct ProbeInfo
{
    bool connected;
    ProbeQualityReason quality;
    SenseMode senseMode;
    int32_t raw; // 32-bit raw reading for consistency across probe/calibration paths
    bool rawValid;
};

struct CalibrationInfo
{
    CalibrationState state;
    int32_t dry;
    int32_t wet;
    bool inverted;
    int32_t minDiff;
};

struct LevelInfo
{
    float percent;
    bool percentValid;
    float liters;
    bool litersValid;
    float centimeters;
    bool centimetersValid;
};

struct ConfigInfo
{
    float tankVolumeLiters;
    float rodLengthCm;
    SenseMode senseMode;
    uint8_t simulationMode;
};

struct TimeInfo
{
    bool valid = false;
    char status[TIME_STATUS_MAX] = {0}; // valid | syncing | time_not_set
    uint32_t last_attempt_s = 0;
    uint32_t last_success_s = 0;
    uint32_t next_retry_s = 0;
};

enum class OtaStatus : uint8_t
{
    IDLE = 0,
    DOWNLOADING = 1,
    VERIFYING = 2,
    APPLYING = 3,
    REBOOTING = 4,
    SUCCESS = 5,
    ERROR = 6
};

struct OtaState
{
    OtaStatus status = OtaStatus::IDLE;
    uint8_t progress = 0;

    // active request
    char request_id[OTA_REQUEST_ID_MAX] = {0};
    char version[OTA_VERSION_MAX] = {0};
    char url[OTA_URL_MAX] = {0};
    char sha256[OTA_SHA256_MAX] = {0};
    uint32_t started_ts = 0; // epoch seconds (0 if time not set)

    // last result
    char last_status[OTA_STATUS_MAX] = {0};
    char last_message[OTA_MESSAGE_MAX] = {0};
    uint32_t completed_ts = 0; // epoch seconds (0 if time not set)
};

struct LastCmdInfo
{
    const char *requestId; // points to a buffer owned by commands.cpp
    const char *type;      // e.g. "set_config"
    CmdStatus status;      // applied/rejected/etc
    const char *message;   // short human string
    uint32_t ts;           // when applied (epoch or millis/1000)
};
struct DeviceState
{
    uint8_t schema;
    uint32_t ts; // epoch seconds if you have it; otherwise millis()/1000 is ok
    uint32_t uptime_seconds = 0;               // derived from millis()/1000 at runtime (not persisted)
    char reset_reason[RESET_REASON_MAX] = {0}; // power_on | software_reset | panic | deep_sleep | watchdog | other
    uint32_t boot_count = 0;                   // persistent boot counter

    DeviceInfo device;
    // Mirror of device.fw for telemetry safety (stable, null-terminated buffer).
    // Keep in sync with device.fw if firmware version changes.
    char fw_version[DEVICE_FW_VERSION_MAX] = {0};
    WifiInfo wifi;
    MqttInfo mqtt;

    ProbeInfo probe;
    CalibrationInfo calibration;
    LevelInfo level;
    ConfigInfo config;
    TimeInfo time;

    OtaState ota;
    // Flat OTA fields for telemetry/HA compatibility (derived or legacy mirrors of ota.*).
    char ota_state[OTA_STATE_MAX] = {0};              // optional override of ota.status label
    uint8_t ota_progress = 0;                         // mirror of ota.progress
    char ota_error[OTA_ERROR_MAX] = {0};              // mirror/summary of ota.result.message
    char ota_target_version[OTA_TARGET_VERSION_MAX] = {0}; // mirror of ota.version or manifest
    uint32_t ota_last_ts = 0;                         // epoch seconds mirror of ota.started_ts/completed_ts
    uint32_t ota_last_success_ts = 0;                 // epoch seconds of last successful OTA
    bool update_available = false;
    bool ota_force = false;                           // default force behavior for ota_pull
    bool ota_reboot = true;                           // default reboot behavior for ota_pull

    LastCmdInfo lastCmd;
};
