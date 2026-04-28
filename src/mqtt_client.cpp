#include "mqtt_client.h"
#include "display.h"
#include "config.h"
#include "secrets.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <Arduino.h>
#include <esp_system.h>

static WiFiClient    s_wifi_client;
static PubSubClient  s_mqtt(s_wifi_client);
static AppConfig    *s_cfg     = nullptr;
static bool          s_enabled = false;

static const uint32_t RECONNECT_INTERVAL_MS = 5000;
static uint32_t s_last_reconnect_ms = 0;

static void reset_transport() {
    if (s_mqtt.connected()) {
        s_mqtt.disconnect();
    }
    s_wifi_client.stop();
}

// ── MQTT publish helpers ──────────────────────────────────────────────────────

static void mqttPublishStat(String topic, String payload) {
    if (!s_mqtt.connected()) return;
    String full_topic = String(DEFAULT_DEVICE_NAME) + "/stat/" + topic;
    s_mqtt.publish(full_topic.c_str(), payload.c_str());
}

static String IpAddress2String(IPAddress ip) {
    return String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
}

void mqttTransmitInitStat(String deviceName) {
    mqttPublishStat("init", "{\"value1\":\"" + IpAddress2String(WiFi.localIP()) +
                    "\",\"value2\":\"" + WiFi.macAddress() + "\",\"value3\":\"" + deviceName + "\"}");
}

void mqttTransmitInitStat() {
    mqttTransmitInitStat(DEFAULT_DEVICE_NAME);
}

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
    reset_transport();

    uint64_t chip_id = ESP.getEfuseMac();
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp32_calendar_%08lx", (unsigned long)(chip_id & 0xffffffffULL));

    Serial.printf("[mqtt] Connecting to %s:%u ...\n",
                  s_cfg->mqtt_host, s_cfg->mqtt_port);
    if (!s_mqtt.connect(client_id)) {
        Serial.printf("[mqtt] Failed, state=%d\n", s_mqtt.state());
        return false;
    }

    if (s_cfg->mqtt_topic[0] != '\0' && !s_mqtt.subscribe(s_cfg->mqtt_topic)) {
        Serial.printf("[mqtt] Subscribe failed for '%s'\n", s_cfg->mqtt_topic);
        reset_transport();
        return false;
    }

    Serial.printf("[mqtt] Connected. Subscribed to '%s'\n", s_cfg->mqtt_topic);
    mqttTransmitInitStat();
    return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void mqtt_client_init(AppConfig &cfg) {
    s_cfg = &cfg;
    reset_transport();
    s_last_reconnect_ms = 0;

    if (cfg.mqtt_host[0] == '\0' || cfg.mqtt_topic[0] == '\0') {
        s_enabled = false;
        Serial.println("[mqtt] No broker configured — disabled");
        return;
    }

    s_enabled = true;
    s_mqtt.setServer(cfg.mqtt_host, cfg.mqtt_port);
    s_mqtt.setCallback(on_message);
    s_mqtt.setKeepAlive(30);
    s_mqtt.setSocketTimeout(3);
    try_connect();
}

void mqtt_client_on_wifi_disconnected() {
    if (!s_enabled) return;
    reset_transport();
    s_last_reconnect_ms = millis();
}

void mqtt_client_tick() {
    if (!s_enabled) return;
    if (WiFi.status() != WL_CONNECTED) {
        mqtt_client_on_wifi_disconnected();
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
