#pragma once
#include <stddef.h>
#include "device_state.h"

// Writes JSON into outBuf (null-terminated).
// Returns true if successful, false if buffer too small.
bool buildStateJson(const DeviceState &s, char *outBuf, size_t outSize);