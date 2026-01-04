

#pragma once

#include <Preferences.h>
#include <stdint.h>

// Initialize WiFi provisioning module (kept for future expansion)
void wifiProvisioningBegin(Preferences &prefs);

// Ensure WiFi is connected, otherwise start captive portal
void wifiEnsureConnected(Preferences &prefs, uint32_t wifiTimeoutMs);

// Force captive portal on next loop without wiping credentials
void wifiEnterSetupMode(Preferences &prefs);

// Wipe WiFi credentials and reboot into captive portal
void wifiFactoryReset(Preferences &prefs);