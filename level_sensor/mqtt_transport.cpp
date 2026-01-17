#include "mqtt_transport.h"
#include <WiFi.h>
#include <PubSubClient.h>

#include "device_state.h"
#include "state_json.h"
#include "commands.h" // we’ll build next

// ---- topics ----
static const char *DEVICE_ID = "water_tank_esp32";
static const char *BASE = "water_tank/water_tank_esp32";
static const char *TOPIC_STATE = "water_tank/water_tank_esp32/state";
static const char *TOPIC_CMD = "water_tank/water_tank_esp32/cmd";
static const char *TOPIC_AVAIL = "water_tank/water_tank_esp32/availability";

static const char *AVAIL_ONLINE = "online";
static const char *AVAIL_OFFLINE = "offline";

// broker host/creds come from your secrets.h usage in main (or keep them here if you prefer)
extern const char *MQTT_HOST;
extern const int MQTT_PORT;
extern const char *MQTT_CLIENT_ID;
extern const char *MQTT_USER;
extern const char *MQTT_PASS;

static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);

static bool g_publishStateRequested = true;

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    // Only care about cmd topic
    if (strcmp(topic, TOPIC_CMD) != 0)
        return;

    // Hand off to commands module (central API)
    commands_handle(payload, length);

    // After applying command, we want a fresh retained state publish
    g_publishStateRequested = true;
}

void mqtt_begin()
{
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(5);
    mqtt.setBufferSize(1024);
    mqtt.setCallback(mqttCallback);
}

static void mqtt_subscribe()
{
    mqtt.subscribe(TOPIC_CMD);
}

static void mqtt_ensureConnected()
{
    if (mqtt.connected() || WiFi.status() != WL_CONNECTED)
        return;

    // LWT: broker publishes OFFLINE if we drop unexpectedly
    const bool ok = mqtt.connect(
        MQTT_CLIENT_ID,
        MQTT_USER,
        MQTT_PASS,
        TOPIC_AVAIL, 0, true, AVAIL_OFFLINE);

    if (!ok)
        return;

    mqtt.publish(TOPIC_AVAIL, AVAIL_ONLINE, true);
    mqtt_subscribe();

    // request a fresh retained state after reconnect
    g_publishStateRequested = true;
}

void mqtt_loop()
{
    mqtt_ensureConnected();
    if (mqtt.connected())
        mqtt.loop();
}

bool mqtt_isConnected()
{
    return mqtt.connected();
}

void mqtt_requestStatePublish()
{
    g_publishStateRequested = true;
}

bool mqtt_publishState(const DeviceState &state)
{
    if (!mqtt.connected())
        return false;

    static char buf[768]; // tune if needed
    if (!buildStateJson(state, buf, sizeof(buf)))
        return false;

    // retained snapshot
    const bool ok = mqtt.publish(TOPIC_STATE, buf, true);
    g_publishStateRequested = false;
    return ok;
}