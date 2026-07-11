#include "tz_table.h"
#include <string.h>
#include <stddef.h>

typedef struct { const char *iana; const char *posix; } tz_entry_t;

/* POSIX TZ strings include DST rules (Mm.w.d/hh format) for automatic DST transitions.
   DST offsets are relative to standard time, not UTC — the sign convention is inverted. */
static const tz_entry_t s_table[] = {
    /* Universal */
    {"UTC",                  "UTC0"},

    /* Europe */
    {"Europe/London",        "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Dublin",        "IST-1GMT0,M10.5.0,M3.5.0/1"},
    {"Europe/Lisbon",        "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Berlin",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris",         "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Rome",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Warsaw",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Budapest",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Amsterdam",     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels",      "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Stockholm",     "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Oslo",          "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen",    "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vienna",        "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Helsinki",      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Riga",          "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Tallinn",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Vilnius",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Bucharest",     "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Sofia",         "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Athens",        "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Nicosia",       "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Kyiv",          "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Chisinau",      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Minsk",         "FET-3"},
    {"Europe/Moscow",        "MSK-3"},
    {"Europe/Istanbul",      "TRT-3"},
    {"Europe/Kaliningrad",   "EET-2"},

    /* Americas */
    {"America/New_York",     "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago",      "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver",       "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles",  "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Phoenix",      "MST7"},
    {"America/Anchorage",    "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Honolulu",     "HST10"},
    {"America/Sao_Paulo",    "BRT3BRST,M10.3.0/0,M2.3.0/0"},
    {"America/Toronto",      "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver",    "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City",  "CST6CDT,M4.1.0,M10.5.0"},
    {"America/Buenos_Aires", "ART3"},

    /* Asia/Pacific */
    {"Asia/Tokyo",           "JST-9"},
    {"Asia/Seoul",           "KST-9"},
    {"Asia/Shanghai",        "CST-8"},
    {"Asia/Singapore",       "SGT-8"},
    {"Asia/Taipei",          "CST-8"},
    {"Asia/Hong_Kong",       "HKT-8"},
    {"Asia/Jakarta",         "WIB-7"},
    {"Asia/Bangkok",         "ICT-7"},
    {"Asia/Kolkata",         "IST-5:30"},
    {"Asia/Karachi",         "PKT-5"},
    {"Asia/Dubai",           "GST-4"},
    {"Asia/Tehran",          "IRST-3:30IRDT,80/0,264/0"},
    {"Asia/Jerusalem",       "IST-2IDT,M3.4.4/26,M10.5.0"},
    {"Asia/Riyadh",          "AST-3"},
    {"Asia/Vladivostok",     "VLAT-10"},
    {"Asia/Yekaterinburg",   "YEKT-5"},
    {"Asia/Novosibirsk",     "NOVT-7"},
    {"Asia/Almaty",          "ALMT-6"},

    /* Australia */
    {"Australia/Sydney",     "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne",  "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth",      "AWST-8"},
    {"Australia/Adelaide",   "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Darwin",     "ACST-9:30"},
    {"Australia/Brisbane",   "AEST-10"},

    /* Africa */
    {"Africa/Cairo",         "EET-2"},
    {"Africa/Johannesburg",  "SAST-2"},
    {"Africa/Lagos",         "WAT-1"},
    {"Africa/Nairobi",       "EAT-3"},
};

#define TZ_TABLE_SIZE (int)(sizeof(s_table) / sizeof(s_table[0]))

const char *tz_find_posix(const char *iana)
{
    if (!iana) return NULL;
    for (int i = 0; i < TZ_TABLE_SIZE; i++) {
        if (strcmp(s_table[i].iana, iana) == 0) {
            return s_table[i].posix;
        }
    }
    return NULL;
}

int tz_table_count(void)
{
    return TZ_TABLE_SIZE;
}

const char *tz_table_name(int idx)
{
    if (idx < 0 || idx >= TZ_TABLE_SIZE) return NULL;
    return s_table[idx].iana;
}
