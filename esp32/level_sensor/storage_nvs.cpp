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

void storage_dump()
{
    int32_t dry = prefs.getInt(storage::nvs::kKeyDry, 0);
    int32_t wet = prefs.getInt(storage::nvs::kKeyWet, 0);
    bool inv = prefs.getBool(storage::nvs::kKeyInv, false);

    float vol = prefs.getFloat(storage::nvs::kKeyTankVol, 0.0f);
    float height = prefs.getFloat(storage::nvs::kKeyTankHeight, 0.0f);

    SenseMode sense = (SenseMode)prefs.getUChar(storage::nvs::kKeySenseMode, (uint8_t)SenseMode::TOUCH);
    uint8_t simMode = (uint8_t)prefs.getUChar(storage::nvs::kKeySimMode, 0);
    bool otaForce = prefs.getBool(storage::nvs::kKeyOtaForce, false);
    bool otaReboot = prefs.getBool(storage::nvs::kKeyOtaReboot, true);

    LOG_INFO(LogDomain::CONFIG, "NVS: dry=%ld wet=%ld inv=%s", (long)dry, (long)wet, inv ? "true" : "false");
    LOG_INFO(LogDomain::CONFIG, "NVS: tank_volume_l=%.2f tank_height_cm=%.2f", (double)vol, (double)height);
    LOG_INFO(LogDomain::CONFIG, "NVS: sense_mode=%s sim_mode=%u",
             domain_strings::c_str(domain_strings::to_string(sense)), (unsigned)simMode);
    LOG_INFO(LogDomain::CONFIG, "NVS: ota_force=%s ota_reboot=%s",
             otaForce ? "true" : "false", otaReboot ? "true" : "false");
}
