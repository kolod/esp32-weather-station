#pragma once

/**
 * @brief Look up the POSIX TZ string for an IANA timezone name.
 * @param iana  e.g. "Europe/Kyiv", "UTC", "America/New_York"
 * @return POSIX TZ string, or NULL if not in the compiled-in table.
 */
const char *tz_find_posix(const char *iana);

/** @brief Return the number of entries in the timezone table. */
int tz_table_count(void);

/** @brief Return the IANA name of the n-th entry (0-based). Returns NULL on OOB. */
const char *tz_table_name(int idx);
