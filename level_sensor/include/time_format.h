#pragma once

#include <stddef.h>
#include <stdint.h>

namespace time_format
{
// Formats epoch seconds as YYYY-MM-DDTHH:MM:SSZ.
// Returns false and writes an empty string when formatting is not possible.
bool formatIsoUtc(uint32_t epochSeconds, char *out, size_t outSize);

// Strict validator for YYYY-MM-DDTHH:MM:SSZ.
// Empty strings and non-printable leading characters are treated as invalid.
bool isValidIsoUtc(const char *value);
} // namespace time_format

