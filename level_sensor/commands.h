#pragma once
#include <stddef.h>
#include <stdint.h>

// payload is raw MQTT bytes (not null terminated)
void commands_handle(const uint8_t *payload, size_t len);