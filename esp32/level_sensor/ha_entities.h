#pragma once
#include <stddef.h>

struct HaSensorSpec
{
    const char *objectId;
    const char *name;
    const char *valueTemplate;
    const char *deviceClass;    // optional
    const char *unit;           // optional
    const char *icon;           // optional
    const char *attrTemplate;   // optional JSON attributes template
    const char *uniqIdOverride; // optional stable unique_id suffix
};

struct HaButtonSpec
{
    const char *objectId;
    const char *name;
    const char *payloadJson;
    const char *uniqIdOverride; // optional stable unique_id suffix
};

struct HaNumberSpec
{
    const char *objectId;
    const char *name;
    const char *dataKey;
    float min;
    float max;
    float step;
    const char *cmdType;        // e.g., "set_config"
    const char *uniqIdOverride; // optional stable unique_id suffix
};

struct HaSwitchSpec
{
    const char *objectId;
    const char *name;
    const char *valueTemplate;
    const char *payloadOnJson;
    const char *payloadOffJson;
    const char *uniqIdOverride; // optional stable unique_id suffix
};

struct HaSelectSpec
{
    const char *objectId;
    const char *name;
    const char *valueTemplate;
    const char *cmdTemplateJson;
    const int *options;
    size_t optionCount;
    const char *uniqIdOverride; // optional stable unique_id suffix
};

// Accessors returning pointers to static tables; count is set via out param.
const HaSensorSpec *ha_getSensors(size_t &count);
const HaButtonSpec *ha_getButtons(size_t &count);
const HaNumberSpec *ha_getNumbers(size_t &count);
const HaSwitchSpec *ha_getSwitches(size_t &count);
const HaSelectSpec *ha_getSelects(size_t &count);
