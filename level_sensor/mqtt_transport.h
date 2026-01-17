#pragma once
#include <stdint.h>
#include <stddef.h>

struct DeviceState;

// Begin MQTT (sets server, callback etc.)
void mqtt_begin();

// Call frequently from loop()
void mqtt_loop();

// Publish retained state JSON snapshot
bool mqtt_publishState(const DeviceState &state);

// Force re-publish state (useful on reconnect)
void mqtt_requestStatePublish();

// MQTT connection status
bool mqtt_isConnected();