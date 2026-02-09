#pragma once
#include <stddef.h>

// Build-time firmware version. Override with compiler flags, e.g.:
//   -DFW_VERSION=\"1.4.0\"
#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif

namespace fw_version
{
template <size_t N>
constexpr const char *as_literal(const char (&value)[N])
{
    return value;
}

template <size_t N>
constexpr size_t literal_size(const char (&)[N])
{
    return N;
}

// Compile-time guardrails:
// - Ensures FW_VERSION resolves to a string literal / char array.
// - Provides the literal size including trailing NUL for buffer checks.
[[maybe_unused]] static constexpr const char *kLiteral = as_literal(FW_VERSION);
static constexpr size_t kSizeWithNul = literal_size(FW_VERSION);
static_assert(kSizeWithNul >= 2, "FW_VERSION must be a non-empty string literal");
} // namespace fw_version
