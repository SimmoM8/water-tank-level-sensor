// Optional config overrides for water_level_sensor.ino
// Copy any of the defines below to change probe reliability thresholds.
// This file contains no secrets and is safe to commit (or add to .gitignore if preferred).

#define CFG_PROBE_DISCONNECTED_BELOW_RAW 25000u
#define CFG_CAL_MIN_DIFF 20u
// #define CFG_SPIKE_DELTA 10000u
// #define CFG_RAPID_FLUCTUATION_DELTA 5000u
// #define CFG_SPIKE_COUNT_THRESHOLD 3u
// #define CFG_SPIKE_WINDOW_MS 5000u
// #define CFG_STUCK_EPS 2u
// #define CFG_STUCK_MS 8000u
// #define CFG_PROBE_MIN_RAW 0u
// #define CFG_PROBE_MAX_RAW 65535u
