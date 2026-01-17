#pragma once
#include <stddef.h>
#include <stdint.h>

struct DeviceState;

// Callback bundle that lets commands mutate state without globals.
struct CommandsContext
{
    DeviceState *state; // points to device-owned state snapshot

    void (*updateTankVolume)(float liters, bool forcePublish);
    void (*updateRodLength)(float cm, bool forcePublish);
    void (*captureCalibrationPoint)(bool isDry);
    void (*clearCalibration)();
    void (*setSimulationEnabled)(bool enabled, bool forcePublish, const char *sourceMsg);
    void (*setSimulationModeInternal)(uint8_t mode, bool forcePublish, const char *sourceMsg);

    void (*requestStatePublish)();
    bool (*publishAck)(const char *requestId, const char *type, CmdStatus status, const char *msg);
};

// Initialize command handling with device-owned context.
void commands_begin(const CommandsContext &ctx);

// payload is raw MQTT bytes (not null terminated)
void commands_handle(const uint8_t *payload, size_t len);