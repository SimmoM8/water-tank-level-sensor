// Optional config overrides for water_level_sensor.ino
// Contract: values must be sane/positive; this file should only define overrides.
#pragma once
// Copy any of the defines below to change probe reliability thresholds.

#define CFG_PROBE_DISCONNECTED_BELOW_RAW 60000u
#define CFG_CAL_MIN_DIFF 20u
#define CFG_SPIKE_DELTA 1000000u
#define CFG_RAPID_FLUCTUATION_DELTA 500000u
// #define CFG_SPIKE_COUNT_THRESHOLD 3u
// #define CFG_SPIKE_WINDOW_MS 5000u
// #define CFG_STUCK_EPS 2u
// #define CFG_STUCK_MS 8000u
// #define CFG_PROBE_MIN_RAW 0u
#define CFG_PROBE_MAX_RAW 1000000u
// #define CFG_CAL_RECOMMEND_MARGIN 2000u
// #define CFG_CAL_RECOMMEND_COUNT 3u
// #define CFG_CAL_RECOMMEND_WINDOW_MS 60000u
// #define CFG_ZERO_HIT_COUNT 2u
// #define CFG_ZERO_WINDOW_MS 5000u

// #define CFG_SERIAL_CMD_BUF 512u // default 256 bytes for incoming serial command buffer
#ifndef CFG_LOG_COLOR
#define CFG_LOG_COLOR 0 // ANSI colorized Serial logs (0=off, 1=on)
#endif
#ifndef CFG_LOG_HIGH_FREQ_DEFAULT
#define CFG_LOG_HIGH_FREQ_DEFAULT 1 // High-frequency DEBUG/trace logs at boot (0=off, 1=on)
#endif

// — Tank Volume Number entity (display/input) —
#define CFG_TANK_VOLUME_UNIT "L" // "L" or "mL"
#define CFG_TANK_VOLUME_MAX 30000.0f
// #define CFG_TANK_VOLUME_STEP 1.0f

// — Rod Length Number entity (display/input) —
// #define CFG_ROD_LENGTH_UNIT "cm"    // "cm" or "m"
// #define CFG_ROD_LENGTH_MAX 300.0f
// #define CFG_ROD_LENGTH_STEP 1.0f

// — Sampling / smoothing —
#define CFG_RAW_SAMPLE_MS 1000u
#define CFG_PERCENT_SAMPLE_MS 1000u // the interval in milliseconds to update the smoothed percent level
#define CFG_PERCENT_EMA_ALPHA 1.0f

// — OTA —
#define CFG_OTA_MANIFEST_URL "https://github.com/SimmoM8/water-tank-level-sensor/releases/latest/download/dev.json"
#define CFG_OTA_TLS_PREFER_CRT_BUNDLE 1 // default 1 uses embedded fallback CA chain
#ifndef CFG_OTA_DEV_LOGS
#define CFG_OTA_DEV_LOGS 0 // OTA verbose trace/health/chunk logs (0=normal production logs, 1=dev verbose)
#endif
#ifndef CFG_OTA_PROGRESS_NEWLINES
#define CFG_OTA_PROGRESS_NEWLINES 0 // OTA progress style (0=in-place carriage return, 1=print each update on new line)
#endif
// — Time sync (non-blocking NTP) —
// #define CFG_TIME_SYNC_TIMEOUT_MS 20000u
// #define CFG_TIME_SYNC_RETRY_MIN_MS 5000u
// #define CFG_TIME_SYNC_RETRY_MAX_MS 300000u
// Optional OTA safety guardrails (disabled by default):
// #define CFG_OTA_GUARD_REQUIRE_MQTT_CONNECTED 1   // reject OTA if MQTT is not connected
// #define CFG_OTA_GUARD_MIN_WIFI_RSSI -80          // reject OTA if RSSI is below threshold (dBm)
