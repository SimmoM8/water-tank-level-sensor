#include <Preferences.h>
#include "storage_nvs.h"

static Preferences prefs;

static const char *PREF_NAMESPACE = "level_sensor"; // NVS namespace

// ---------------- Calibration ----------------
static const char *PREF_KEY_DRY = "dry"; // NVS key for dry calibration value
static const char *PREF_KEY_WET = "wet"; // NVS key for wet calibration value
static const char *PREF_KEY_INV = "inv"; // NVS key for calibration inverted flag

// ---------------- Tank Configuration ----------------
static const char *PREF_KEY_TANK_VOL = "tank_vol";       // NVS key for tank volume in liters
static const char *PREF_KEY_TANK_HEIGHT = "tank_height"; // NVS key for rod length in cm

// ---------------- Simulation Configuration ----------------
static const char *PREF_KEY_SIM_ENABLED = "sim_en"; // NVS key for simulation enabled flag
static const char *PREF_KEY_SIM_MODE = "sim_mode";  // NVS key for simulation mode

bool storageBegin()
{
    // Open NVS namespace for read/write. Keep this open for the life of the firmware.
    prefs.begin(PREF_NAMESPACE, false);
    return true;
}

void storageEnd()
{
    prefs.end();
}

bool loadActiveCalibration(uint16_t &dry, uint16_t &wet, bool &inverted)
{
    dry = prefs.getUShort(PREF_KEY_DRY, 0);
    wet = prefs.getUShort(PREF_KEY_WET, 0);
    inverted = prefs.getBool(PREF_KEY_INV, false);

    // Return true only if we appear to have a usable calibration pair.
    return (dry != 0 && wet != 0);
}

bool loadTank(float &volumeLiters, float &tankHeightCm)
{
    const float DEFAULT_VOLUME_L = 0.0f;
    const float DEFAULT_HEIGHT_CM = 0.0f;

    tankVolumeLiters = prefs.getFloat(PREF_KEY_TANK_VOL, DEFAULT_VOLUME_L);
    rodLengthCm = prefs.getFloat(PREF_KEY_TANK_HEIGHT, DEFAULT_HEIGHT_CM);
    simulationEnabled = prefs.getBool(PREF_KEY_SIM_ENABLED, false);
    simulationMode = prefs.getUChar(PREF_KEY_SIM_MODE, 0);

    return true;
}

bool loadSimulation(bool &enabled, uint8_t &mode)
{
    const bool DEFAULT_ENABLED = false;
    const uint8_t DEFAULT_MODE = 0;

    enabled = prefs.getBool(PREF_KEY_SIM_ENABLED, DEFAULT_ENABLED);
    mode = (uint8_t)prefs.getUChar(PREF_KEY_SIM_MODE, DEFAULT_MODE);

    return true;
}