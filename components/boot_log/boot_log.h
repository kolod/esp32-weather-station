#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot diagnostic log (feature 006): tees allowlisted ESP_LOG output emitted
   during startup into a bounded RAM buffer, flushed once to /storage/boot.log
   when the boot sequence completes. Log-path failures never affect boot. */

#define BOOT_LOG_CAP          4096 /* hard cap, buffer == file size (SC-003) */
#define BOOT_LOG_TAIL_RESERVE 48   /* reserved for the truncation marker */
#define BOOT_LOG_FILE_PATH    "/storage/boot.log"

/* ── Lifecycle (called from app_main, exactly once each per boot) ── */

/* Delete the stale boot.log and start capturing. Call after LittleFS is
   mounted and before sensor/display tasks start. Safe if storage is absent. */
void boot_log_init(void);

/* Stop capturing and write the buffer to /storage/boot.log in one operation.
   Write failures are silently tolerated (FR-008). The RAM buffer is retained
   so the web handler can serve it as a fallback. */
void boot_log_close(void);

/* Current capture content (valid any time; len 0 before first message).
   Returned pointer is stable for the device lifetime; bytes [0, *len) are
   never mutated after being written. */
const char *boot_log_get(size_t *len);

/* ── Pure buffer logic, exposed for unit tests ── */

typedef struct {
    char   buf[BOOT_LOG_CAP];
    size_t len;
    bool   truncated;
} boot_log_buf_t;

/* Strip ANSI escape sequences in place; returns the new length. */
size_t boot_log_strip_ansi(char *s, size_t len);

/* True if a formatted log line ("L (millis) tag: message") carries an
   allowlisted tag (sensor/display subsystems and their drivers). */
bool boot_log_tag_allowed(const char *line, size_t len);

/* Append one line, all-or-nothing against the cap minus the reserved tail.
   On the first discard, writes the truncation marker into the tail.
   Returns true if the line was stored. */
bool boot_log_buf_append(boot_log_buf_t *b, const char *line, size_t len);

#ifdef __cplusplus
}
#endif
