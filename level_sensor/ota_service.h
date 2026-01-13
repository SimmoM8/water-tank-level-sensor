#pragma once
#include <Arduino.h>

void ota_begin(const char* hostName), const char* password);
void ota_handle();