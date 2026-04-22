#pragma once
#include "config.h"

extern bool g_ap_mode;

void ws_start_ap(AppConfig &cfg);
void ws_start_sta(AppConfig &cfg);
void ws_handle();
