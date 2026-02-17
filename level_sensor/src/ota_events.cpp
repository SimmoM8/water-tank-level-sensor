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
        struct
        {
            bool hasState;
            bool hasError;
            bool hasTargetVersion;
            bool stamp;
            uint8_t progress;
            char state[OTA_STATE_MAX];
            char error[OTA_ERROR_MAX];
            char targetVersion[OTA_TARGET_VERSION_MAX];
        } flat;
        struct
        {
            char status[OTA_STATUS_MAX];
            char message[OTA_MESSAGE_MAX];
            uint32_t completedTs;
        } result;
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

static bool pushEvent(const OtaEvent &ev)
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
    s_queue = xQueueCreate((UBaseType_t)CFG_OTA_EVENTS_QUEUE_DEPTH, sizeof(OtaEvent));
    return s_queue != nullptr;
}

bool ota_events_pushStatus(OtaStatus status)
{
    OtaEvent ev{};
    ev.type = OtaEventType::STATUS;
    ev.data.status = status;
    return pushEvent(ev);
}

bool ota_events_pushProgress(uint8_t progress)
{
    OtaEvent ev{};
    ev.type = OtaEventType::PROGRESS;
    ev.data.progress = progress;
    return pushEvent(ev);
}

bool ota_events_pushError(const char *errorText)
{
    OtaEvent ev{};
    ev.type = OtaEventType::ERROR_TEXT;
    strncpy(ev.data.error.error, errorText ? errorText : "", sizeof(ev.data.error.error));
    ev.data.error.error[sizeof(ev.data.error.error) - 1] = '\0';
    return pushEvent(ev);
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
    return pushEvent(ev);
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
    return pushEvent(ev);
}

bool ota_events_pushClearActive()
{
    OtaEvent ev{};
    ev.type = OtaEventType::CLEAR_ACTIVE;
    return pushEvent(ev);
}

bool ota_events_pushUpdateAvailable(bool value)
{
    OtaEvent ev{};
    ev.type = OtaEventType::SET_UPDATE_AVAILABLE;
    ev.data.update.value = value;
    return pushEvent(ev);
}

bool ota_events_pushLastSuccessTs(uint32_t ts)
{
    OtaEvent ev{};
    ev.type = OtaEventType::SET_LAST_SUCCESS_TS;
    ev.data.lastSuccess.ts = ts;
    return pushEvent(ev);
}

bool ota_events_requestPublish()
{
    OtaEvent ev{};
    ev.type = OtaEventType::REQUEST_PUBLISH;
    return pushEvent(ev);
}

static void applyEvent(DeviceState *state, const OtaEvent &ev, bool &requestPublish)
{
    if (!state)
    {
        return;
    }

    switch (ev.type)
    {
    case OtaEventType::STATUS:
        state->ota.status = ev.data.status;
        break;
    case OtaEventType::PROGRESS:
        state->ota.progress = ev.data.progress;
        state->ota_progress = ev.data.progress;
        break;
    case OtaEventType::ERROR_TEXT:
        strncpy(state->ota_error, ev.data.error.error, sizeof(state->ota_error));
        state->ota_error[sizeof(state->ota_error) - 1] = '\0';
        strncpy(state->ota.last_status, "error", sizeof(state->ota.last_status));
        state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';
        strncpy(state->ota.last_message, ev.data.error.error, sizeof(state->ota.last_message));
        state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';
        break;
    case OtaEventType::FLAT_STATE:
        if (ev.data.flat.hasState)
        {
            strncpy(state->ota_state, ev.data.flat.state, sizeof(state->ota_state));
            state->ota_state[sizeof(state->ota_state) - 1] = '\0';
        }
        state->ota.progress = ev.data.flat.progress;
        state->ota_progress = ev.data.flat.progress;
        if (ev.data.flat.hasError)
        {
            strncpy(state->ota_error, ev.data.flat.error, sizeof(state->ota_error));
            state->ota_error[sizeof(state->ota_error) - 1] = '\0';
        }
        if (ev.data.flat.hasTargetVersion)
        {
            strncpy(state->ota_target_version, ev.data.flat.targetVersion, sizeof(state->ota_target_version));
            state->ota_target_version[sizeof(state->ota_target_version) - 1] = '\0';
        }
        if (ev.data.flat.stamp)
        {
            const time_t now = time(nullptr);
            if (now >= 1600000000)
            {
                state->ota_last_ts = (uint32_t)now;
            }
        }
        break;
    case OtaEventType::RESULT:
        strncpy(state->ota.last_status, ev.data.result.status, sizeof(state->ota.last_status));
        state->ota.last_status[sizeof(state->ota.last_status) - 1] = '\0';
        strncpy(state->ota.last_message, ev.data.result.message, sizeof(state->ota.last_message));
        state->ota.last_message[sizeof(state->ota.last_message) - 1] = '\0';
        state->ota.completed_ts = ev.data.result.completedTs;
        break;
    case OtaEventType::CLEAR_ACTIVE:
        state->ota.request_id[0] = '\0';
        state->ota.version[0] = '\0';
        state->ota.url[0] = '\0';
        state->ota.sha256[0] = '\0';
        state->ota.started_ts = 0;
        break;
    case OtaEventType::SET_UPDATE_AVAILABLE:
        state->update_available = ev.data.update.value;
        break;
    case OtaEventType::SET_LAST_SUCCESS_TS:
        state->ota_last_success_ts = ev.data.lastSuccess.ts;
        break;
    case OtaEventType::REQUEST_PUBLISH:
        requestPublish = true;
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
    bool requestPublish = false;
    OtaEvent ev{};

    while (xQueueReceive(s_queue, &ev, 0) == pdTRUE)
    {
        applyEvent(state, ev, requestPublish);
        anyApplied = true;
    }

    if (requestPublish)
    {
        mqtt_requestStatePublish();
    }
    return anyApplied;
}
