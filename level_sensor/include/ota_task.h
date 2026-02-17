#pragma once

#include <stddef.h>
#include <stdint.h>
#include "device_state.h"

#ifndef CFG_OTA_TASK_STACK_WORDS
#define CFG_OTA_TASK_STACK_WORDS 4096u
#endif

#ifndef CFG_OTA_TASK_PRIORITY
#define CFG_OTA_TASK_PRIORITY 2u
#endif

#ifndef CFG_OTA_TASK_QUEUE_DEPTH
#define CFG_OTA_TASK_QUEUE_DEPTH 2u
#endif

#ifndef CFG_OTA_TASK_CORE
#define CFG_OTA_TASK_CORE 1
#endif

struct OtaTaskJob
{
    char request_id[OTA_REQUEST_ID_MAX] = {0};
    char version[OTA_VERSION_MAX] = {0};
    char url[OTA_URL_MAX] = {0};
    char sha256[OTA_SHA256_MAX] = {0};
    bool force = false;
    bool reboot = true;
};

bool ota_taskBegin(DeviceState *state);
bool ota_taskEnqueue(const OtaTaskJob &job);
bool ota_taskRequestCancel(const char *reason);
bool ota_taskCancelAll(const char *reason);
uint32_t ota_taskClearQueue();
bool ota_taskTakeCancelReason(char *reasonBuf, size_t reasonBufLen);
bool ota_taskHasPendingWork();
