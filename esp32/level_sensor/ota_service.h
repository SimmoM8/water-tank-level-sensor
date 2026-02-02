#pragma once
#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "device_state.h"

void ota_begin(const char *hostName, const char *password);
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

void ota_tick(DeviceState *state);

bool ota_isBusy();