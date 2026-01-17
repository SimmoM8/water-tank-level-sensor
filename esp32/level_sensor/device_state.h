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
    uint16_t minDiff;
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
    bool simulationEnabled;
    uint8_t simulationMode;
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

    LastCmdInfo lastCmd;
};

// --- enum → string helpers (we’ll implement in .cpp) ---
const char *toString(SenseMode v);
const char *toString(CalibrationState v);
const char *toString(ProbeQualityReason v);
const char *toString(CmdStatus v);