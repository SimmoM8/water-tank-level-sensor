#include <cstdio>
#include <cstring>
#include "semver.h"

static int s_failures = 0;

#define EXPECT_TRUE(expr)                                                                                               \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!(expr))                                                                                                    \
        {                                                                                                               \
            std::fprintf(stderr, "FAIL:%d expected true: %s\n", __LINE__, #expr);                                     \
            ++s_failures;                                                                                               \
        }                                                                                                               \
    } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ_INT(actual, expected)                                                                                 \
    do                                                                                                                  \
    {                                                                                                                   \
        const int _a = (actual);                                                                                       \
        const int _e = (expected);                                                                                     \
        if (_a != _e)                                                                                                  \
        {                                                                                                               \
            std::fprintf(stderr, "FAIL:%d expected %s=%d got %d\n", __LINE__, #actual, _e, _a);                      \
            ++s_failures;                                                                                               \
        }                                                                                                               \
    } while (0)

static void test_parse_valid()
{
    Version v{};
    EXPECT_TRUE(parseVersion("1.2.3", &v));
    EXPECT_EQ_INT(v.major, 1);
    EXPECT_EQ_INT(v.minor, 2);
    EXPECT_EQ_INT(v.patch, 3);
    EXPECT_FALSE(v.hasPrerelease);

    Version p{};
    EXPECT_TRUE(parseVersion("10.20.30-rc.1", &p));
    EXPECT_EQ_INT(p.major, 10);
    EXPECT_EQ_INT(p.minor, 20);
    EXPECT_EQ_INT(p.patch, 30);
    EXPECT_TRUE(p.hasPrerelease);
    EXPECT_TRUE(std::strcmp(p.prerelease, "rc.1") == 0);
}

static void test_parse_invalid()
{
    Version v{};
    EXPECT_FALSE(parseVersion(nullptr, &v));
    EXPECT_FALSE(parseVersion("", &v));
    EXPECT_FALSE(parseVersion("1", &v));
    EXPECT_FALSE(parseVersion("1.2", &v));
    EXPECT_FALSE(parseVersion("1.2.3-", &v));
    EXPECT_FALSE(parseVersion("1.2.3+build", &v));
    EXPECT_FALSE(parseVersion("01.2.3", &v));
    EXPECT_FALSE(parseVersion("1.02.3", &v));
    EXPECT_FALSE(parseVersion("1.2.03", &v));
    EXPECT_FALSE(parseVersion("1.2.3-01", &v));
    EXPECT_FALSE(parseVersion("1.2.3-alpha..1", &v));
}

static int cmp(const char *a, const char *b)
{
    int out = 0;
    EXPECT_TRUE(compareVersionStrings(a, b, &out));
    return out;
}

static void test_compare_core()
{
    EXPECT_EQ_INT(cmp("1.2.3", "1.2.3"), 0);
    EXPECT_EQ_INT(cmp("1.2.4", "1.2.3"), 1);
    EXPECT_EQ_INT(cmp("1.2.3", "1.2.4"), -1);
    EXPECT_EQ_INT(cmp("2.0.0", "1.99.99"), 1);
    EXPECT_EQ_INT(cmp("1.10.0", "1.2.0"), 1);
}

static void test_compare_prerelease()
{
    EXPECT_EQ_INT(cmp("1.2.3-alpha", "1.2.3"), -1);
    EXPECT_EQ_INT(cmp("1.2.3", "1.2.3-alpha"), 1);
    EXPECT_EQ_INT(cmp("1.2.3-alpha", "1.2.3-beta"), -1);
    EXPECT_EQ_INT(cmp("1.2.3-alpha.1", "1.2.3-alpha.2"), -1);
    EXPECT_EQ_INT(cmp("1.2.3-alpha.2", "1.2.3-alpha.10"), -1);
    EXPECT_EQ_INT(cmp("1.2.3-alpha.1", "1.2.3-alpha.beta"), -1);
    EXPECT_EQ_INT(cmp("1.2.3-rc.1", "1.2.3-rc.1.1"), -1);
}

static void test_policy_upgrade_only()
{
    int cmpOut = 0;
    EXPECT_TRUE(compareVersionStrings("1.2.4", "1.2.3", &cmpOut));
    EXPECT_TRUE(cmpOut > 0); // upgrade allowed by default

    EXPECT_TRUE(compareVersionStrings("1.2.2", "1.2.3", &cmpOut));
    EXPECT_TRUE(cmpOut < 0); // downgrade blocked unless force=true

    EXPECT_TRUE(compareVersionStrings("1.2.3", "1.2.3", &cmpOut));
    EXPECT_EQ_INT(cmpOut, 0); // noop/equal

    EXPECT_FALSE(compareVersionStrings("bad", "1.2.3", &cmpOut));
    EXPECT_FALSE(compareVersionStrings("1.2.3", "bad", &cmpOut));
}

static bool otaPolicyAllows(const char *current, const char *target, bool force)
{
    int cmpOut = 0;
    if (!compareVersionStrings(target, current, &cmpOut))
    {
        return false;
    }
    if (!force && cmpOut < 0)
    {
        return false;
    }
    return true;
}

static void test_policy_decisions()
{
    EXPECT_TRUE(otaPolicyAllows("1.2.3", "1.2.4", false)); // upgrade
    EXPECT_TRUE(otaPolicyAllows("1.2.3", "1.2.3", false)); // noop
    EXPECT_FALSE(otaPolicyAllows("1.2.3", "1.2.2", false)); // downgrade blocked
    EXPECT_TRUE(otaPolicyAllows("1.2.3", "1.2.2", true)); // forced downgrade
    EXPECT_FALSE(otaPolicyAllows("bad", "1.2.4", false)); // malformed current rejected
    EXPECT_FALSE(otaPolicyAllows("1.2.3", "bad", false)); // malformed target rejected
}

int main()
{
    test_parse_valid();
    test_parse_invalid();
    test_compare_core();
    test_compare_prerelease();
    test_policy_upgrade_only();
    test_policy_decisions();

    if (s_failures != 0)
    {
        std::fprintf(stderr, "semver tests failed: %d\n", s_failures);
        return 1;
    }

    std::printf("semver tests passed\n");
    return 0;
}
