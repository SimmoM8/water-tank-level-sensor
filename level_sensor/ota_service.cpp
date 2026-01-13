#include "ota_service.h"
#include <ArduinoOTA.h>

void ota_begin(const char *hostName, const char *password)
{
    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.setPassword(password);

    ArduinoOTA.onStart([]()
                       { Serial.println("[OTA] Update started"); });

    ArduinoOTA.onEnd([]()
                     { Serial.println("[OTA] Update finished"); });

    ArduinoOTA.onError([](ota_error_t error)
                       { Serial.printf("[OTA] Error %u\n", error); });

    ArduinoOTA.begin();
    Serial.println("[OTA] Ready");
}

void ota_handle()
{
    ArduinoOTA.handle();
}