#pragma once

#include <stdint.h>
#include "device_state.h"

#ifndef CFG_OTA_EVENTS_QUEUE_DEPTH
#define CFG_OTA_EVENTS_QUEUE_DEPTH 48u
#endif

bool ota_events_begin();
bool ota_events_pushStatus(OtaStatus status);
bool ota_events_pushProgress(uint8_t progress);
bool ota_events_pushError(const char *errorText);
bool ota_events_pushFlatState(const char *stateStr,
                              uint8_t progress,
                              const char *errorText,
                              const char *targetVersion,
                              bool stamp);
bool ota_events_pushResult(const char *status, const char *message, uint32_t completedTs);
bool ota_events_pushClearActive();
bool ota_events_pushUpdateAvailable(bool value);
bool ota_events_pushLastSuccessTs(uint32_t ts);
bool ota_events_requestPublish();
bool ota_events_drainAndApply(DeviceState *state);
