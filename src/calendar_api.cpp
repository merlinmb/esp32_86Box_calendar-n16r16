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

static CalEvent s_last_successful_read;
static bool s_has_last_successful_read = false;

static bool restore_previous_read(CalEvent &ev, const char *reason) {
    if (!s_has_last_successful_read) {
        return false;
    }

    ev = s_last_successful_read;
    Serial.printf("[cal] %s; using previous read (%u events)\n", reason, ev.count);
    return true;
}

bool calendar_fetch(const AppConfig &cfg, CalEvent &ev, Timezone *tz) {
    memset(&ev, 0, sizeof(ev));

    Serial.printf("[cal] Fetching URL: '%s'\n", cfg.server_url);
    Serial.printf("[cal] Read token configured: %s\n", cfg.read_token[0] != '\0' ? "yes" : "no");

    const int MAX_RETRIES = 3;
    const int RETRY_DELAY_MS = 500;

    // Create client objects once outside retry loop to avoid heap fragmentation
    WiFiClientSecure client;
    client.setInsecure();  // Home LAN device — skip CA verification
    // Force HTTP/1.1 — ESP32 WiFiClientSecure doesn't support HTTP/2 (h2).
    // Without this, nginx servers with h2 enabled reject the ALPN negotiation.
    static const char *alpn_protos[] = {"http/1.1", nullptr};
    client.setAlpnProtocols(alpn_protos);

    HTTPClient http;
    http.setTimeout(10000);
    http.setReuse(false);
    http.useHTTP10(true);

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1) {
            Serial.printf("[cal] Retry attempt %d/%d after %dms delay\n", attempt, MAX_RETRIES, RETRY_DELAY_MS);
            delay(RETRY_DELAY_MS);
        }

        if (!http.begin(client, cfg.server_url)) {
            Serial.printf("[cal] http.begin() failed on attempt %d — bad URL?\n", attempt);
            http.end();
            client.flush();
            client.stop();
            delay(10);  // Allow TCP stack to clean up socket
            continue;  // Retry on http.begin() failure
        }

        if (cfg.read_token[0] != '\0') {
            char bearer[140];
            snprintf(bearer, sizeof(bearer), "Bearer %s", cfg.read_token);
            http.addHeader("Authorization", bearer);
        }

        int code = http.GET();
        Serial.printf("[cal] HTTP response code: %d (attempt %d/%d)\n", code, attempt, MAX_RETRIES);

        if (code != 200) {
            if (code > 0) {
                String errBody = http.getString();
                Serial.printf("[cal] Response body: %.200s\n", errBody.c_str());
                http.end();
                client.flush();
                client.stop();
                delay(10);  // Allow TCP stack to clean up socket
                return false;  // HTTP error (not timeout) — don't retry
            } else {
                Serial.printf("[cal] HTTP error: %s\n", http.errorToString(code).c_str());
                http.end();
                client.flush();
                client.stop();
                delay(10);  // Allow TCP stack to clean up socket
                // Retry on transient errors: connection refused (-1), connection lost (-5), timeout (-11)
                bool is_transient = (code == -1 || code == -5 || code == -11);
                if (attempt < MAX_RETRIES && is_transient) {
                    continue;  // Retry on transient network errors
                }
                if (code == -11 && restore_previous_read(ev, "Read timeout")) {
                    return true;
                }
                return false;  // Failed after retries
            }
        }

        // Success — parse JSON
        JsonDocument doc;
        auto *stream = http.getStreamPtr();
        unsigned long read_wait_started = millis();
        while (stream && stream->available() == 0 && stream->connected() && millis() - read_wait_started < 1000UL) {
            delay(10);
        }

        if (!stream || stream->available() == 0) {
            http.end();
            client.flush();
            client.stop();
            delay(10);  // Allow TCP stack to clean up socket
            if (attempt < MAX_RETRIES) {
                Serial.printf("[cal] No response body available on attempt %d/%d\n", attempt, MAX_RETRIES);
                continue;
            }
            if (restore_previous_read(ev, "No response body available")) {
                return true;
            }
            return false;
        }

        DeserializationError err = deserializeJson(doc, *stream);
        http.end();
        client.flush();
        client.stop();
        delay(10);  // Allow TCP stack to clean up socket
        if (err) {
            Serial.printf("[cal] JSON parse error: %s\n", err.c_str());
            if (attempt < MAX_RETRIES) {
                continue;  // Retry on JSON parse error
            }
            return false;
        }

        // Successfully fetched and parsed — process events
        JsonArray events = doc["events"].as<JsonArray>();
        Serial.printf("[cal] events array: %s, size=%d\n",
                      events.isNull() ? "null" : "ok",
                      events.isNull() ? 0 : (int)events.size());
        if (events.isNull() || events.size() == 0) {
            ev.has_event = false;
            return true;  // Successful fetch, just no events
        }

        const time_t now_ts = ::now();
        // Sanity-check: if time hasn't synced yet (NTP failed), epoch will be near 0
        // and all real events fall outside the 1970 window — skip filtering entirely.
        static constexpr time_t MIN_SANE_EPOCH = 1700000000L; // ~Nov 2023
        if (now_ts < MIN_SANE_EPOCH) {
            Serial.printf("[cal] Clock not synced (now=%ld) — skipping time-window filter\n", (long)now_ts);
            for (JsonObject entry_json : events) {
                if (ev.count >= CAL_MAX_EVENTS) break;
                const char *title     = entry_json["title"] | "";
                const char *start_str = entry_json["start"] | "";
                const char *end_str   = entry_json["end"]   | "";
                const bool is_all_day = entry_json["isAllDay"] | false;
                if (!title[0]) continue;
                const int32_t utc_start = parse_iso8601_utc(start_str);
                int32_t       utc_end   = parse_iso8601_utc(end_str);
                const int32_t start_local = tz ? (int32_t)tz->toLocal((time_t)utc_start) : utc_start;
                int32_t end_local = utc_end > 0 ? (tz ? (int32_t)tz->toLocal((time_t)utc_end) : utc_end) : 0;
                if (end_local <= start_local)
                    end_local = is_all_day ? start_local + 86400 : start_local;
                AgendaEntry &item = ev.items[ev.count++];
                copy_string(item.title,         sizeof(item.title),         title);
                copy_string(item.location,      sizeof(item.location),      entry_json["location"]  | "");
                copy_string(item.calendar_name, sizeof(item.calendar_name), entry_json["calendar"]  | "");
                copy_string(item.source,        sizeof(item.source),        entry_json["source"]    | "");
                item.ts_start_local = start_local;
                item.ts_end_local   = end_local;
                item.is_all_day     = is_all_day;
                if (is_all_day) { item.time_start[0] = '\0'; item.time_end[0] = '\0'; }
                else { epoch_to_hhmm(start_local, item.time_start); epoch_to_hhmm(end_local, item.time_end); }
            }
            ev.has_event = ev.count > 0;
            s_last_successful_read = ev;
            s_has_last_successful_read = true;
            return true;
        }

        Serial.printf("[cal] Time window: now=%ld  window=[%ld, %ld)\n",
                      (long)now_ts, (long)local_day_start(now_ts), (long)(local_day_start(now_ts) + 7*86400));

        const time_t window_start = local_day_start(now_ts);
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
                Serial.printf("[cal] Skip '%s': start_local=%ld end_local=%ld window=[%ld,%ld)\n",
                              title, (long)start_local, (long)end_local, (long)window_start, (long)window_end);
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
        s_last_successful_read = ev;
        s_has_last_successful_read = true;
        return true;  // Successfully completed
    }

    return false;  // All retries exhausted
}
