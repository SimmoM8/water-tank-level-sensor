// Optional config overrides for water_level_sensor.ino
// Copy any of the defines below to change probe reliability thresholds.
// This file contains no secrets and is safe to commit (or add to .gitignore if preferred).

#define CFG_PROBE_DISCONNECTED_BELOW_RAW 10000u
#define CFG_CAL_MIN_DIFF 20u
// #define CFG_SPIKE_DELTA 10000u
// #define CFG_RAPID_FLUCTUATION_DELTA 5000u
// #define CFG_SPIKE_COUNT_THRESHOLD 3u
// #define CFG_SPIKE_WINDOW_MS 5000u
// #define CFG_STUCK_EPS 2u
// #define CFG_STUCK_MS 8000u
// #define CFG_PROBE_MIN_RAW 0u
// #define CFG_PROBE_MAX_RAW 65535u

// — Tank Volume Number entity (display/input) —
#define CFG_TANK_VOLUME_UNIT "mL" // "L" or "mL"
#define CFG_TANK_VOLUME_MAX 30000.0f
// #define CFG_TANK_VOLUME_STEP 1.0f

// — Rod Length Number entity (display/input) —
// #define CFG_ROD_LENGTH_UNIT "cm"    // "cm" or "m"
// #define CFG_ROD_LENGTH_MAX 300.0f
// #define CFG_ROD_LENGTH_STEP 1.0f
