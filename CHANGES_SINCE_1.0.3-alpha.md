# Changes Since v1.0.3-alpha

**Release Tag:** v1.0.3-alpha  
**Tag Commit:** `71a5ee9` - "chore(release): bump version to 1.0.3-alpha"  
**Release Date:** 2026-02-18  
**Current Main Branch:** `f4d9976` - "feat(mqtt,ha): robust Home Assistant discovery and MQTT session diagnostics"

## Overview

Since the v1.0.3-alpha release (the first successful OTA end-to-end release), there has been **1 new commit** on the main branch that introduces significant improvements to MQTT connectivity, Home Assistant discovery, and logging diagnostics.

---

## Summary of Changes

### 📦 **Commit: f4d9976 - "feat(mqtt,ha): robust Home Assistant discovery and MQTT session diagnostics"**
**Author:** SimmoM8  
**Date:** 2026-02-18 20:17:32 +0100

This commit adds robust retry logic, enhanced diagnostics, and improved logging for MQTT and Home Assistant integration.

---

## Detailed Feature Additions and Changes

### 🔧 **1. Home Assistant Discovery Enhancements**

#### **Retry Logic and Result Handling**
- **Added `ha_discovery_publishAll` return value**: Now returns a result enum to indicate success/failure
- **Improved error handling**: Discovery failures are now properly detected and reported
- **Retry mechanism**: Failed discoveries can be retried automatically

#### **Enhanced Logging**
- **Dev/Non-Dev Log Modes**: 
  - Non-dev mode shows concise, user-friendly messages (e.g., "MQTT: Home Assistant discovery failed (will retry)")
  - Dev mode shows detailed diagnostic information including entity names, topics, and byte counts
- **Rate-limited warnings**: Uses `logger_logEvery()` to avoid log spam (60-second intervals)
- **Payload size diagnostics**: Logs when discovery payloads are too large with entity-specific details

#### **Refactored Publishing Logic**
- **New `publishDiscoveryPayload()` helper function**: Centralizes publishing logic for all entity types
  - Validates payload before publishing
  - Provides consistent error handling
  - Logs detailed diagnostics in dev mode
- **Applied to all entity types**: Sensors, binary sensors, buttons, numbers, switches, and selects

#### **Configuration Support**
- **`CFG_LOG_DEV` and `CFG_OTA_DEV_LOGS` integration**: Dynamically adjusts logging verbosity based on configuration
- **`ha_devLogsEnabled()` helper**: Determines whether to show detailed logs

---

### 🌐 **2. MQTT Transport Improvements**

#### **Connection Diagnostics**
- **State tracking variables**:
  - `s_loggedFirstConnectAttempt`: Tracks if initial connection attempt was logged
  - `s_seenConnectFailure`: Records if any connection failure has occurred
  - `s_lastConnected`: Previous connection state for change detection
  - `s_rxConfirmedForSession`: Confirms successful message reception
  - `s_connectionSubscribed`: Tracks subscription status
  - `s_connectionOnlinePublished`: Tracks if "online" status was published

#### **Enhanced Error Reporting**
- **`mqtt_stateToString()` function**: Converts MQTT state codes to readable strings
  - Examples: "timeout", "connection_lost", "bad_credentials", "not_authorized"
- **`mqtt_stateHint()` function**: Provides actionable troubleshooting hints
  - Example hints: "check MQTT username/password", "check broker IP/network"
- **Detailed connection failure logging**: Now logs state code, state name, and helpful hints

#### **Session Readiness Detection**
- **`mqtt_isReadyForSession()` function**: Determines when MQTT session is fully established
  - Checks if subscribed to command topics
  - Verifies "online" status was published
  - Ensures Home Assistant discovery is complete
- **`mqtt_logReadyIfComplete()` function**: Logs "MQTT: Ready ✓" when session is fully operational
  - Concise message in non-dev mode
  - Detailed status in dev mode showing all readiness conditions

#### **Discovery Retry Management**
- **`s_discoveryPending` flag**: Tracks if discovery needs to be (re)attempted
- **`s_discoveryRetryAtMs` timestamp**: Schedules next retry attempt
- **60-second retry interval**: Automatic retry for failed discoveries

#### **RX Confirmation Logging**
- **`s_rxConfirmedForSession` flag**: Confirms the device can receive MQTT messages
- **Logs when first message is received**: Provides confirmation of bidirectional MQTT communication

---

### 📊 **3. State JSON Diagnostics**

#### **Enhanced State Publishing Diagnostics**
- **`logStateJsonDiag()` function**: Logs detailed information about state JSON serialization
  - Tracks bytes used, required capacity, and output buffer size
  - Reports field count and write operations
  - Detects overflow and empty root conditions
- **Error reason strings**: Human-readable error codes
  - Examples: "ok", "empty", "doc_overflow", "out_too_small", "serialize_failed"

#### **State Build Pausing**
- **`s_stateBuildPaused` flag**: Can pause state updates (useful during OTA)
- **Periodic "still paused" logs**: Logs every 60 seconds when state publishing is paused

---

### 📝 **4. Logger Module Enhancements**

#### **Serial Output Mutex**
- **Thread-safe serial logging**: Added `SemaphoreHandle_t s_serialMutex`
  - Prevents interleaved output from multiple tasks
  - `logger_serialLock()` and `logger_serialUnlock()` public API
  - `logger_serialEnsureLineBreak()`: Ensures clean line breaks for inline output

#### **OTA Quiet Mode**
- **`logger_setOtaQuietMode()` and `logger_isOtaQuietMode()` functions**:
  - Suppresses INFO and DEBUG logs from SYSTEM, WIFI, and MQTT domains during OTA updates
  - Reduces log noise during firmware updates
  - Critical errors and warnings still logged

#### **Inline Serial Output Support**
- **`s_serialInlineActive` flag**: Tracks if inline (non-line-break) output is active
- **`logger_serialSetInlineActive()` function**: Allows progress indicators (e.g., download progress bars)
- **Automatic line break insertion**: Ensures log messages don't corrupt inline output

---

### ⚙️ **5. Configuration Updates**

#### **New Configuration Options** (in `level_sensor/include/config.h`)
- **`CFG_LOG_HIGH_FREQ_DEFAULT`**: Default value for high-frequency logging
- **`CFG_LOG_DEV`**: Master switch for development/verbose logging
- **`CFG_OTA_DEV_LOGS`**: Specific control for OTA-related verbose logs

#### **Version Bump** (in `level_sensor/include/version.h`)
- Version information updated to reflect this commit (details depend on versioning scheme)

---

### 🗂️ **6. Main Loop Improvements** (in `level_sensor/src/main.cpp`)

- **Enhanced MQTT connection logging at boot**: Uses new dev/non-dev log modes
- **Session readiness checks**: Integrates new MQTT session diagnostics
- **Improved state publishing flow**: Better handling of state build errors with diagnostics

---

### 🛡️ **7. OTA Service Improvements** (in `level_sensor/src/ota_service.cpp`)

- **Enhanced diagnostic logging**: More detailed OTA progress and error reporting
- **Integration with OTA quiet mode**: Reduces log spam during firmware downloads and installations
- **Better error context**: Uses dev logs to provide actionable troubleshooting information

---

## Files Modified

| File | Lines Changed | Description |
|------|---------------|-------------|
| `.gitignore` | +2 | Additional entries for build artifacts |
| `level_sensor/include/config.h` | +25/-0 | New configuration options for logging |
| `level_sensor/include/ha_discovery.h` | +10/-0 | New return types and function signatures |
| `level_sensor/include/logger.h` | +8/-0 | New public API for serial mutex and OTA quiet mode |
| `level_sensor/include/state_json.h` | +27/-0 | New diagnostic structures |
| `level_sensor/include/version.h` | +2/-0 | Version bump |
| `level_sensor/src/ha_discovery.cpp` | +220/-0 | Retry logic, diagnostics, and refactored publishing |
| `level_sensor/src/logger.cpp` | +98/-0 | Serial mutex, OTA quiet mode, inline output support |
| `level_sensor/src/main.cpp` | +48/-0 | Integration of new diagnostics and session checks |
| `level_sensor/src/mqtt_transport.cpp` | +540/-0 | Comprehensive MQTT diagnostics and session management |
| `level_sensor/src/ota_service.cpp` | +817/-0 | Enhanced OTA logging and diagnostics |
| `level_sensor/src/state_json.cpp` | +102/-0 | State JSON diagnostics implementation |
| `manifests/dev.json` | +8/-8 | Updated to reference v1.0.3-alpha firmware |

**Total:** 13 files changed, **1,677 insertions**, **230 deletions**

---

## Key Benefits

### 🎯 **For Developers**
- **Better debugging**: Verbose dev logs provide deep insight into MQTT and HA discovery operations
- **Cleaner serial output**: Thread-safe logging prevents garbled output
- **Progress indicators**: Inline output support allows for OTA download progress bars

### 🎯 **For End Users (Non-Dev Mode)**
- **Concise, friendly messages**: Easy-to-understand status updates (e.g., "MQTT: Ready ✓")
- **Actionable error hints**: Clear guidance when something goes wrong (e.g., "check MQTT username/password")
- **Reduced log noise**: OTA quiet mode keeps logs clean during firmware updates

### 🎯 **For System Reliability**
- **Automatic retry**: Home Assistant discovery retries on failure
- **Session readiness detection**: Ensures MQTT connection is fully established before normal operations
- **Better error handling**: Comprehensive diagnostics help identify and resolve issues quickly

---

## Testing Recommendations

Before promoting this to a new release, test the following scenarios:

1. **MQTT Connection Failures**:
   - Verify error messages and hints are helpful
   - Confirm automatic retry works

2. **Home Assistant Discovery**:
   - Test with all entity types (sensors, buttons, numbers, switches, selects)
   - Verify retry logic when broker is temporarily unavailable

3. **OTA Updates**:
   - Confirm OTA quiet mode reduces log spam
   - Verify inline progress indicators work correctly
   - Ensure critical errors still appear during OTA

4. **Serial Output**:
   - Test multi-task scenarios to confirm no interleaved output
   - Verify line breaks are handled correctly

5. **Dev vs Non-Dev Mode**:
   - Compare log output with `CFG_LOG_DEV=0` and `CFG_LOG_DEV=1`
   - Ensure non-dev mode is concise and user-friendly

---

## Migration Notes

This commit is **backward compatible** with v1.0.3-alpha. No configuration changes are required. However, to take advantage of new features:

- **Enable dev logs**: Set `CFG_LOG_DEV=1` in `config.h` for verbose diagnostics
- **Tune OTA logs**: Set `CFG_OTA_DEV_LOGS=1` for detailed OTA debugging (independent of `CFG_LOG_DEV`)

---

## Next Steps

To create a new release (e.g., v1.0.4-alpha or v1.1.0):

1. **Test thoroughly** on hardware
2. **Update version** in `level_sensor/include/version.h`
3. **Build firmware** and calculate SHA256
4. **Create GitHub Release** with firmware binary
5. **Update manifest** (`manifests/dev.json` or `manifests/stable.json`)
6. **Tag the release** (e.g., `v1.1.0`)

---

## Conclusion

The single commit since v1.0.3-alpha (`f4d9976`) represents a **significant improvement in robustness, diagnostics, and user experience**. It does not add new features to the water tank sensor itself, but rather makes the device **more reliable, easier to debug, and more user-friendly** through better MQTT/HA integration and logging.

**Recommendation:** This is a strong candidate for a **v1.1.0** stable release after thorough testing.
