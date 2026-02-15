#pragma once
#include <stdint.h>

struct Version
{
    uint16_t major = 0;
    uint16_t minor = 0;
    uint16_t patch = 0;
    bool hasPrerelease = false;
    char prerelease[16] = {0};
};

// Parse MAJOR.MINOR.PATCH[-prerelease]
bool parseVersion(const char *str, Version *out);

// Return -1 if a < b, 0 if a == b, 1 if a > b.
int compareVersion(const Version &a, const Version &b);

// Parse and compare version strings.
// Returns false if either version is malformed.
bool compareVersionStrings(const char *a, const char *b, int *outCmp);
