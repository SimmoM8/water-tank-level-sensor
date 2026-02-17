#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "device_state.h"

void ota_begin(DeviceState *state, const char *hostName, const char *password);
void ota_handle();

// Pull-OTA (device pulls firmware from URL)
bool ota_pullStart(DeviceState *state,
                   const char *request_id,
                   const char *version,
                   const char *url,
                   const char *sha256,
                   bool force,
                   bool reboot,
                   char *errBuf,
                   size_t errBufLen);

// Pull-OTA via manifest URL (device fetches URL/SHA from manifest)
bool ota_pullStartFromManifest(DeviceState *state,
                               const char *request_id,
                               bool force,
                               bool reboot,
                               char *errBuf,
                               size_t errBufLen);

// Periodic manifest check for update availability.
bool ota_checkManifest(DeviceState *state, char *errBuf, size_t errBufLen);

void ota_confirmRunningApp();

bool ota_cancel(const char *reason);

bool ota_isBusy();
