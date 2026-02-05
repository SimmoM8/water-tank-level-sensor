#include "main.h"

//  initialize everything once
void setup()
{
    /*
main.cpp → appSetup()
    1.	Serial + basic boot info
    •	start Serial
    •	print version/device id
    2.	Load persisted “applied truth”
storage_nvs.cpp
    •	load calibration (dry/wet/inverted)
    •	load tank params (rod length, liters, etc)
    •	load simulation flags
    •	if missing → set defaults (but still “applied”)
    3.	Init probe/simulation
probe_reader.cpp / simulation.cpp
    •	configure pins / touch channel / RC timing
    •	if simulation enabled, prepare sim generator
    4.	Connect Wi-Fi
wifi_provisioning.cpp
    •	connect using saved creds
    •	if no creds → captive portal
    •	keep trying until connected (or safe timeout + retry loop)
    5.	Setup OTA
ota.cpp
    •	ArduinoOTA.begin()
    •	set password callbacks/logging
    6.	Connect MQTT
mqtt_client.cpp
    •	connect to broker
    •	subscribe to water_tank/<id>/cmd
    •	set callback for incoming cmd messages
    7.	Publish initial retained state
device_state.cpp + mqtt_client.cpp
    •	build state JSON from applied truth + current health
    •	publish retained to .../state

At this point device is “online” and clients can immediately render a full state snapshot.
    */
    appSetup();
}

// repeatedly run the main loop
void loop()
{
    /*
    level_sensor_app.cpp → appLoop() (called many times per second)

Think of appLoop() as three concurrent “ticks”:

Tick A: keep infrastructure alive (fast, always)
Runs every loop iteration:
    •	OTA handler
ota.cpp
    •	ArduinoOTA.handle()
    •	Wi-Fi health
wifi_provisioning.cpp
    •	if disconnected → reconnect
    •	MQTT health
mqtt_client.cpp
    •	if disconnected → reconnect + resubscribe
    •	mqtt.loop() to process inbound messages

This ensures OTA + MQTT keep working even if sensor code is slow.

⸻

Tick B: sample sensor + update state (timed)
Runs on a schedule, e.g. every 250ms / 500ms / 1s:
    1.	Read raw
probe_reader.cpp (or simulation.cpp)
    •	get raw reading
    2.	Compute derived values using applied calibration
probe_calibration.cpp
    •	raw → percent
    •	percent → height_cm
    •	height_cm → liters (via tank model)
    3.	Evaluate validity / quality
probe_quality.cpp
    •	probe connected?
    •	raw in range?
    •	calibration present?
    •	set flags: valid.raw, valid.percent, etc
    •	set warnings vs errors
    4.	Update the in-memory device state
device_state.cpp
    •	update readings.*
    •	update health.*

No MQTT publish yet — just update the canonical state object.

⸻

Tick C: publish retained state (timed)
Runs on a slower schedule, e.g. every 5s, and also immediately after commands apply:
    •	Serialize current state to JSON
device_state.cpp
    •	Publish retained to .../state
mqtt_client.cpp

This is the “heartbeat” snapshot clients rely on.
    */
    appLoop();
}