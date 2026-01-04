#include "wifi_provisioning.h"
#include <WiFi.h>
#include <WiFiManager.h>

static const char *PREF_KEY_FORCE_PORTAL = "force_portal";

void wifiProvisioningBegin(Preferences &prefs)
{
    // Reserved for future use (MQTT provisioning, hostname, etc.)
    (void)prefs;
}

static void startPortal(Preferences &prefs)
{
    Serial.println("[WIFI] Starting captive portal (setup mode)...");

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

    prefs.putBool(PREF_KEY_FORCE_PORTAL, false);
}

void wifiEnsureConnected(Preferences &prefs, uint32_t wifiTimeoutMs)
{
    if (WiFi.status() == WL_CONNECTED)
        return;

    WiFi.mode(WIFI_STA);

    if (prefs.getBool(PREF_KEY_FORCE_PORTAL, false))
    {
        startPortal(prefs);
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
            startPortal(prefs);
            return;
        }
    }

    Serial.println();
    Serial.println("[WIFI] Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
}

void wifiForcePortalNext(Preferences &prefs)
{
    Serial.println("[WIFI] Forcing captive portal");
    prefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    WiFi.disconnect(true, true);
    delay(250);
}

void wifiWipeAndPortal(Preferences &prefs)
{
    Serial.println("[WIFI] Wiping WiFi credentials and rebooting");
    WiFi.disconnect(true, true);
    prefs.putBool(PREF_KEY_FORCE_PORTAL, true);
    delay(500);
    ESP.restart();
}
