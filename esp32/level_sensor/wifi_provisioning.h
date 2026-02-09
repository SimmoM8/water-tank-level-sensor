#pragma once

#include <stdint.h>

// Initialize WiFi provisioning module (kept for future expansion)
void wifi_begin();

// Ensure WiFi is connected, otherwise start captive portal
void wifi_ensureConnected(uint32_t wifiTimeoutMs);

// Returns true once SNTP time has been synchronized.
bool wifi_timeIsValid();

// Force captive portal on next loop without wiping credentials
void wifi_requestPortal();

// Wipe WiFi credentials and reboot into captive portal
void wifi_wipeCredentialsAndReboot();
