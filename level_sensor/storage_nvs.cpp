#include <Preferences.h>
#include <limits>
#include "storage_nvs.h"
#include "logger.h"
#include "domain_strings.h"
#include "config.h"

static Preferences prefs;

namespace storage
{
namespace nvs
{
static constexpr const char kNamespace[] = "level_sensor";
static constexpr const char kSchemaKey[] = "schema";
static constexpr uint32_t kSchemaVersion = 1;

// ---------------- Calibration ----------------
static constexpr const char kKeyDry[] = "dry";
static constexpr const char kKeyWet[] = "wet";
static constexpr const char kKeyInv[] = "inv";

// ---------------- Tank Configuration ----------------
static constexpr const char kKeyTankVol[] = "tank_vol";
static constexpr const char kKeyTankHeight[] = "tank_height";

// ---------------- Simulation Configuration ----------------
static constexpr const char kKeySenseMode[] = "sense_mode";
static constexpr const char kKeySimMode[] = "sim_mode";

// ---------------- OTA Options ----------------
static constexpr const char kKeyOtaForce[] = "ota_force";
static constexpr const char kKeyOtaReboot[] = "ota_reboot";
static constexpr const char kKeyOtaLastSuccess[] = "ota_last_ok";
static constexpr const char kKeyBootCount[] = "boot_count";

static constexpr uint32_t kWarnThrottleMs = 5000;
} // namespace nvs
} // namespace storage

// Policy: on schema mismatch, clear all keys in this namespace and write the new version.
bool storage_begin()
{
    const bool ok = prefs.begin(storage::nvs::kNamespace, false);
    if (!ok)
    {
        LOG_ERROR(LogDomain::CONFIG, "NVS: begin failed namespace=%s", storage::nvs::kNamespace);
        return false;
    }

    const uint32_t ver = prefs.getUInt(storage::nvs::kSchemaKey, 0);
    if (ver != storage::nvs::kSchemaVersion)
    {
        LOG_WARN_EVERY("nvs_schema_mismatch", storage::nvs::kWarnThrottleMs, LogDomain::CONFIG,
                       "NVS: schema mismatch stored=%lu expected=%lu; clearing",
                       (unsigned long)ver, (unsigned long)storage::nvs::kSchemaVersion);
        const bool cleared = prefs.clear();
        if (!cleared)
        {
            LOG_WARN_EVERY("nvs_clear_failed", storage::nvs::kWarnThrottleMs, LogDomain::CONFIG,
                           "NVS: clear failed namespace=%s", storage::nvs::kNamespace);
        }
        const size_t written = prefs.putUInt(storage::nvs::kSchemaKey, storage::nvs::kSchemaVersion);
        if (written == 0)
        {
            LOG_WARN_EVERY("nvs_schema_write_failed", storage::nvs::kWarnThrottleMs, LogDomain::CONFIG,
                           "NVS: failed to store schema version");
        }
    }

    return true;
}

void storage_end()
{
    prefs.end();
}

bool storage_loadActiveCalibration(int32_t &dry, int32_t &wet, bool &inverted)
{
    dry = prefs.getInt(storage::nvs::kKeyDry, 0);
    wet = prefs.getInt(storage::nvs::kKeyWet, 0);
    inverted = prefs.getBool(storage::nvs::kKeyInv, false);

    const int32_t origDry = dry;
    const int32_t origWet = wet;
    bool ok = (dry != 0) && (wet != 0) && (dry != wet);
#ifdef CFG_PROBE_MAX_RAW
    if (dry < 0 || wet < 0 || (uint32_t)dry > CFG_PROBE_MAX_RAW || (uint32_t)wet > CFG_PROBE_MAX_RAW)
    {
        ok = false;
    }
#endif
    if (!ok)
    {
        dry = 0;
        wet = 0;
        inverted = false;
        LOG_WARN_EVERY("nvs_cal_invalid", storage::nvs::kWarnThrottleMs, LogDomain::CAL,
                       "NVS: invalid calibration dry=%ld wet=%ld", (long)origDry, (long)origWet);
    }
    return ok;
}

bool storage_loadTank(float &volumeLiters, float &tankHeightCm)
{
    const bool hasVol = prefs.isKey(storage::nvs::kKeyTankVol);
    const bool hasHeight = prefs.isKey(storage::nvs::kKeyTankHeight);

    volumeLiters = hasVol ? prefs.getFloat(storage::nvs::kKeyTankVol, 0.0f) : std::numeric_limits<float>::quiet_NaN();
    tankHeightCm = hasHeight ? prefs.getFloat(storage::nvs::kKeyTankHeight, 0.0f) : std::numeric_limits<float>::quiet_NaN();

    bool ok = true;
    if (!(volumeLiters > 0.0f) || !(tankHeightCm > 0.0f))
    {
        ok = false;
    }
#ifdef CFG_TANK_VOLUME_MAX
    if (volumeLiters > CFG_TANK_VOLUME_MAX)
    {
        ok = false;
    }
#endif
#ifdef CFG_ROD_LENGTH_MAX
    if (tankHeightCm > CFG_ROD_LENGTH_MAX)
    {
        ok = false;
    }
#endif

    if (!ok)
    {
        volumeLiters = std::numeric_limits<float>::quiet_NaN();
        tankHeightCm = std::numeric_limits<float>::quiet_NaN();
        LOG_WARN_EVERY("nvs_tank_invalid", storage::nvs::kWarnThrottleMs, LogDomain::CONFIG,
                       "NVS: invalid tank config vol=%.2f height=%.2f", (double)volumeLiters, (double)tankHeightCm);
    }

    return ok && hasVol && hasHeight;
}

bool storage_loadSimulation(SenseMode &senseMode, uint8_t &mode)
{
    static constexpr uint8_t kSenseMin = (uint8_t)SenseMode::TOUCH;
    static constexpr uint8_t kSenseMax = (uint8_t)SenseMode::SIM;
    static constexpr uint8_t kSimModeMax = 6;

    const uint8_t senseRaw = prefs.getUChar(storage::nvs::kKeySenseMode, kSenseMin);
    const uint8_t modeRaw = prefs.getUChar(storage::nvs::kKeySimMode, 0);

    bool ok = true;
    if (senseRaw < kSenseMin || senseRaw > kSenseMax)
    {
        senseMode = SenseMode::TOUCH;
        ok = false;
    }
    else
    {
        senseMode = static_cast<SenseMode>(senseRaw);
    }

    if (modeRaw > kSimModeMax)
    {
        mode = 0;
        ok = false;
    }
    else
    {
        mode = modeRaw;
    }

    if (!ok)
    {
        LOG_WARN_EVERY("nvs_sim_invalid", storage::nvs::kWarnThrottleMs, LogDomain::CONFIG,
                       "NVS: invalid simulation config sense=%u mode=%u", (unsigned)senseRaw, (unsigned)modeRaw);
    }
    return ok;
}

bool storage_loadOtaOptions(bool &force, bool &reboot)
{
    const bool hasForce = prefs.isKey(storage::nvs::kKeyOtaForce);
    const bool hasReboot = prefs.isKey(storage::nvs::kKeyOtaReboot);

    force = prefs.getBool(storage::nvs::kKeyOtaForce, false);
    reboot = prefs.getBool(storage::nvs::kKeyOtaReboot, true);

    return hasForce || hasReboot;
}

bool storage_loadOtaLastSuccess(uint32_t &ts)
{
    const bool hasTs = prefs.isKey(storage::nvs::kKeyOtaLastSuccess);
    ts = prefs.getUInt(storage::nvs::kKeyOtaLastSuccess, 0);
    return hasTs;
}

bool storage_loadBootCount(uint32_t &count)
{
    const bool hasCount = prefs.isKey(storage::nvs::kKeyBootCount);
    count = prefs.getUInt(storage::nvs::kKeyBootCount, 0u);
    return hasCount;
}

void storage_saveCalibrationDry(int32_t dry)
{
    prefs.putInt(storage::nvs::kKeyDry, dry);
}

void storage_saveCalibrationWet(int32_t wet)
{
    prefs.putInt(storage::nvs::kKeyWet, wet);
}

void storage_saveCalibrationInverted(bool inverted)
{
    prefs.putBool(storage::nvs::kKeyInv, inverted);
}

void storage_clearCalibration()
{
    prefs.remove(storage::nvs::kKeyDry);
    prefs.remove(storage::nvs::kKeyWet);
    prefs.remove(storage::nvs::kKeyInv);
}

void storage_saveTankVolume(float volumeLiters)
{
    prefs.putFloat(storage::nvs::kKeyTankVol, volumeLiters);
}

void storage_saveTankHeight(float tankHeightCm)
{
    prefs.putFloat(storage::nvs::kKeyTankHeight, tankHeightCm);
}

void storage_saveSimulationMode(uint8_t mode)
{
    prefs.putUChar(storage::nvs::kKeySimMode, mode);
}

void storage_saveSenseMode(SenseMode senseMode)
{
    prefs.putUChar(storage::nvs::kKeySenseMode, (uint8_t)senseMode);
}

void storage_saveOtaForce(bool force)
{
    prefs.putBool(storage::nvs::kKeyOtaForce, force);
}

void storage_saveOtaReboot(bool reboot)
{
    prefs.putBool(storage::nvs::kKeyOtaReboot, reboot);
}

void storage_saveOtaLastSuccess(uint32_t ts)
{
    prefs.putUInt(storage::nvs::kKeyOtaLastSuccess, ts);
}

void storage_saveBootCount(uint32_t count)
{
    prefs.putUInt(storage::nvs::kKeyBootCount, count);
}

static inline void fnv1aMixByte(uint32_t &h, uint8_t b)
{
    h ^= (uint32_t)b;
    h *= 16777619u;
}

static inline void fnv1aMixBool(uint32_t &h, bool v)
{
    fnv1aMixByte(h, v ? 1u : 0u);
}

static inline void fnv1aMixU32(uint32_t &h, uint32_t v)
{
    fnv1aMixByte(h, (uint8_t)(v & 0xFFu));
    fnv1aMixByte(h, (uint8_t)((v >> 8) & 0xFFu));
    fnv1aMixByte(h, (uint8_t)((v >> 16) & 0xFFu));
    fnv1aMixByte(h, (uint8_t)((v >> 24) & 0xFFu));
}

static inline void fnv1aMixI32(uint32_t &h, int32_t v)
{
    fnv1aMixU32(h, (uint32_t)v);
}

static inline void fnv1aMixFloat(uint32_t &h, float v)
{
    union
    {
        float f;
        uint32_t u;
    } conv{};
    conv.f = v;
    fnv1aMixU32(h, conv.u);
}

void storage_dump()
{
    const bool hasSchema = prefs.isKey(storage::nvs::kSchemaKey);
    const uint32_t schema = prefs.getUInt(storage::nvs::kSchemaKey, 0u);

    const bool hasDry = prefs.isKey(storage::nvs::kKeyDry);
    const bool hasWet = prefs.isKey(storage::nvs::kKeyWet);
    const bool hasInv = prefs.isKey(storage::nvs::kKeyInv);
    int32_t dry = prefs.getInt(storage::nvs::kKeyDry, 0);
    int32_t wet = prefs.getInt(storage::nvs::kKeyWet, 0);
    bool inv = prefs.getBool(storage::nvs::kKeyInv, false);

    const bool hasVol = prefs.isKey(storage::nvs::kKeyTankVol);
    const bool hasHeight = prefs.isKey(storage::nvs::kKeyTankHeight);
    float vol = prefs.getFloat(storage::nvs::kKeyTankVol, 0.0f);
    float height = prefs.getFloat(storage::nvs::kKeyTankHeight, 0.0f);

    const bool hasSense = prefs.isKey(storage::nvs::kKeySenseMode);
    const bool hasSimMode = prefs.isKey(storage::nvs::kKeySimMode);
    uint8_t senseRaw = (uint8_t)prefs.getUChar(storage::nvs::kKeySenseMode, (uint8_t)SenseMode::TOUCH);
    uint8_t simMode = (uint8_t)prefs.getUChar(storage::nvs::kKeySimMode, 0);

    const bool senseValid =
        (senseRaw >= (uint8_t)SenseMode::TOUCH) &&
        (senseRaw <= (uint8_t)SenseMode::SIM);
    const SenseMode sense = senseValid ? static_cast<SenseMode>(senseRaw) : SenseMode::TOUCH;
    const char *senseText = senseValid ? domain_strings::c_str(domain_strings::to_string(sense)) : "unknown";

    const bool hasOtaForce = prefs.isKey(storage::nvs::kKeyOtaForce);
    const bool hasOtaReboot = prefs.isKey(storage::nvs::kKeyOtaReboot);
    const bool hasOtaLastOk = prefs.isKey(storage::nvs::kKeyOtaLastSuccess);
    const bool hasBootCount = prefs.isKey(storage::nvs::kKeyBootCount);
    bool otaForce = prefs.getBool(storage::nvs::kKeyOtaForce, false);
    bool otaReboot = prefs.getBool(storage::nvs::kKeyOtaReboot, true);
    uint32_t otaLastOk = prefs.getUInt(storage::nvs::kKeyOtaLastSuccess, 0);
    uint32_t bootCount = prefs.getUInt(storage::nvs::kKeyBootCount, 0u);

    // Deterministic marker over presence + values to detect unexpected NVS drift.
    uint32_t marker = 2166136261u; // FNV-1a 32-bit offset basis
    fnv1aMixBool(marker, hasSchema);
    fnv1aMixU32(marker, schema);
    fnv1aMixBool(marker, hasDry);
    fnv1aMixI32(marker, dry);
    fnv1aMixBool(marker, hasWet);
    fnv1aMixI32(marker, wet);
    fnv1aMixBool(marker, hasInv);
    fnv1aMixBool(marker, inv);
    fnv1aMixBool(marker, hasVol);
    fnv1aMixFloat(marker, vol);
    fnv1aMixBool(marker, hasHeight);
    fnv1aMixFloat(marker, height);
    fnv1aMixBool(marker, hasSense);
    fnv1aMixByte(marker, senseRaw);
    fnv1aMixBool(marker, hasSimMode);
    fnv1aMixByte(marker, simMode);
    fnv1aMixBool(marker, hasOtaForce);
    fnv1aMixBool(marker, otaForce);
    fnv1aMixBool(marker, hasOtaReboot);
    fnv1aMixBool(marker, otaReboot);
    fnv1aMixBool(marker, hasOtaLastOk);
    fnv1aMixU32(marker, otaLastOk);
    fnv1aMixBool(marker, hasBootCount);
    fnv1aMixU32(marker, bootCount);

    LOG_INFO(LogDomain::CONFIG,
             "NVS dump v1 schema=%lu expected=%lu marker=0x%08lX",
             (unsigned long)schema,
             (unsigned long)storage::nvs::kSchemaVersion,
             (unsigned long)marker);
    LOG_INFO(LogDomain::CONFIG,
             "NVS cal has[dry=%s wet=%s inv=%s] dry=%ld wet=%ld inv=%s",
             hasDry ? "y" : "n",
             hasWet ? "y" : "n",
             hasInv ? "y" : "n",
             (long)dry,
             (long)wet,
             inv ? "true" : "false");
    LOG_INFO(LogDomain::CONFIG,
             "NVS tank has[vol=%s height=%s] tank_volume_l=%.2f tank_height_cm=%.2f",
             hasVol ? "y" : "n",
             hasHeight ? "y" : "n",
             (double)vol,
             (double)height);
    LOG_INFO(LogDomain::CONFIG,
             "NVS sim has[sense=%s mode=%s] sense_mode=%s(raw=%u) sim_mode=%u",
             hasSense ? "y" : "n",
             hasSimMode ? "y" : "n",
             senseText,
             (unsigned)senseRaw,
             (unsigned)simMode);
    LOG_INFO(LogDomain::CONFIG,
             "NVS ota has[force=%s reboot=%s last_ok=%s] ota_force=%s ota_reboot=%s ota_last_success_ts=%lu",
             hasOtaForce ? "y" : "n",
             hasOtaReboot ? "y" : "n",
             hasOtaLastOk ? "y" : "n",
             otaForce ? "true" : "false",
             otaReboot ? "true" : "false",
             (unsigned long)otaLastOk);
    LOG_INFO(LogDomain::CONFIG,
             "NVS boot has[count=%s] boot_count=%lu",
             hasBootCount ? "y" : "n",
             (unsigned long)bootCount);
}
