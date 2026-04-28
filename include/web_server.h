#pragma once
#include "config.h"

extern bool g_ap_mode;

// Called after a live (no-restart) settings save. Implement in main.cpp.
void on_config_updated();

void ws_start_ap(AppConfig &cfg);
void ws_start_sta(AppConfig &cfg);
void ws_handle();
