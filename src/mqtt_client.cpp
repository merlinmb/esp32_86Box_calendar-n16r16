#include "mqtt_client.h"
#include "display.h"
#include "config.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>

static WiFiClient    s_wifi_client;
static PubSubClient  s_mqtt(s_wifi_client);
static AppConfig    *s_cfg     = nullptr;
static bool          s_enabled = false;

static const uint32_t RECONNECT_INTERVAL_MS = 5000;
static uint32_t s_last_reconnect_ms = 0;

// ── Message handler ───────────────────────────────────────────────────────────

static void on_message(char *topic, byte *payload, unsigned int len) {
    if (len == 0 || len > 4) return;  // 0–100 is at most 3 digits

    char buf[5];
    memcpy(buf, payload, len);
    buf[len] = '\0';

    int pct = atoi(buf);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint8_t level = (uint8_t)((pct * 255) / 100);
    Serial.printf("[mqtt] brightness %d%% -> %d\n", pct, level);
    display_set_brightness(level);
}

// ── Connection helper ─────────────────────────────────────────────────────────

static bool try_connect() {
    Serial.printf("[mqtt] Connecting to %s:%u ...\n",
                  s_cfg->mqtt_host, s_cfg->mqtt_port);
    if (!s_mqtt.connect("esp32_calendar")) {
        Serial.printf("[mqtt] Failed, state=%d\n", s_mqtt.state());
        return false;
    }
    s_mqtt.subscribe(s_cfg->mqtt_topic);
    Serial.printf("[mqtt] Connected. Subscribed to '%s'\n", s_cfg->mqtt_topic);
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void mqtt_client_init(AppConfig &cfg) {
    s_cfg = &cfg;
    if (cfg.mqtt_host[0] == '\0') {
        Serial.println("[mqtt] No broker configured — disabled");
        return;
    }
    s_enabled = true;
    s_mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);
    s_mqtt.setCallback(on_message);
    try_connect();
}

void mqtt_client_tick() {
    if (!s_enabled) return;
    if (WiFi.status() != WL_CONNECTED) {
        if (s_mqtt.connected()) {
            s_mqtt.disconnect();
        }
        return;
    }
    if (s_mqtt.connected()) {
        s_mqtt.loop();
        return;
    }
    uint32_t now = millis();
    if (now - s_last_reconnect_ms >= RECONNECT_INTERVAL_MS) {
        s_last_reconnect_ms = now;
        try_connect();
    }
}

bool mqtt_client_connected() {
    return s_enabled && s_mqtt.connected();
}
