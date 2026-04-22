#include "timezone_db.h"

static const char* const NAMES[] = {
    "Europe/London",
    "Europe/Paris",
    "Europe/Athens",
    "America/New_York",
    "America/Chicago",
    "America/Denver",
    "America/Los_Angeles",
    "UTC",
};

static const int TZ_COUNT = sizeof(NAMES) / sizeof(NAMES[0]);

const char* const* tz_names() { return NAMES; }
int tz_count() { return TZ_COUNT; }

Timezone* tz_lookup(const char *name) {
    // Europe/London: GMT/BST  (last Sun Mar 01:00 UTC -> BST, last Sun Oct 01:00 UTC -> GMT)
    if (strcmp(name, "Europe/London") == 0) {
        static TimeChangeRule bst = {"BST", Last, Sun, Mar, 1, 60};
        static TimeChangeRule gmt = {"GMT", Last, Sun, Oct, 2, 0};
        return new Timezone(bst, gmt);
    }
    // Europe/Paris: CET/CEST  (last Sun Mar 02:00 -> CEST +120, last Sun Oct 03:00 -> CET +60)
    if (strcmp(name, "Europe/Paris") == 0) {
        static TimeChangeRule cest = {"CEST", Last, Sun, Mar, 2, 120};
        static TimeChangeRule cet  = {"CET",  Last, Sun, Oct, 3, 60};
        return new Timezone(cest, cet);
    }
    // Europe/Athens: EET/EEST  (+120/+180)
    if (strcmp(name, "Europe/Athens") == 0) {
        static TimeChangeRule eest = {"EEST", Last, Sun, Mar, 3, 180};
        static TimeChangeRule eet  = {"EET",  Last, Sun, Oct, 4, 120};
        return new Timezone(eest, eet);
    }
    // America/New_York: EST/EDT  (2nd Sun Mar 02:00 -> EDT -240, 1st Sun Nov 02:00 -> EST -300)
    if (strcmp(name, "America/New_York") == 0) {
        static TimeChangeRule edt = {"EDT", Second, Sun, Mar, 2, -240};
        static TimeChangeRule est = {"EST", First,  Sun, Nov, 2, -300};
        return new Timezone(edt, est);
    }
    // America/Chicago: CST/CDT  (-360/-300)
    if (strcmp(name, "America/Chicago") == 0) {
        static TimeChangeRule cdt = {"CDT", Second, Sun, Mar, 2, -300};
        static TimeChangeRule cst = {"CST", First,  Sun, Nov, 2, -360};
        return new Timezone(cdt, cst);
    }
    // America/Denver: MST/MDT  (-420/-360)
    if (strcmp(name, "America/Denver") == 0) {
        static TimeChangeRule mdt = {"MDT", Second, Sun, Mar, 2, -360};
        static TimeChangeRule mst = {"MST", First,  Sun, Nov, 2, -420};
        return new Timezone(mdt, mst);
    }
    // America/Los_Angeles: PST/PDT  (-480/-420)
    if (strcmp(name, "America/Los_Angeles") == 0) {
        static TimeChangeRule pdt = {"PDT", Second, Sun, Mar, 2, -420};
        static TimeChangeRule pst = {"PST", First,  Sun, Nov, 2, -480};
        return new Timezone(pdt, pst);
    }
    // UTC fallback
    static TimeChangeRule utc = {"UTC", Last, Sun, Jan, 1, 0};
    return new Timezone(utc, utc);
}
