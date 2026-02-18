#pragma once

#include <stddef.h>
#include <stdint.h>

struct HaDiscoveryConfig
{
    const char *baseTopic;
    const char *deviceId;
    const char *deviceName;
    const char *deviceModel;
    const char *deviceSw;
    const char *deviceHw;

    // publish(topic, payload, retained)
    bool (*publish)(const char *, const char *, bool);
};

enum class HaDiscoveryResult : uint8_t
{
    NOT_INITIALIZED = 0,
    ALREADY_PUBLISHED,
    PUBLISHED,
    FAILED
};

void ha_discovery_begin(const HaDiscoveryConfig &cfg);
HaDiscoveryResult ha_discovery_publishAll();
