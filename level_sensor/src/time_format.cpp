#include "time_format.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace
{
static inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}
} // namespace

namespace time_format
{
bool formatIsoUtc(uint32_t epochSeconds, char *out, size_t outSize)
{
    if (!out || outSize == 0)
    {
        return false;
    }

    out[0] = '\0';
    if (outSize < 21 || epochSeconds < 1600000000u)
    {
        return false;
    }

    const time_t t = static_cast<time_t>(epochSeconds);
    struct tm tmUtc;
    memset(&tmUtc, 0, sizeof(tmUtc));
    if (!gmtime_r(&t, &tmUtc))
    {
        return false;
    }

    const int written = snprintf(out,
                                 outSize,
                                 "%04d-%02d-%02dT%02d:%02d:%02dZ",
                                 tmUtc.tm_year + 1900,
                                 tmUtc.tm_mon + 1,
                                 tmUtc.tm_mday,
                                 tmUtc.tm_hour,
                                 tmUtc.tm_min,
                                 tmUtc.tm_sec);
    if (written <= 0 || static_cast<size_t>(written) >= outSize)
    {
        out[outSize - 1] = '\0';
        return false;
    }

    return true;
}

bool isValidIsoUtc(const char *value)
{
    if (!value || value[0] == '\0')
    {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(value[0]);
    if (first < 0x20u || first > 0x7Eu)
    {
        return false;
    }

    // Exact shape: YYYY-MM-DDTHH:MM:SSZ
    if (strlen(value) != 20)
    {
        return false;
    }

    if (value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z')
    {
        return false;
    }

    static constexpr uint8_t kDigitIndexes[] = {
        0, 1, 2, 3,
        5, 6,
        8, 9,
        11, 12,
        14, 15,
        17, 18};
    for (size_t i = 0; i < sizeof(kDigitIndexes) / sizeof(kDigitIndexes[0]); ++i)
    {
        if (!isDigit(value[kDigitIndexes[i]]))
        {
            return false;
        }
    }

    return true;
}
} // namespace time_format

