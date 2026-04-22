#pragma once
#include <stdint.h>

static constexpr uint8_t CAL_MAX_EVENTS = 32;

struct AgendaEntry {
    char title[96];
    char location[64];
    char calendar_name[48];
    char source[16];        // "google", "microsoft", or ""
    char time_start[6];
    char time_end[6];
    int32_t ts_start_local;
    int32_t ts_end_local;
    bool is_all_day;
};

struct CalEvent {
    AgendaEntry items[CAL_MAX_EVENTS];
    uint8_t count;
    bool has_event;
};

void display_init();
bool display_ready();
void display_show_setup();
void display_show_connecting(const char *ssid = nullptr);
void display_render(const CalEvent &ev, bool offline);
void display_update_clock(bool offline);
void display_breathe(const CalEvent &ev, bool offline);
void display_set_brightness(uint8_t level);
