#include "web_server.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <string.h>
#include <stdlib.h>
#include "timezone_db.h"

bool g_ap_mode = false;

static WebServer  s_server(80);
static DNSServer  s_dns;
static AppConfig *s_cfg_ptr = nullptr;

static void handle_root() {
    // Build timezone <select> options from the lookup table
    String tz_opts = "";
    const char* const* names = tz_names();
    int count = tz_count();
    const char* saved_tz = s_cfg_ptr ? s_cfg_ptr->timezone : "Europe/London";
    for (int i = 0; i < count; i++) {
        tz_opts += "<option value=\"";
        tz_opts += names[i];
        tz_opts += "\"";
        if (strcmp(names[i], saved_tz) == 0) tz_opts += " selected";
        tz_opts += ">";
        tz_opts += names[i];
        tz_opts += "</option>";
    }

    String html = F("<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Calendar Setup</title>"
        "<style>"
        "body{background:#0b0c11;color:#e8eaf2;font-family:system-ui,sans-serif;"
        "display:flex;justify-content:center;padding:20px}"
        ".card{background:#111318;border-radius:10px;padding:24px;width:100%;max-width:420px}"
        "h1{color:#5b8ef0;font-size:18px;margin:0 0 20px}"
        "label{display:block;color:#8b90aa;font-size:12px;margin:12px 0 4px}"
        "input,select{width:100%;box-sizing:border-box;background:#1e202c;"
        "border:1px solid rgba(255,255,255,0.12);border-radius:6px;"
        "color:#e8eaf2;padding:8px 10px;font-size:14px}"
        "button{margin-top:20px;width:100%;background:#5b8ef0;color:#0b0c11;border:none;"
        "border-radius:6px;padding:10px;font-size:15px;font-weight:600;cursor:pointer}"
        ".note{color:#8b90aa;font-size:11px;margin-top:6px}"
        "</style>"
        "</head><body><div class='card'>"
        "<h1>nextUp &mdash; Calendar Setup</h1>"
        "<form method='POST' action='/save'>"
        "<label>WiFi SSID</label>"
        "<input name='ssid' required maxlength='63'>"
        "<label>WiFi Password</label>"
        "<input name='pass' type='password' maxlength='63'>"
        "<label>Calendar Server URL</label>"
        "<input name='url' maxlength='255' value='");
    html += String(s_cfg_ptr ? s_cfg_ptr->server_url : "");
    html += F("'>"
        "<label>Read Token <span class='note'>(optional)</span></label>"
        "<input name='token' maxlength='127'>"
        "<label>Timezone</label>"
        "<select name='tz'>");
    html += tz_opts;
    html += F("</select>"
        "<label>Refresh Interval (seconds, 60&ndash;3600)</label>"
        "<input name='refresh' type='number' min='60' max='3600' value='");
    html += String(s_cfg_ptr ? s_cfg_ptr->refresh_secs : 300);
    html += F("'>"
        "<label>MQTT Host</label>"
        "<input name='mqtt_host' maxlength='63' value='");
    html += String(s_cfg_ptr ? s_cfg_ptr->mqtt_host : "");
    html += F("'>"
        "<label>MQTT Port</label>"
        "<input name='mqtt_port' type='number' min='1' max='65535' value='");
    html += String(s_cfg_ptr ? s_cfg_ptr->mqtt_port : 1883);
    html += F("'>"
        "<label>MQTT Brightness Topic</label>"
        "<input name='mqtt_topic' maxlength='127' value='");
    html += String(s_cfg_ptr ? s_cfg_ptr->mqtt_topic : "");
    html += F("'>"
        "<button type='submit'>Save &amp; Restart</button>"
        "</form></div></body></html>");

    s_server.send(200, "text/html", html);
}

static void handle_save() {
    if (!s_cfg_ptr) { s_server.send(500, "text/plain", "err"); return; }

    String ssid = s_server.arg("ssid");
    if (ssid.length() == 0) {
        s_server.send(400, "text/plain", "SSID required");
        return;
    }

    strncpy(s_cfg_ptr->wifi_ssid,     ssid.c_str(), sizeof(s_cfg_ptr->wifi_ssid) - 1);
    s_cfg_ptr->wifi_ssid[sizeof(s_cfg_ptr->wifi_ssid) - 1] = '\0';

    String pass = s_server.arg("pass");
    strncpy(s_cfg_ptr->wifi_password, pass.c_str(), sizeof(s_cfg_ptr->wifi_password) - 1);
    s_cfg_ptr->wifi_password[sizeof(s_cfg_ptr->wifi_password) - 1] = '\0';

    String url = s_server.arg("url");
    if (url.length() > 0) {
        strncpy(s_cfg_ptr->server_url, url.c_str(), sizeof(s_cfg_ptr->server_url) - 1);
        s_cfg_ptr->server_url[sizeof(s_cfg_ptr->server_url) - 1] = '\0';
    }

    String token = s_server.arg("token");
    strncpy(s_cfg_ptr->read_token, token.c_str(), sizeof(s_cfg_ptr->read_token) - 1);
    s_cfg_ptr->read_token[sizeof(s_cfg_ptr->read_token) - 1] = '\0';

    String tz = s_server.arg("tz");
    if (tz.length() > 0) {
        strncpy(s_cfg_ptr->timezone, tz.c_str(), sizeof(s_cfg_ptr->timezone) - 1);
        s_cfg_ptr->timezone[sizeof(s_cfg_ptr->timezone) - 1] = '\0';
    }

    int refresh = s_server.arg("refresh").toInt();
    if (refresh < 60 || refresh > 3600) refresh = 300;
    s_cfg_ptr->refresh_secs = (uint16_t)refresh;

    String mqtt_host = s_server.arg("mqtt_host");
    const char *mqtt_host_value = mqtt_host.length() > 0 ? mqtt_host.c_str() : s_cfg_ptr->mqtt_host;
    strncpy(s_cfg_ptr->mqtt_host, mqtt_host_value, sizeof(s_cfg_ptr->mqtt_host) - 1);
    s_cfg_ptr->mqtt_host[sizeof(s_cfg_ptr->mqtt_host) - 1] = '\0';

    int mqtt_port = s_server.arg("mqtt_port").toInt();
    if (mqtt_port < 1 || mqtt_port > 65535) mqtt_port = s_cfg_ptr->mqtt_port;
    s_cfg_ptr->mqtt_port = (uint16_t)mqtt_port;

    String mqtt_topic = s_server.arg("mqtt_topic");
    const char *mqtt_topic_value = mqtt_topic.length() > 0 ? mqtt_topic.c_str() : s_cfg_ptr->mqtt_topic;
    strncpy(s_cfg_ptr->mqtt_topic, mqtt_topic_value, sizeof(s_cfg_ptr->mqtt_topic) - 1);
    s_cfg_ptr->mqtt_topic[sizeof(s_cfg_ptr->mqtt_topic) - 1] = '\0';

    config_save(*s_cfg_ptr);

    s_server.send(200, "text/html",
        "<!DOCTYPE html><html><body style='background:#0b0c11;color:#e8eaf2;"
        "font-family:system-ui;display:flex;justify-content:center;padding:40px'>"
        "<div style='text-align:center'>"
        "<p style='color:#5b8ef0;font-size:20px'>Saved! Restarting...</p>"
        "</div></body></html>");
    delay(1500);
    ESP.restart();
}

void ws_start_ap(AppConfig &cfg) {
    s_cfg_ptr = &cfg;
    g_ap_mode = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP("CalendarSetup");
    delay(100);

    s_dns.start(53, "*", WiFi.softAPIP());

    s_server.on("/",     HTTP_GET,  handle_root);
    s_server.on("/save", HTTP_POST, handle_save);
    s_server.onNotFound([]() {
        // Captive portal redirect — any URL triggers setup page
        s_server.sendHeader("Location", "http://192.168.4.1/", true);
        s_server.send(302, "text/plain", "");
    });
    s_server.begin();
}

void ws_start_sta(AppConfig &cfg) {
    s_cfg_ptr = &cfg;
    // g_ap_mode stays false — normal loop continues alongside web server

    s_server.on("/",     HTTP_GET,  handle_root);
    s_server.on("/save", HTTP_POST, handle_save);
    s_server.begin();
    Serial.printf("[web] Config server running at http://%s/\n", WiFi.localIP().toString().c_str());
}

void ws_handle() {
    if (g_ap_mode) s_dns.processNextRequest();
    s_server.handleClient();
}
