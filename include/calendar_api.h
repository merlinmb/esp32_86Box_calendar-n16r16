#pragma once
#include "display.h"
#include "config.h"
#include <Timezone.h>

// Returns true on success, populates ev. On failure returns false.
// tz is used to convert UTC event times to local for display.
bool calendar_fetch(const AppConfig &cfg, CalEvent &ev, Timezone *tz);
