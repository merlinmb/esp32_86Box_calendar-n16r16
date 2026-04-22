#pragma once
#include "config.h"

// Initialise the MQTT client. If mqtt_host is empty the client is disabled.
// Must be called after WiFi is connected.
void mqtt_client_init(AppConfig &cfg);

// Must be called every loop() iteration to maintain connection and
// process incoming messages.
void mqtt_client_tick();

// Returns true when the MQTT broker session is currently established.
bool mqtt_client_connected();
