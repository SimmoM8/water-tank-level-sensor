#include "semver.h"
#include <stddef.h>
#include <string.h>

static inline bool isDecDigit(char c)
{
    return c >= '0' && c <= '9';
}

static inline bool isPrereleaseChar(char c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           c == '-' ||
           c == '.';
}

static bool parseUint16Part(const char *&p, uint16_t &out)
{
    if (!p || !isDecDigit(*p))
    {
        return false;
    }

    const char *start = p;
    uint32_t value = 0;
    size_t digits = 0;
    while (isDecDigit(*p))
    {
        value = (value * 10u) + (uint32_t)(*p - '0');
        if (value > 65535u)
        {
            return false;
        }
        ++p;
        ++digits;
    }

    // Numeric identifiers must not contain leading zeroes.
    if (digits > 1 && start[0] == '0')
    {
        return false;
    }

    out = (uint16_t)value;
    return true;
}

static bool validatePrereleaseIdentifiers(const char *s)
{
    if (!s || s[0] == '\0')
    {
        return false;
    }

    const char *idStart = s;
    while (*idStart != '\0')
    {
        const char *idEnd = idStart;
        bool allDigits = true;
        while (*idEnd != '\0' && *idEnd != '.')
        {
            if (!isDecDigit(*idEnd))
            {
                allDigits = false;
            }
            ++idEnd;
        }

        const size_t len = (size_t)(idEnd - idStart);
        if (len == 0)
        {
            return false;
        }
        if (allDigits && len > 1 && idStart[0] == '0')
        {
            return false;
        }
        if (*idEnd == '\0')
        {
            break;
        }
        idStart = idEnd + 1;
    }
    return true;
}

bool parseVersion(const char *str, Version *out)
{
    if (!str || !out)
    {
        return false;
    }

    Version parsed;
    const char *p = str;
    if (!parseUint16Part(p, parsed.major))
    {
        return false;
    }
    if (*p != '.')
    {
        return false;
    }
    ++p;
    if (!parseUint16Part(p, parsed.minor))
    {
        return false;
    }
    if (*p != '.')
    {
        return false;
    }
    ++p;
    if (!parseUint16Part(p, parsed.patch))
    {
        return false;
    }

    if (*p == '\0')
    {
        *out = parsed;
        return true;
    }
    if (*p != '-')
    {
        return false;
    }
    ++p;

    size_t n = 0;
    while (*p != '\0')
    {
        const char c = *p;
        if (!isPrereleaseChar(c))
        {
            return false;
        }
        if (n + 1 >= sizeof(parsed.prerelease))
        {
            return false;
        }
        parsed.prerelease[n++] = c;
        ++p;
    }
    if (n == 0)
    {
        return false;
    }
    parsed.prerelease[n] = '\0';
    if (!validatePrereleaseIdentifiers(parsed.prerelease))
    {
        return false;
    }

    parsed.hasPrerelease = true;
    *out = parsed;
    return true;
}

static bool prereleaseIdIsNumeric(const char *start, size_t len)
{
    if (!start || len == 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (!isDecDigit(start[i]))
        {
            return false;
        }
    }
    return true;
}

static int comparePrerelease(const char *a, const char *b)
{
    const char *pa = a;
    const char *pb = b;

    while (true)
    {
        const char *ea = pa;
        const char *eb = pb;
        while (*ea != '\0' && *ea != '.')
            ++ea;
        while (*eb != '\0' && *eb != '.')
            ++eb;

        const size_t la = (size_t)(ea - pa);
        const size_t lb = (size_t)(eb - pb);
        const bool na = prereleaseIdIsNumeric(pa, la);
        const bool nb = prereleaseIdIsNumeric(pb, lb);

        int cmp = 0;
        if (na && nb)
        {
            if (la < lb)
                cmp = -1;
            else if (la > lb)
                cmp = 1;
            else
            {
                const int raw = strncmp(pa, pb, la);
                if (raw < 0)
                    cmp = -1;
                else if (raw > 0)
                    cmp = 1;
            }
        }
        else if (na != nb)
        {
            cmp = na ? -1 : 1;
        }
        else
        {
            const size_t minLen = (la < lb) ? la : lb;
            const int raw = strncmp(pa, pb, minLen);
            if (raw < 0)
                cmp = -1;
            else if (raw > 0)
                cmp = 1;
            else if (la < lb)
                cmp = -1;
            else if (la > lb)
                cmp = 1;
        }

        if (cmp != 0)
        {
            return cmp;
        }

        const bool aDone = (*ea == '\0');
        const bool bDone = (*eb == '\0');
        if (aDone && bDone)
        {
            return 0;
        }
        if (aDone)
        {
            return -1;
        }
        if (bDone)
        {
            return 1;
        }

        pa = ea + 1;
        pb = eb + 1;
    }
}

int compareVersion(const Version &a, const Version &b)
{
    if (a.major < b.major)
        return -1;
    if (a.major > b.major)
        return 1;

    if (a.minor < b.minor)
        return -1;
    if (a.minor > b.minor)
        return 1;

    if (a.patch < b.patch)
        return -1;
    if (a.patch > b.patch)
        return 1;

    if (a.hasPrerelease != b.hasPrerelease)
    {
        // Release > prerelease
        return a.hasPrerelease ? -1 : 1;
    }
    if (!a.hasPrerelease)
    {
        return 0;
    }
    return comparePrerelease(a.prerelease, b.prerelease);
}

bool compareVersionStrings(const char *a, const char *b, int *outCmp)
{
    if (!a || !b || !outCmp)
    {
        return false;
    }

    Version va;
    Version vb;
    if (!parseVersion(a, &va) || !parseVersion(b, &vb))
    {
        return false;
    }

    *outCmp = compareVersion(va, vb);
    return true;
}
