#include "wifi_provisioning.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <time.h>
#include "logger.h"

static const char *PREF_KEY_FORCE_PORTAL = "force_portal";
static const time_t VALID_TIME_EPOCH = 1600000000;

static Preferences wifiPrefs; // WiFi-related preferences

bool wifi_timeIsValid()
{
    return time(nullptr) > VALID_TIME_EPOCH;
}

static void wifi_syncTime()
{
    if (wifi_timeIsValid())
    {
        return;
    }

    LOG_INFO(LogDomain::WIFI, "Starting NTP sync (pool.ntp.org, time.google.com)");
    configTime(0, 0, "pool.ntp.org", "time.google.com");

    while (!wifi_timeIsValid())
    {
        delay(250);
    }

    LOG_INFO(LogDomain::WIFI, "System time valid epoch=%lu", (unsigned long)time(nullptr));
}

// Initialize WiFi provisioning module
void wifi_begin()
{
    wifiPrefs.begin("wifi", false); // namespace "wifi", read-write
}

static void startPortal()
{
    LOG_INFO(LogDomain::WIFI, "Starting captive portal (setup mode)...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true, true);
    delay(100);

    WiFiManager wm;
    wm.setConfigPortalTimeout(180); // seconds
    wm.setConnectTimeout(20);
    wm.setConnectRetries(2);

    wm.setHostname("water-tank-esp32");

    bool ok = wm.startConfigPortal("WaterTank-Setup");

    if (!ok)
    {
        LOG_WARN(LogDomain::WIFI, "Portal timed out or failed. Rebooting...");
        delay(500);
        ESP.restart();
    }

    LOG_INFO(LogDomain::WIFI, "WiFi configured and connected ip=%s", WiFi.localIP().toString().c_str());
    wifi_syncTime();

    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, false);
}

// Ensure WiFi is connected, otherwise start captive portal
void wifi_ensureConnected(uint32_t wifiTimeoutMs)
{
    // If already connected, ensure time has been synchronized once.
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_syncTime();
        return;
    }

    WiFi.mode(WIFI_STA); // station mode

    // Check if we need to force the portal
    if (wifiPrefs.getBool(PREF_KEY_FORCE_PORTAL, false))
    {
        startPortal(); // start captive portal
        return;
    }

    LOG_INFO(LogDomain::WIFI, "Connecting to saved WiFi%s", WiFi.SSID().length() ? "" : " (no saved SSID)");
    if (WiFi.SSID().length())
    {
        LOG_INFO(LogDomain::WIFI, "SSID=%s", WiFi.SSID().c_str());
    }

    WiFi.begin(); // use stored credentials

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);

        if (millis() - start > wifiTimeoutMs)
        {
            LOG_WARN(LogDomain::WIFI, "Connection failed, launching portal...");
            startPortal(); // start captive portal
            return;
        }
    }

    LOG_INFO(LogDomain::WIFI, "Connected ip=%s", WiFi.localIP().toString().c_str());
    wifi_syncTime();
}

void wifi_requestPortal()
{
    LOG_INFO(LogDomain::WIFI, "Forcing captive portal");
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    WiFi.disconnect(true, true);
    delay(250);
}

void wifi_wipeCredentialsAndReboot()
{
    LOG_WARN(LogDomain::WIFI, "Wiping WiFi credentials and rebooting");
    WiFi.disconnect(true, true);
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    delay(500);
    ESP.restart();
}
