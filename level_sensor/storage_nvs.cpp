#include <Preferences.h>
#include "storage_nvs.h"
#include "logger.h"

static Preferences prefs;

static const char *PREF_NAMESPACE = "level_sensor"; // NVS namespace

// ---------------- Calibration ----------------
static const char *PREF_KEY_DRY = "dry"; // NVS key for dry calibration value
static const char *PREF_KEY_WET = "wet"; // NVS key for wet calibration value
static const char *PREF_KEY_INV = "inv"; // NVS key for calibration inverted flag

// ---------------- Tank Configuration ----------------
static const char *PREF_KEY_TANK_VOL = "tank_vol";       // NVS key for tank volume in liters
static const char *PREF_KEY_TANK_HEIGHT = "tank_height"; // NVS key for tank height in cm

// ---------------- Simulation Configuration ----------------
static const char *PREF_KEY_SIM_ENABLED = "sim_en"; // NVS key for simulation enabled flag
static const char *PREF_KEY_SIM_MODE = "sim_mode";  // NVS key for simulation mode

/*
/
*/
bool storage_begin()
{
    // Open NVS namespace for read/write. Keep this open for the life of the firmware.
    // Preferences::begin returns true on success.
    return prefs.begin(PREF_NAMESPACE, false);
}

void storage_end()
{
    prefs.end();
}

/*
/
*/
bool storage_loadActiveCalibration(int32_t &dry, int32_t &wet, bool &inverted)
{
    dry = prefs.getInt(PREF_KEY_DRY, 0);
    wet = prefs.getInt(PREF_KEY_WET, 0);
    inverted = prefs.getBool(PREF_KEY_INV, false);

    // Return true only if we appear to have a usable calibration pair.
    return (dry != 0 && wet != 0);
}

bool storage_loadTank(float &volumeLiters, float &tankHeightCm)
{
    const float DEFAULT_VOLUME_L = 0.0f;
    const float DEFAULT_HEIGHT_CM = 0.0f;

    volumeLiters = prefs.getFloat(PREF_KEY_TANK_VOL, DEFAULT_VOLUME_L);
    tankHeightCm = prefs.getFloat(PREF_KEY_TANK_HEIGHT, DEFAULT_HEIGHT_CM);

    // If you want this to reflect "has usable config", tighten this check later.
    return true;
}

bool storage_loadSimulation(bool &enabled, uint8_t &mode)
{
    const bool DEFAULT_ENABLED = false;
    const uint8_t DEFAULT_MODE = 0;

    enabled = prefs.getBool(PREF_KEY_SIM_ENABLED, DEFAULT_ENABLED);
    mode = (uint8_t)prefs.getUChar(PREF_KEY_SIM_MODE, DEFAULT_MODE);

    return true;
}

/*
/
*/
void storage_saveCalibrationDry(int32_t dry)
{
    prefs.putInt(PREF_KEY_DRY, dry);
}

void storage_saveCalibrationWet(int32_t wet)
{
    prefs.putInt(PREF_KEY_WET, wet);
}

void storage_saveCalibrationInverted(bool inverted)
{
    prefs.putBool(PREF_KEY_INV, inverted);
}

void storage_clearCalibration()
{
    prefs.remove(PREF_KEY_DRY);
    prefs.remove(PREF_KEY_WET);
    prefs.remove(PREF_KEY_INV);
}

void storage_saveTankVolume(float volumeLiters)
{
    prefs.putFloat(PREF_KEY_TANK_VOL, volumeLiters);
}

void storage_saveTankHeight(float tankHeightCm)
{
    prefs.putFloat(PREF_KEY_TANK_HEIGHT, tankHeightCm);
}

void storage_saveSimulationEnabled(bool enabled)
{
    prefs.putBool(PREF_KEY_SIM_ENABLED, enabled);
}

void storage_saveSimulationMode(uint8_t mode)
{
    prefs.putUChar(PREF_KEY_SIM_MODE, mode);
}

void storage_dump()
{
    int32_t dry = prefs.getInt(PREF_KEY_DRY, 0);
    int32_t wet = prefs.getInt(PREF_KEY_WET, 0);
    bool inv = prefs.getBool(PREF_KEY_INV, false);

    float vol = prefs.getFloat(PREF_KEY_TANK_VOL, 0.0f);
    float height = prefs.getFloat(PREF_KEY_TANK_HEIGHT, 0.0f);

    bool simEn = prefs.getBool(PREF_KEY_SIM_ENABLED, false);
    uint8_t simMode = (uint8_t)prefs.getUChar(PREF_KEY_SIM_MODE, 0);

    LOG_INFO(LogDomain::CONFIG, "NVS: dry=%ld wet=%ld inv=%s", (long)dry, (long)wet, inv ? "true" : "false");
    LOG_INFO(LogDomain::CONFIG, "NVS: tank_volume_l=%.2f tank_height_cm=%.2f", (double)vol, (double)height);
    LOG_INFO(LogDomain::CONFIG, "NVS: sim_enabled=%s sim_mode=%u", simEn ? "true" : "false", (unsigned)simMode);
}