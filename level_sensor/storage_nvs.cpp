#include <Preferences.h>
#include "storage_nvs.h"

static Preferences prefs;

static const char *PREF_NAMESPACE = "level_sensor";

bool storageBegin()
{
    // Open NVS namespace for read/write. Keep this open for the life of the firmware.
    prefs.begin(PREF_NAMESPACE, false);
    return true;
}