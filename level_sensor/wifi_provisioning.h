#pragma once

#include <stdint.h>

struct WifiTimeSyncStatus
{
    bool valid;
    bool syncing;
    uint32_t lastAttemptMs;
    uint32_t lastSuccessMs;
    uint32_t nextRetryMs;
    const char *status;
};

// Initialize WiFi provisioning module (kept for future expansion)
void wifi_begin();

// Ensure WiFi is connected, otherwise start captive portal
void wifi_ensureConnected(uint32_t wifiTimeoutMs);

// Returns true once SNTP time has been synchronized.
bool wifi_timeIsValid();

// Periodic non-blocking NTP sync tick (safe to call from main loop).
void wifi_timeSyncTick();

// Returns current non-blocking time sync status for telemetry.
void wifi_getTimeSyncStatus(WifiTimeSyncStatus &out);

// Force captive portal on next loop without wiping credentials
void wifi_requestPortal();

// Wipe WiFi credentials and reboot into captive portal
void wifi_wipeCredentialsAndReboot();
