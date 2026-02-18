#include <WiFi.h>
#include <WiFiManager.h>
#include <Arduino.h>
#include <esp_wifi.h>

#include "wifi_provisioning.h"
#include <Preferences.h>
#include <time.h>
#include "logger.h"

static const char *PREF_KEY_FORCE_PORTAL = "force_portal";
static const time_t VALID_TIME_EPOCH = 1600000000;

#ifdef __has_include
#if __has_include("config.h")
#include "config.h"
#endif
#endif

#ifndef CFG_TIME_SYNC_TIMEOUT_MS
#define CFG_TIME_SYNC_TIMEOUT_MS 20000u
#endif
#ifndef CFG_TIME_SYNC_RETRY_MIN_MS
#define CFG_TIME_SYNC_RETRY_MIN_MS 5000u
#endif
#ifndef CFG_TIME_SYNC_RETRY_MAX_MS
#define CFG_TIME_SYNC_RETRY_MAX_MS 300000u
#endif
#ifndef CFG_WIFI_CONNECT_RETRY_MIN_MS
#define CFG_WIFI_CONNECT_RETRY_MIN_MS 5000u
#endif
#ifndef CFG_WIFI_CONNECT_RETRY_MAX_MS
#define CFG_WIFI_CONNECT_RETRY_MAX_MS 300000u
#endif
#ifndef CFG_WIFI_AUTO_PORTAL_ON_MISSING_CREDS
#define CFG_WIFI_AUTO_PORTAL_ON_MISSING_CREDS 1
#endif
#ifndef CFG_WIFI_PORTAL_DEBUG
#define CFG_WIFI_PORTAL_DEBUG 0
#endif

static Preferences wifiPrefs; // WiFi-related preferences

static const char *TIME_STATUS_VALID = "valid";
static const char *TIME_STATUS_SYNCING = "syncing";
static const char *TIME_STATUS_NOT_SET = "time_not_set";

static bool s_wifiConnectInFlight = false;
static uint32_t s_wifiConnectStartMs = 0;
static uint32_t s_wifiConnectRetryAtMs = 0;
static uint32_t s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MIN_MS;

static bool s_timeSyncInFlight = false;
static uint32_t s_timeSyncStartMs = 0;
static uint32_t s_lastTimeSyncAttemptMs = 0;
static uint32_t s_lastTimeSyncSuccessMs = 0;
static uint32_t s_nextTimeSyncRetryMs = 0;
static uint32_t s_timeSyncBackoffMs = CFG_TIME_SYNC_RETRY_MIN_MS;
static bool s_timeWasValid = false;
static bool s_loggedMissingCredentials = false;
static bool s_runtimePortalRequested = false;

static bool wifi_hasSavedCredentials()
{
    // Use esp_wifi_get_config() so we don't rely on WiFi.SSID(), which can be empty
    // before WiFi is initialized/put into STA mode.
    wifi_config_t conf{};
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK)
    {
        return false;
    }
    return conf.sta.ssid[0] != 0;
}

bool wifi_timeIsValid()
{
    return time(nullptr) > VALID_TIME_EPOCH;
}

static uint32_t wifi_clampBackoff(uint32_t valueMs)
{
    if (valueMs < CFG_TIME_SYNC_RETRY_MIN_MS)
    {
        return CFG_TIME_SYNC_RETRY_MIN_MS;
    }
    if (valueMs > CFG_TIME_SYNC_RETRY_MAX_MS)
    {
        return CFG_TIME_SYNC_RETRY_MAX_MS;
    }
    return valueMs;
}

static uint32_t wifi_clampConnectBackoff(uint32_t valueMs)
{
    if (valueMs < CFG_WIFI_CONNECT_RETRY_MIN_MS)
    {
        return CFG_WIFI_CONNECT_RETRY_MIN_MS;
    }
    if (valueMs > CFG_WIFI_CONNECT_RETRY_MAX_MS)
    {
        return CFG_WIFI_CONNECT_RETRY_MAX_MS;
    }
    return valueMs;
}

static void wifi_startTimeSyncAttempt()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    const uint32_t now = millis();
    s_timeSyncInFlight = true;
    s_timeSyncStartMs = now;
    s_lastTimeSyncAttemptMs = now;

    LOG_INFO(LogDomain::WIFI,
             "Starting NTP sync (pool.ntp.org, time.google.com) timeout_ms=%lu backoff_ms=%lu",
             (unsigned long)CFG_TIME_SYNC_TIMEOUT_MS,
             (unsigned long)s_timeSyncBackoffMs);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
}

void wifi_timeSyncTick()
{
    const uint32_t now = millis();
    const bool timeValid = wifi_timeIsValid();

    if (timeValid)
    {
        const bool gainedValidity = !s_timeWasValid || s_timeSyncInFlight;
        if (!s_timeWasValid || s_timeSyncInFlight)
        {
            LOG_INFO(LogDomain::WIFI, "System time valid epoch=%lu", (unsigned long)time(nullptr));
        }

        s_timeWasValid = true;
        s_timeSyncInFlight = false;
        s_timeSyncBackoffMs = CFG_TIME_SYNC_RETRY_MIN_MS;
        s_nextTimeSyncRetryMs = 0;
        if (gainedValidity || s_lastTimeSyncSuccessMs == 0)
        {
            s_lastTimeSyncSuccessMs = now;
        }
        return;
    }

    s_timeWasValid = false;

    if (WiFi.status() != WL_CONNECTED)
    {
        if (s_timeSyncInFlight)
        {
            LOG_WARN(LogDomain::WIFI, "NTP sync interrupted: wifi_disconnected");
            s_timeSyncInFlight = false;
        }
        return;
    }

    if (s_timeSyncInFlight)
    {
        const uint32_t elapsed = now - s_timeSyncStartMs;
        if (elapsed < CFG_TIME_SYNC_TIMEOUT_MS)
        {
            return;
        }

        s_timeSyncInFlight = false;
        s_nextTimeSyncRetryMs = now + s_timeSyncBackoffMs;
        LOG_WARN(LogDomain::WIFI, "NTP sync timeout after %lums, retry_in_ms=%lu",
                 (unsigned long)elapsed,
                 (unsigned long)s_timeSyncBackoffMs);
        if (s_timeSyncBackoffMs >= (CFG_TIME_SYNC_RETRY_MAX_MS / 2u))
        {
            s_timeSyncBackoffMs = CFG_TIME_SYNC_RETRY_MAX_MS;
        }
        else
        {
            s_timeSyncBackoffMs = wifi_clampBackoff(s_timeSyncBackoffMs * 2u);
        }
        return;
    }

    if (s_nextTimeSyncRetryMs != 0 && (int32_t)(now - s_nextTimeSyncRetryMs) < 0)
    {
        logger_logEvery("ntp_wait_retry", 15000, LogLevel::DEBUG, LogDomain::WIFI,
                        "Waiting for next NTP retry now_ms=%lu next_retry_ms=%lu",
                        (unsigned long)now,
                        (unsigned long)s_nextTimeSyncRetryMs);
        return;
    }

    wifi_startTimeSyncAttempt();
}

// Initialize WiFi provisioning module
void wifi_begin()
{
    wifiPrefs.begin("wifi", false); // namespace "wifi", read-write
}

static void startPortal()
{
    LOG_INFO(LogDomain::WIFI, "Starting captive portal (setup mode)...");
    // Clear one-shot portal latches before entering portal so failure cannot
    // trap the device in reboot/setup loops.
    s_runtimePortalRequested = false;
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, false);

    LOG_DEBUG(LogDomain::WIFI, "Heap before portal free=%lu min=%lu", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());

    // ESP32-S3 portal bring-up is most reliable with AP+STA, modem sleep disabled,
    // and a short disconnect settle delay before launching WiFiManager.
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, false);
    delay(200);

    WiFiManager wm;
    wm.setDebugOutput(CFG_WIFI_PORTAL_DEBUG ? true : false);
    wm.setConfigPortalTimeout(180); // seconds
    wm.setConnectTimeout(20);
    wm.setConnectRetries(2);

    wm.setHostname("water-tank-esp32");

    LOG_INFO(LogDomain::WIFI, "Entering WiFiManager startConfigPortal(ssid=WaterTank-Setup) ...");
    bool ok = wm.startConfigPortal("WaterTank-Setup");
    LOG_INFO(LogDomain::WIFI, "WiFiManager startConfigPortal returned ok=%s", ok ? "true" : "false");

    if (!ok)
    {
        LOG_WARN(LogDomain::WIFI, "Portal timed out or failed; continuing without reboot");
        s_wifiConnectInFlight = false;
        s_wifiConnectStartMs = 0;
        s_wifiConnectRetryAtMs = 0;
        s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MIN_MS;
        LOG_DEBUG(LogDomain::WIFI, "Heap after portal fail free=%lu min=%lu", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
        return;
    }

    LOG_INFO(LogDomain::WIFI, "WiFi configured and connected ip=%s", WiFi.localIP().toString().c_str());
    LOG_DEBUG(LogDomain::WIFI, "Heap after portal success free=%lu min=%lu", (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getMinFreeHeap());
    s_wifiConnectInFlight = false;
    s_wifiConnectStartMs = 0;
    s_wifiConnectRetryAtMs = 0;
    s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MIN_MS;
    s_nextTimeSyncRetryMs = 0;
    wifi_timeSyncTick();

    s_loggedMissingCredentials = false;
}

// Ensure WiFi is connected, otherwise start captive portal
void wifi_ensureConnected(uint32_t wifiTimeoutMs)
{
    const uint32_t now = millis();

    // Keep time sync state fresh even while disconnected.
    wifi_timeSyncTick();

    // Ensure WiFi stack is initialized consistently before checking stored creds.
    // This prevents false "no saved credentials" on fresh boot.
    WiFi.persistent(true);
    if (WiFi.getMode() == WIFI_MODE_NULL)
    {
        WiFi.mode(WIFI_STA);
        delay(10);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        if (s_wifiConnectInFlight)
        {
            const uint32_t elapsed = now - s_wifiConnectStartMs;
            LOG_INFO(LogDomain::WIFI, "Connected ip=%s connect_ms=%lu",
                     WiFi.localIP().toString().c_str(),
                     (unsigned long)elapsed);
        }

        s_wifiConnectInFlight = false;
        s_wifiConnectStartMs = 0;
        s_wifiConnectRetryAtMs = 0;
        s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MIN_MS;
        s_loggedMissingCredentials = false;
        return;
    }

    if (s_wifiConnectInFlight)
    {
        const uint32_t timeoutMs = (wifiTimeoutMs == 0u) ? 1u : wifiTimeoutMs;
        if ((uint32_t)(now - s_wifiConnectStartMs) < timeoutMs)
        {
            return;
        }

        LOG_WARN(LogDomain::WIFI, "WiFi connect timed out after %lums; retry_in_ms=%lu",
                 (unsigned long)(now - s_wifiConnectStartMs),
                 (unsigned long)s_wifiConnectBackoffMs);
        WiFi.disconnect(false, false);
        s_wifiConnectInFlight = false;
        s_wifiConnectStartMs = 0;
        s_wifiConnectRetryAtMs = now + s_wifiConnectBackoffMs;
        if (s_wifiConnectBackoffMs >= (CFG_WIFI_CONNECT_RETRY_MAX_MS / 2u))
        {
            s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MAX_MS;
        }
        else
        {
            s_wifiConnectBackoffMs = wifi_clampConnectBackoff(s_wifiConnectBackoffMs * 2u);
        }
        return;
    }

    // Check if we need to force the portal
    if (s_runtimePortalRequested || wifiPrefs.getBool(PREF_KEY_FORCE_PORTAL, false))
    {
        startPortal(); // explicit user-driven setup mode
        return;
    }

    if (!wifi_hasSavedCredentials())
    {
        if (!s_loggedMissingCredentials)
        {
            LOG_WARN(LogDomain::WIFI, "No saved WiFi credentials");
            s_loggedMissingCredentials = true;
        }
#if CFG_WIFI_AUTO_PORTAL_ON_MISSING_CREDS
        LOG_WARN(LogDomain::WIFI, "Auto portal enabled; entering captive portal");
        startPortal();
#else
        logger_logEvery("wifi_no_creds", 15000, LogLevel::INFO, LogDomain::WIFI,
                        "Setup portal not auto-started; run serial command 'wifi' to provision");
#endif
        return;
    }

    if (s_wifiConnectRetryAtMs != 0 && (int32_t)(now - s_wifiConnectRetryAtMs) < 0)
    {
        logger_logEvery("wifi_wait_retry", 15000, LogLevel::DEBUG, LogDomain::WIFI,
                        "Waiting for WiFi retry now_ms=%lu retry_at_ms=%lu",
                        (unsigned long)now,
                        (unsigned long)s_wifiConnectRetryAtMs);
        return;
    }

    WiFi.mode(WIFI_STA); // station mode
    LOG_INFO(LogDomain::WIFI, "Connecting to saved WiFi");

    WiFi.begin(); // use stored credentials
    s_wifiConnectInFlight = true;
    s_wifiConnectStartMs = now;
    s_wifiConnectRetryAtMs = 0;
}

void wifi_getTimeSyncStatus(WifiTimeSyncStatus &out)
{
    out.valid = wifi_timeIsValid();
    out.syncing = s_timeSyncInFlight;
    out.lastAttemptMs = s_lastTimeSyncAttemptMs;
    out.lastSuccessMs = s_lastTimeSyncSuccessMs;
    out.nextRetryMs = s_nextTimeSyncRetryMs;
    out.status = out.valid ? TIME_STATUS_VALID : (out.syncing ? TIME_STATUS_SYNCING : TIME_STATUS_NOT_SET);
}

void wifi_requestPortal()
{
    LOG_INFO(LogDomain::WIFI, "Forcing captive portal");
    // Runtime request should not persist across reboot.
    s_runtimePortalRequested = true;
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, false);
    WiFi.disconnect(true, false);
    s_wifiConnectInFlight = false;
    s_wifiConnectStartMs = 0;
    s_wifiConnectRetryAtMs = 0;
    s_wifiConnectBackoffMs = CFG_WIFI_CONNECT_RETRY_MIN_MS;
}

void wifi_wipeCredentialsAndReboot()
{
    LOG_WARN(LogDomain::WIFI, "Wiping WiFi credentials and rebooting");
    WiFi.disconnect(true, true);
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    LOG_WARN(LogDomain::WIFI, "REBOOTING... reason=wifi_credentials_wiped intent=wifi_wipe");
    Serial.flush();
    delay(100);
    ESP.restart();
}
