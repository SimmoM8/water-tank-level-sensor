#include "ota_events.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "mqtt_transport.h"

enum class OtaEventType : uint8_t
{
    STATUS = 0,
    PROGRESS,
    ERROR_TEXT,
    FLAT_STATE,
    RESULT,
    CLEAR_ACTIVE,
    SET_UPDATE_AVAILABLE,
    SET_LAST_SUCCESS_TS,
    REQUEST_PUBLISH
};

struct OtaEventFlatPayload
{
    bool hasState;
    bool hasError;
    bool hasTargetVersion;
    bool stamp;
    uint8_t progress;
    char state[OTA_STATE_MAX];
    char error[OTA_ERROR_MAX];
    char targetVersion[OTA_TARGET_VERSION_MAX];
};

struct OtaEventResultPayload
{
    char status[OTA_STATUS_MAX];
    char message[OTA_MESSAGE_MAX];
    uint32_t completedTs;
};

struct OtaEvent
{
    OtaEventType type = OtaEventType::REQUEST_PUBLISH;
    union
    {
        OtaStatus status;
        uint8_t progress;
        struct
        {
            char error[OTA_ERROR_MAX];
        } error;
        OtaEventFlatPayload flat;
        OtaEventResultPayload result;
        struct
        {
            bool value;
        } update;
        struct
        {
            uint32_t ts;
        } lastSuccess;
    } data;
};

static QueueHandle_t s_queue = nullptr;
static portMUX_TYPE s_eventsMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_progressCoalescedPending = false;
static uint8_t s_progressCoalescedValue = 0;

static bool pushEventDropOldest(const OtaEvent &ev)
{
    if (s_queue == nullptr)
    {
        return false;
    }
    if (xQueueSend(s_queue, &ev, 0) == pdTRUE)
    {
        return true;
    }

    // Drop oldest event to preserve forward progress under bursty OTA updates.
    OtaEvent dropped{};
    (void)xQueueReceive(s_queue, &dropped, 0);
    return xQueueSend(s_queue, &ev, 0) == pdTRUE;
}

bool ota_events_begin()
{
    if (s_queue != nullptr)
    {
        return true;
    }

    QueueHandle_t created = xQueueCreate((UBaseType_t)CFG_OTA_EVENTS_QUEUE_DEPTH, sizeof(OtaEvent));
    if (created == nullptr)
    {
        return false;
    }

    portENTER_CRITICAL(&s_eventsMux);
    if (s_queue == nullptr)
    {
        s_queue = created;
        created = nullptr;
    }
    portEXIT_CRITICAL(&s_eventsMux);

    if (created != nullptr)
    {
        vQueueDelete(created);
    }
    return s_queue != nullptr;
}

bool ota_events_pushStatus(OtaStatus status)
{
    OtaEvent ev{};
    ev.type = OtaEventType::STATUS;
    ev.data.status = status;
    return pushEventDropOldest(ev);
}

bool ota_events_pushProgress(uint8_t progress)
{
    OtaEvent ev{};
    ev.type = OtaEventType::PROGRESS;
    ev.data.progress = progress;
    if (s_queue != nullptr && xQueueSend(s_queue, &ev, 0) == pdTRUE)
    {
        // Fresh queued progress supersedes stale coalesced fallback.
        portENTER_CRITICAL(&s_eventsMux);
        s_progressCoalescedPending = false;
        s_progressCoalescedValue = 0;
        portEXIT_CRITICAL(&s_eventsMux);
        return true;
    }

    // Progress is high-frequency; coalesce to latest value if queue is full.
    portENTER_CRITICAL(&s_eventsMux);
    s_progressCoalescedValue = progress;
    s_progressCoalescedPending = true;
    portEXIT_CRITICAL(&s_eventsMux);
    return true;
}

bool ota_events_pushError(const char *errorText)
{
    OtaEvent ev{};
    ev.type = OtaEventType::ERROR_TEXT;
    strncpy(ev.data.error.error, errorText ? errorText : "", sizeof(ev.data.error.error));
    ev.data.error.error[sizeof(ev.data.error.error) - 1] = '\0';
    return pushEventDropOldest(ev);
}

bool ota_events_pushFlatState(const char *stateStr,
                              uint8_t progress,
                              const char *errorText,
                              const char *targetVersion,
                              bool stamp)
{
    OtaEvent ev{};
    ev.type = OtaEventType::FLAT_STATE;
    ev.data.flat.progress = progress;
    ev.data.flat.stamp = stamp;
    ev.data.flat.hasState = (stateStr != nullptr);
    ev.data.flat.hasError = (errorText != nullptr);
    ev.data.flat.hasTargetVersion = (targetVersion != nullptr);
    if (stateStr)
    {
        strncpy(ev.data.flat.state, stateStr, sizeof(ev.data.flat.state));
        ev.data.flat.state[sizeof(ev.data.flat.state) - 1] = '\0';
    }
    if (errorText)
    {
        strncpy(ev.data.flat.error, errorText, sizeof(ev.data.flat.error));
        ev.data.flat.error[sizeof(ev.data.flat.error) - 1] = '\0';
    }
    if (targetVersion)
    {
        strncpy(ev.data.flat.targetVersion, targetVersion, sizeof(ev.data.flat.targetVersion));
        ev.data.flat.targetVersion[sizeof(ev.data.flat.targetVersion) - 1] = '\0';
    }
    return pushEventDropOldest(ev);
}

bool ota_events_pushResult(const char *status, const char *message, uint32_t completedTs)
{
    OtaEvent ev{};
    ev.type = OtaEventType::RESULT;
    strncpy(ev.data.result.status, status ? status : "", sizeof(ev.data.result.status));
    ev.data.result.status[sizeof(ev.data.result.status) - 1] = '\0';
    strncpy(ev.data.result.message, message ? message : "", sizeof(ev.data.result.message));
    ev.data.result.message[sizeof(ev.data.result.message) - 1] = '\0';
    ev.data.result.completedTs = completedTs;
    return pushEventDropOldest(ev);
}

bool ota_events_pushClearActive()
{
    OtaEvent ev{};
    ev.type = OtaEventType::CLEAR_ACTIVE;
    return pushEventDropOldest(ev);
}

bool ota_events_pushUpdateAvailable(bool value)
{
    OtaEvent ev{};
    ev.type = OtaEventType::SET_UPDATE_AVAILABLE;
    ev.data.update.value = value;
    return pushEventDropOldest(ev);
}

bool ota_events_pushLastSuccessTs(uint32_t ts)
{
    OtaEvent ev{};
    ev.type = OtaEventType::SET_LAST_SUCCESS_TS;
    ev.data.lastSuccess.ts = ts;
    return pushEventDropOldest(ev);
}

bool ota_events_requestPublish()
{
    OtaEvent ev{};
    ev.type = OtaEventType::REQUEST_PUBLISH;
    return pushEventDropOldest(ev);
}

struct PendingApply
{
    bool hasStatus = false;
    OtaStatus status = OtaStatus::IDLE;
    bool hasProgress = false;
    bool hasProgressFromQueue = false;
    uint8_t progress = 0;
    bool hasFlat = false;
    OtaEventFlatPayload flat{};
    bool hasError = false;
    char error[OTA_ERROR_MAX] = {0};
    bool hasResult = false;
    OtaEventResultPayload result{};
    bool clearActive = false;
    bool hasUpdateAvailable = false;
    bool updateAvailable = false;
    bool hasLastSuccessTs = false;
    uint32_t lastSuccessTs = 0;
    bool requestPublish = false;
};

static void collectPending(PendingApply &pending, const OtaEvent &ev)
{
    switch (ev.type)
    {
    case OtaEventType::STATUS:
        pending.hasStatus = true;
        pending.status = ev.data.status;
        break;
    case OtaEventType::PROGRESS:
        pending.hasProgress = true;
        pending.hasProgressFromQueue = true;
        pending.progress = ev.data.progress;
        break;
    case OtaEventType::ERROR_TEXT:
        pending.hasError = true;
        strncpy(pending.error, ev.data.error.error, sizeof(pending.error));
        pending.error[sizeof(pending.error) - 1] = '\0';
        break;
    case OtaEventType::FLAT_STATE:
        pending.hasFlat = true;
        pending.flat = ev.data.flat;
        break;
    case OtaEventType::RESULT:
        pending.hasResult = true;
        pending.result = ev.data.result;
        break;
    case OtaEventType::CLEAR_ACTIVE:
        pending.clearActive = true;
        break;
    case OtaEventType::SET_UPDATE_AVAILABLE:
        pending.hasUpdateAvailable = true;
        pending.updateAvailable = ev.data.update.value;
        break;
    case OtaEventType::SET_LAST_SUCCESS_TS:
        pending.hasLastSuccessTs = true;
        pending.lastSuccessTs = ev.data.lastSuccess.ts;
        break;
    case OtaEventType::REQUEST_PUBLISH:
        pending.requestPublish = true;
        break;
    }
}

bool ota_events_drainAndApply(DeviceState *state)
{
    if (!state || s_queue == nullptr)
    {
        return false;
    }

    bool anyApplied = false;
    PendingApply pending{};
    OtaEvent ev{};

    while (xQueueReceive(s_queue, &ev, 0) == pdTRUE)
    {
        collectPending(pending, ev);
        anyApplied = true;
    }

    bool coalescedProgressPending = false;
    uint8_t coalescedProgress = 0;
    if (!pending.hasProgressFromQueue)
    {
        portENTER_CRITICAL(&s_eventsMux);
        if (s_progressCoalescedPending)
        {
            coalescedProgressPending = true;
            coalescedProgress = s_progressCoalescedValue;
            s_progressCoalescedPending = false;
        }
        portEXIT_CRITICAL(&s_eventsMux);
    }
    else
    {
        // Ignore stale coalesced value if this drain already got queued progress.
        portENTER_CRITICAL(&s_eventsMux);
        s_progressCoalescedPending = false;
        s_progressCoalescedValue = 0;
        portEXIT_CRITICAL(&s_eventsMux);
    }

    if (coalescedProgressPending)
    {
        pending.hasProgress = true;
        pending.progress = coalescedProgress;
        anyApplied = true;
    }

    // Deterministic apply order:
    // status -> progress -> flat -> error/result -> timestamps/flags.
    if (pending.hasStatus)
    {
        state->ota.status = pending.status;
    }
    if (pending.hasProgress)
    {
        state->ota.progress = pending.progress;
        state->ota_progress = pending.progress;
    }
    if (pending.hasFlat)
    {
        if (pending.flat.hasState)
        {
            strncpy(state->ota_state, pending.flat.state, sizeof(state->ota_state));
            state->ota_state[sizeof(state->ota_state) - 1] = '\0';
        }
        state->ota.progress = pending.flat.progress;
        state->ota_progress = pending.flat.progress;
        if (pending.flat.hasError)
        {
            strncpy(state->ota_error, pending.flat.error, sizeof(state->ota_error));
            state->ota_error[sizeof(state->ota_error) - 1] = '\0';
        }
        else
        {
            state->ota_error[0] = '\0';
        }
        if (pending.flat.hasTargetVersion)
        {
            strncpy(state->ota_target_version, pending.flat.targetVersion, sizeof(state->ota_target_version));
            state->ota_target_version[sizeof(state->ota_target_version) - 1] = '\0';
        }
        else
        {
            state->ota_target_version[0] = '\0';
        }
    }
    if (pending.hasError)
    {
        strncpy(state->ota_error, pending.error, sizeof(state->ota_error));
        state->ota_error[sizeof(state->ota_error) - 1] = '\0';
        strncpy(state->ota.last_status, "error", sizeof(state->ota.last_status));
        state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';
        strncpy(state->ota.last_message, pending.error, sizeof(state->ota.last_message));
        state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';
    }
    if (pending.hasResult)
    {
        strncpy(state->ota.last_status, pending.result.status, sizeof(state->ota.last_status));
        state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';
        strncpy(state->ota.last_message, pending.result.message, sizeof(state->ota.last_message));
        state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';
        state->ota.completed_ts = pending.result.completedTs;
    }
    if (pending.hasFlat && pending.flat.stamp)
    {
        const time_t now = time(nullptr);
        if (now >= 1600000000)
        {
            state->ota_last_ts = (uint32_t)now;
        }
    }
    if (pending.clearActive)
    {
        state->ota.request_id[0] = '\0';
        state->ota.version[0] = '\0';
        state->ota.url[0] = '\0';
        state->ota.sha256[0] = '\0';
        state->ota.started_ts = 0;
    }
    if (pending.hasUpdateAvailable)
    {
        state->update_available = pending.updateAvailable;
    }
    if (pending.hasLastSuccessTs)
    {
        state->ota_last_success_ts = pending.lastSuccessTs;
    }
    if (pending.requestPublish)
    {
        mqtt_requestStatePublish();
    }
    return anyApplied;
}
