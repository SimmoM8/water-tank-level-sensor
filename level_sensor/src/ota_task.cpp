#include "ota_task.h"

#include <Arduino.h>
#include <string.h>
#include "logger.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Implemented in ota_service.cpp. Runs the pull OTA state machine to completion.
extern void ota_processPullJobInTask(DeviceState *state, const OtaTaskJob &job);

enum class OtaTaskMsgType : uint8_t
{
    START_JOB = 0
};

struct OtaTaskMsg
{
    OtaTaskMsgType type = OtaTaskMsgType::START_JOB;
    OtaTaskJob job{};
};

static QueueHandle_t s_otaQueue = nullptr;
static TaskHandle_t s_otaTaskHandle = nullptr;
static DeviceState *s_state = nullptr;
static volatile bool s_jobRunning = false;
static volatile bool s_cancelRequested = false;
static char s_cancelReason[OTA_ERROR_MAX] = {0};
static portMUX_TYPE s_cancelMux = portMUX_INITIALIZER_UNLOCKED;

#if (CFG_OTA_TASK_CORE < 0)
static constexpr const char *kOtaTaskPinMode = "unpinned";
#else
static constexpr const char *kOtaTaskPinMode = "pinned";
#endif

static bool ota_taskIsCancelRequested()
{
    bool cancelRequested = false;
    portENTER_CRITICAL(&s_cancelMux);
    cancelRequested = s_cancelRequested;
    portEXIT_CRITICAL(&s_cancelMux);
    return cancelRequested;
}

// Dedicated OTA worker task:
// - Owns the firmware download/flash lifecycle.
// - Uses a 16KB stack because TLS + HTTP + Update paths are stack-heavy on ESP32.
// - Waits on queue so idle CPU usage remains near-zero when no OTA job is pending.
static void otaTask(void * /*arg*/)
{
    LOG_INFO(LogDomain::OTA,
             "otaTask started mode=%s core=%d configured_core=%d stack_bytes=%u prio=%u queue_depth=%u",
             kOtaTaskPinMode,
             xPortGetCoreID(),
             (int)CFG_OTA_TASK_CORE,
             (unsigned)CFG_OTA_TASK_STACK_BYTES,
             (unsigned)CFG_OTA_TASK_PRIORITY,
             (unsigned)CFG_OTA_TASK_QUEUE_DEPTH);

    for (;;)
    {
        OtaTaskMsg msg{};
        if (xQueueReceive(s_otaQueue, &msg, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }
        if (msg.type != OtaTaskMsgType::START_JOB)
        {
            continue;
        }

        s_jobRunning = true;

        ota_processPullJobInTask(s_state, msg.job);
        s_jobRunning = false;
    }
}

bool ota_taskBegin(DeviceState *state)
{
    if (s_otaTaskHandle != nullptr)
    {
        return true;
    }
    if (state == nullptr)
    {
        return false;
    }

    s_state = state;
    s_otaQueue = xQueueCreate((UBaseType_t)CFG_OTA_TASK_QUEUE_DEPTH, sizeof(OtaTaskMsg));
    if (s_otaQueue == nullptr)
    {
        LOG_ERROR(LogDomain::OTA, "otaTask queue create failed depth=%u", (unsigned)CFG_OTA_TASK_QUEUE_DEPTH);
        return false;
    }

    BaseType_t created = pdFAIL;
#if (CFG_OTA_TASK_CORE < 0)
    LOG_INFO(LogDomain::OTA,
             "Creating otaTask mode=unpinned stack_bytes=%u prio=%u",
             (unsigned)CFG_OTA_TASK_STACK_BYTES,
             (unsigned)CFG_OTA_TASK_PRIORITY);
    created = xTaskCreate(
        otaTask,
        "otaTask",
        (uint32_t)CFG_OTA_TASK_STACK_BYTES,
        nullptr,
        (UBaseType_t)CFG_OTA_TASK_PRIORITY,
        &s_otaTaskHandle);
#else
    LOG_INFO(LogDomain::OTA,
             "Creating otaTask mode=pinned core=%d stack_bytes=%u prio=%u",
             (int)CFG_OTA_TASK_CORE,
             (unsigned)CFG_OTA_TASK_STACK_BYTES,
             (unsigned)CFG_OTA_TASK_PRIORITY);
    created = xTaskCreatePinnedToCore(
        otaTask,
        "otaTask",
        (uint32_t)CFG_OTA_TASK_STACK_BYTES,
        nullptr,
        (UBaseType_t)CFG_OTA_TASK_PRIORITY,
        &s_otaTaskHandle,
        (BaseType_t)CFG_OTA_TASK_CORE);
#endif

    if (created != pdPASS)
    {
        LOG_ERROR(LogDomain::OTA, "otaTask create failed stack_bytes=%u", (unsigned)CFG_OTA_TASK_STACK_BYTES);
        vQueueDelete(s_otaQueue);
        s_otaQueue = nullptr;
        return false;
    }

    return true;
}

bool ota_taskEnqueue(const OtaTaskJob &job)
{
    if (s_otaQueue == nullptr)
    {
        return false;
    }
    if (ota_taskIsCancelRequested())
    {
        LOG_WARN(LogDomain::OTA, "reject enqueue: cancel pending");
        return false;
    }

    OtaTaskMsg msg{};
    msg.type = OtaTaskMsgType::START_JOB;
    msg.job = job;
    return xQueueSend(s_otaQueue, &msg, 0) == pdTRUE;
}

bool ota_taskRequestCancel(const char *reason)
{
    portENTER_CRITICAL(&s_cancelMux);
    s_cancelRequested = true;
    strncpy(s_cancelReason, (reason && reason[0] != '\0') ? reason : "cancelled", sizeof(s_cancelReason));
    s_cancelReason[sizeof(s_cancelReason) - 1] = '\0';
    portEXIT_CRITICAL(&s_cancelMux);
    return true;
}

uint32_t ota_taskClearQueue()
{
    if (s_otaQueue == nullptr)
    {
        return 0u;
    }

    uint32_t drainedCount = 0u;
    OtaTaskMsg dropped{};
    while (xQueueReceive(s_otaQueue, &dropped, 0) == pdTRUE)
    {
        ++drainedCount;
    }
    return drainedCount;
}

bool ota_taskCancelAll(const char *reason)
{
    bool wasCancelRequested = false;
    portENTER_CRITICAL(&s_cancelMux);
    wasCancelRequested = s_cancelRequested;
    s_cancelRequested = true;
    strncpy(s_cancelReason, (reason && reason[0] != '\0') ? reason : "cancelled", sizeof(s_cancelReason));
    s_cancelReason[sizeof(s_cancelReason) - 1] = '\0';
    portEXIT_CRITICAL(&s_cancelMux);

    const uint32_t drainedCount = ota_taskClearQueue();
    const bool hadRunning = s_jobRunning;
    return hadRunning || (drainedCount > 0u) || wasCancelRequested;
}

bool ota_taskTakeCancelReason(char *reasonBuf, size_t reasonBufLen)
{
    bool wasRequested = false;
    char localReason[OTA_ERROR_MAX] = {0};

    portENTER_CRITICAL(&s_cancelMux);
    if (s_cancelRequested)
    {
        wasRequested = true;
        s_cancelRequested = false;
        strncpy(localReason, s_cancelReason, sizeof(localReason));
        localReason[sizeof(localReason) - 1] = '\0';
        s_cancelReason[0] = '\0';
    }
    portEXIT_CRITICAL(&s_cancelMux);

    if (!wasRequested)
    {
        return false;
    }
    if (reasonBuf && reasonBufLen > 0)
    {
        strncpy(reasonBuf, localReason[0] ? localReason : "cancelled", reasonBufLen);
        reasonBuf[reasonBufLen - 1] = '\0';
    }
    return true;
}

bool ota_taskHasPendingWork()
{
    if (s_otaQueue == nullptr)
    {
        return s_jobRunning;
    }
    return s_jobRunning || (uxQueueMessagesWaiting(s_otaQueue) > 0);
}
