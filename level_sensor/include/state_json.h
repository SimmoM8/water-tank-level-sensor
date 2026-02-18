#pragma once
#include <stddef.h>
#include <stdint.h>
#include "device_state.h"

enum class StateJsonError : uint8_t
{
    OK = 0,
    EMPTY,
    DOC_OVERFLOW,
    OUT_TOO_SMALL,
    SERIALIZE_FAILED,
    INTERNAL_MISMATCH
};

struct StateJsonDiag
{
    uint16_t bytes;
    uint16_t required;
    uint16_t outSize;
    uint16_t jsonCapacity;
    uint8_t fields;
    uint8_t writes;
    bool empty_root;
    bool overflowed;
};

// Writes JSON into outBuf (null-terminated).
// Returns a detailed status code and optional diagnostics.
StateJsonError buildStateJson(const DeviceState &s, char *outBuf, size_t outSize, StateJsonDiag *diag);
