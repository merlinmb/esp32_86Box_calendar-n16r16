#include "calendar_api.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <TimeLib.h>
#include <Timezone.h>

static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<int64_t>(doe) - 719468LL;
}

static time_t tm_to_epoch_utc(const struct tm &t) {
    const int64_t days = days_from_civil(t.tm_year + 1900, static_cast<unsigned>(t.tm_mon + 1), static_cast<unsigned>(t.tm_mday));
    const int64_t seconds = (((days * 24) + t.tm_hour) * 60 + t.tm_min) * 60 + t.tm_sec;
    return static_cast<time_t>(seconds);
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) return;
    if (!src) src = "";
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static int32_t parse_iso8601_utc(const char *s) {
    if (!s || !s[0]) return 0;

    int yr = 0, mo = 0, day = 0, hr = 0, mn = 0, sec = 0;
    int matched = sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &yr, &mo, &day, &hr, &mn, &sec);
    if (matched < 3) {
        matched = sscanf(s, "%4d-%2d-%2d", &yr, &mo, &day);
        if (matched < 3) return 0;
    }

    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year  = yr - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = day;
    t.tm_hour  = hr;
    t.tm_min   = mn;
    t.tm_sec   = sec;
    time_t result = tm_to_epoch_utc(t);
    return (result == (time_t)-1) ? 0 : (int32_t)result;
}

static void epoch_to_hhmm(int32_t local_epoch, char *out6) {
    if (!out6) return;
    if (local_epoch <= 0) {
        out6[0] = '\0';
        return;
    }

    struct tm t;
    time_t local = (time_t)local_epoch;
    gmtime_r(&local, &t);
    snprintf(out6, 6, "%02d:%02d", t.tm_hour, t.tm_min);
}

static time_t local_day_start(time_t local_now) {
    struct tm t;
    gmtime_r(&local_now, &t);
    t.tm_hour = 0;
    t.tm_min = 0;
    t.tm_sec = 0;
    return tm_to_epoch_utc(t);
}

static bool overlaps_day_window(int32_t start_local, int32_t end_local,
                                time_t window_start, time_t window_end) {
    time_t normalized_end = end_local > start_local ? end_local : start_local + 60;
    return normalized_end > window_start && start_local < window_end;
}

bool calendar_fetch(const AppConfig &cfg, CalEvent &ev, Timezone *tz) {
    memset(&ev, 0, sizeof(ev));

    Serial.printf("[cal] Fetching URL: '%s'\n", cfg.server_url);
    Serial.printf("[cal] Read token configured: %s\n", cfg.read_token[0] != '\0' ? "yes" : "no");

    WiFiClientSecure client;
    client.setInsecure();  // Home LAN device — skip CA verification
    // Force HTTP/1.1 — ESP32 WiFiClientSecure doesn't support HTTP/2 (h2).
    // Without this, nginx servers with h2 enabled reject the ALPN negotiation.
    static const char *alpn_protos[] = {"http/1.1", nullptr};
    client.setAlpnProtocols(alpn_protos);

    HTTPClient http;
    http.setTimeout(10000);

    if (!http.begin(client, cfg.server_url)) {
        Serial.println("[cal] http.begin() failed — bad URL?");
        return false;
    }

    if (cfg.read_token[0] != '\0') {
        char bearer[140];
        snprintf(bearer, sizeof(bearer), "Bearer %s", cfg.read_token);
        http.addHeader("Authorization", bearer);
    }

    int code = http.GET();
    Serial.printf("[cal] HTTP response code: %d\n", code);
    if (code != 200) {
        if (code > 0) {
            String errBody = http.getString();
            Serial.printf("[cal] Response body: %s\n", errBody.c_str());
        } else {
            Serial.printf("[cal] HTTP error: %s\n", http.errorToString(code).c_str());
        }
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();
    Serial.printf("[cal] Body length: %d bytes\n", body.length());
    Serial.printf("[cal] Body preview: %.200s\n", body.c_str());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[cal] JSON parse error: %s\n", err.c_str());
        return false;
    }

    JsonArray events = doc["events"].as<JsonArray>();
    Serial.printf("[cal] events array: %s, size=%d\n",
                  events.isNull() ? "null" : "ok",
                  events.isNull() ? 0 : (int)events.size());
    if (events.isNull() || events.size() == 0) {
        ev.has_event = false;
        return true;  // Successful fetch, just no events
    }

    const time_t window_start = local_day_start(::now());
    const time_t window_end = window_start + (7 * 86400);

    for (JsonObject entry_json : events) {
        if (ev.count >= CAL_MAX_EVENTS) {
            break;
        }

        const char *title = entry_json["title"] | "";
        const char *start_str = entry_json["start"] | "";
        const char *end_str = entry_json["end"] | "";
        const bool is_all_day = entry_json["isAllDay"] | false;

        const int32_t utc_start = parse_iso8601_utc(start_str);
        int32_t utc_end = parse_iso8601_utc(end_str);
        if (!title[0] || utc_start <= 0) {
            continue;
        }

        const int32_t start_local = tz ? (int32_t)tz->toLocal((time_t)utc_start) : utc_start;
        int32_t end_local = utc_end > 0 ? (tz ? (int32_t)tz->toLocal((time_t)utc_end) : utc_end) : 0;
        if (end_local <= start_local) {
            end_local = is_all_day ? start_local + 86400 : start_local;
        }

        if (!overlaps_day_window(start_local, end_local, window_start, window_end)) {
            continue;
        }

        AgendaEntry &item = ev.items[ev.count++];
        copy_string(item.title, sizeof(item.title), title);
        copy_string(item.location, sizeof(item.location), entry_json["location"] | "");
        copy_string(item.calendar_name, sizeof(item.calendar_name), entry_json["calendar"] | "");
        copy_string(item.source, sizeof(item.source), entry_json["source"] | "");
        item.ts_start_local = start_local;
        item.ts_end_local = end_local;
        item.is_all_day = is_all_day;

        if (item.is_all_day) {
            item.time_start[0] = '\0';
            item.time_end[0] = '\0';
        } else {
            epoch_to_hhmm(item.ts_start_local, item.time_start);
            epoch_to_hhmm(item.ts_end_local, item.time_end);
        }

        Serial.printf("[cal] Agenda item %d: title='%s' start='%s' end='%s' allDay=%d\n",
                      ev.count, item.title, start_str, end_str, item.is_all_day);
    }

    ev.has_event = ev.count > 0;

    return true;
}
