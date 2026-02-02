#pragma once
#include <stdint.h>

// Keep schema version explicit so consumers can evolve safely
static constexpr uint8_t STATE_SCHEMA_VERSION = 1;

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
    APPLIED = 1,
    REJECTED = 2,
    ERROR = 3
};

// --- Nested structs (composition) ---
struct DeviceInfo
{
    const char *id;
    const char *name;
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

enum class OtaStatus : uint8_t
{
    IDLE,
    DOWNLOADING,
    VERIFYING,
    APPLYING,
    REBOOTING,
    SUCCESS,
    ERROR
};

struct OtaState
{
    OtaStatus status = OtaStatus::IDLE;
    uint8_t progress = 0;

    // active request
    char request_id[48] = {};
    char version[16] = {};
    char url[160] = {};
    char sha256[65] = {};
    uint32_t started_ts = 0;

    // last result
    char last_result[16] = {};
    char last_message[64] = {};
    uint32_t completed_ts = 0;
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

    DeviceInfo device;
    WifiInfo wifi;
    MqttInfo mqtt;

    ProbeInfo probe;
    CalibrationInfo calibration;
    LevelInfo level;
    ConfigInfo config;

    OtaState ota;

    LastCmdInfo lastCmd;
};
