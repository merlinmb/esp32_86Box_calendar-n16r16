#pragma once
#include "config.h"

// Initialise the MQTT client. If mqtt_host is empty the client is disabled.
// Must be called after WiFi is connected.
void mqtt_client_init(AppConfig &cfg);

// Must be called every loop() iteration to maintain connection and
// process incoming messages.
void mqtt_client_tick();

// Reset broker/client state after WiFi drops so reconnect attempts start
// from a clean socket state.
void mqtt_client_on_wifi_disconnected();

// Returns true when the MQTT broker session is currently established.
bool mqtt_client_connected();
