#include "ota_service.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

static const char *s_hostName = nullptr;
static const char *s_password = nullptr;
static bool s_started = false;

void ota_begin(const char *hostName, const char *password)
{
    s_hostName = hostName;
    s_password = password;
    s_started = false;
}

void ota_handle()
{
    if (!s_started)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            return;
        }

        if (s_hostName)
        {
            ArduinoOTA.setHostname(s_hostName);
        }
        if (s_password)
        {
            ArduinoOTA.setPassword(s_password);
        }

        ArduinoOTA.onStart([]()
                           { Serial.println("[OTA] Update started"); });

        ArduinoOTA.onEnd([]()
                         { Serial.println("[OTA] Update finished"); });

        ArduinoOTA.onError([](ota_error_t error)
                           { Serial.printf("[OTA] Error %u\n", error); });

        ArduinoOTA.begin();
        s_started = true;

        IPAddress ip = WiFi.localIP();
        Serial.printf("[OTA] started on %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
        return;
    }

    ArduinoOTA.handle();
}