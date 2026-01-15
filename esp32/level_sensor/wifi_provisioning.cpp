#include "wifi_provisioning.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>

static const char *PREF_KEY_FORCE_PORTAL = "force_portal";

static Preferences wifiPrefs; // WiFi-related preferences

// Initialize WiFi provisioning module
void wifi_begin()
{
    wifiPrefs.begin("wifi", false); // namespace "wifi", read-write
}

static void startPortal()
{
    Serial.println("[WIFI] Starting captive portal (setup mode)...");
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
        Serial.println("[WIFI] Portal timed out or failed. Rebooting...");
        delay(500);
        ESP.restart();
    }

    Serial.println("[WIFI] WiFi configured and connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, false);
}

// Ensure WiFi is connected, otherwise start captive portal
void wifi_ensureConnected(uint32_t wifiTimeoutMs)
{
    // If already connected, return
    if (WiFi.status() == WL_CONNECTED)
        return;

    WiFi.mode(WIFI_STA); // station mode

    // Check if we need to force the portal
    if (wifiPrefs.getBool(PREF_KEY_FORCE_PORTAL, false))
    {
        startPortal(); // start captive portal
        return;
    }

    Serial.print("[WIFI] Connecting to saved WiFi");
    if (WiFi.SSID().length())
    {
        Serial.print(": ");
        Serial.println(WiFi.SSID());
    }
    else
    {
        Serial.println(" (no saved SSID)");
    }

    WiFi.begin(); // use stored credentials

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");

        if (millis() - start > wifiTimeoutMs)
        {
            Serial.println();
            Serial.println("[WIFI] Connection failed, launching portal...");
            startPortal(); // start captive portal
            return;
        }
    }

    Serial.println();
    Serial.println("[WIFI] Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
}

void wifi_requestPortal()
{
    Serial.println("[WIFI] Forcing captive portal");
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    WiFi.disconnect(true, true);
    delay(250);
}

void wifi_wipeCredentialsAndReboot()
{
    Serial.println("[WIFI] Wiping WiFi credentials and rebooting");
    WiFi.disconnect(true, true);
    wifiPrefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    delay(500);
    ESP.restart();
}
