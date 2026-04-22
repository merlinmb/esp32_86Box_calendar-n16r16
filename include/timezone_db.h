#pragma once
#include <Timezone.h>   // PaulStoffregen/Time

// Returns a heap-allocated Timezone for the given region name string.
// Caller owns the pointer. Returns UTC if name is unrecognised.
Timezone* tz_lookup(const char *name);

// Returns a null-terminated list of supported region name strings.
// Used to populate the web config dropdown.
const char* const* tz_names();
int tz_count();
