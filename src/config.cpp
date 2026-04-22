#include "config.h"
#include "secrets.h"
#include <Preferences.h>
#include <string.h>

static const char* NVS_NS = "calclock";

void config_defaults(AppConfig &cfg) {
    strncpy(cfg.server_url, DEFAULT_CALENDAR_URL, sizeof(cfg.server_url) - 1);
    cfg.server_url[sizeof(cfg.server_url) - 1] = '\0';
    cfg.wifi_ssid[0]     = '\0';
    cfg.wifi_password[0] = '\0';
    cfg.read_token[0]    = '\0';
    cfg.refresh_secs     = 300;
    strncpy(cfg.timezone, "Europe/London", sizeof(cfg.timezone) - 1);
    cfg.timezone[sizeof(cfg.timezone) - 1] = '\0';
    strncpy(cfg.mqtt_host, DEFAULT_MQTT_HOST_VAL, sizeof(cfg.mqtt_host) - 1);
    cfg.mqtt_host[sizeof(cfg.mqtt_host) - 1] = '\0';
    cfg.mqtt_port = DEFAULT_MQTT_PORT_VAL;
    strncpy(cfg.mqtt_topic, DEFAULT_MQTT_TOPIC_VAL, sizeof(cfg.mqtt_topic) - 1);
    cfg.mqtt_topic[sizeof(cfg.mqtt_topic) - 1] = '\0';
}

void config_load(AppConfig &cfg) {
    config_defaults(cfg);
    Preferences prefs;
    // Open read-write so namespace is created on first boot (avoids NOT_FOUND error)
    if (!prefs.begin(NVS_NS, false)) return;
    prefs.getString("wifi_ssid",  cfg.wifi_ssid,     sizeof(cfg.wifi_ssid));
    prefs.getString("wifi_pass",  cfg.wifi_password, sizeof(cfg.wifi_password));
    prefs.getString("server_url", cfg.server_url,    sizeof(cfg.server_url));
    prefs.getString("read_token", cfg.read_token,    sizeof(cfg.read_token));
    prefs.getString("timezone", cfg.timezone, sizeof(cfg.timezone));
    cfg.refresh_secs = prefs.getUShort("refresh_secs", 300);
    if (cfg.refresh_secs < 60 || cfg.refresh_secs > 3600) cfg.refresh_secs = 300;
    prefs.getString("mqtt_host",  cfg.mqtt_host,  sizeof(cfg.mqtt_host));
    cfg.mqtt_port  = prefs.getUShort("mqtt_port", 1883);
    prefs.getString("mqtt_topic", cfg.mqtt_topic, sizeof(cfg.mqtt_topic));
    prefs.end();
}

void config_save(const AppConfig &cfg) {
    Preferences prefs;
    prefs.begin(NVS_NS, false);
    prefs.putString("wifi_ssid",    cfg.wifi_ssid);
    prefs.putString("wifi_pass",    cfg.wifi_password);
    prefs.putString("server_url",   cfg.server_url);
    prefs.putString("read_token",   cfg.read_token);
    prefs.putString("timezone",    cfg.timezone);
    prefs.putUShort("refresh_secs", cfg.refresh_secs);
    prefs.putString("mqtt_host",   cfg.mqtt_host);
    prefs.putUShort("mqtt_port",   cfg.mqtt_port);
    prefs.putString("mqtt_topic",  cfg.mqtt_topic);
    prefs.end();
}
