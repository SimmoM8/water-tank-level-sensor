#pragma once
#include <stdint.h>
#include <stddef.h>

#include "device_state.h"

struct DeviceState;

struct MqttConfig
{
	const char *host;
	int port;
	const char *clientId;
	const char *user;
	const char *pass;
	const char *baseTopic; // e.g. "water_tank/water_tank_esp32"
	const char *deviceId;  // used for availability
};

using CommandHandlerFn = void (*)(const uint8_t *payload, size_t len);

// Begin MQTT with explicit config and command handler.
void mqtt_begin(const MqttConfig &cfg, CommandHandlerFn cmdHandler);

// Call frequently from loop(); handles keepalive and publishes retained state.
void mqtt_tick(const DeviceState &state);

// Force re-publish state (useful on reconnect or after mutation).
void mqtt_requestStatePublish();

// Publish ACK on the dedicated ack topic (not retained).
bool mqtt_publishAck(const char *reqId, const char *type, CmdStatus status, const char *msg);

// MQTT connection status
bool mqtt_isConnected();