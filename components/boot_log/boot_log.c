#include "boot_log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define TAG "boot_log"

/* ── Pure buffer logic (host/target unit-testable, no locking) ── */

static const char *const ALLOWED_TAGS[] = {
    /* sensor subsystem */
    "sensor", "bmp280", "ds18b20", "onewire", "i2c",
    /* display subsystem */
    "display", "lcd_panel", "spi_master",
};

/* Must fit inside BOOT_LOG_TAIL_RESERVE (incl. NUL); ASCII only. */
static const char TRUNC_MARKER[] =
    "[boot log full - further messages discarded]\n";
_Static_assert(sizeof(TRUNC_MARKER) <= BOOT_LOG_TAIL_RESERVE,
               "truncation marker exceeds reserved tail");

size_t boot_log_strip_ansi(char *s, size_t len)
{
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\033' && i + 1 < len && s[i + 1] == '[') {
            i += 2; /* skip ESC [ then parameter bytes up to the final letter */
            while (i < len && !((s[i] >= 'A' && s[i] <= 'Z') ||
                                (s[i] >= 'a' && s[i] <= 'z')))
                i++;
            continue; /* also consumes the final letter */
        }
        s[out++] = s[i];
    }
    return out;
}

bool boot_log_tag_allowed(const char *line, size_t len)
{
    /* Expected shape: "L (millis) tag: message" (ANSI already stripped). */
    size_t i = 0;
    if (len < 6) return false;
    i++;                                        /* level letter */
    if (i + 1 >= len || line[i] != ' ' || line[i + 1] != '(') return false;
    i += 2;
    while (i < len && line[i] >= '0' && line[i] <= '9') i++;
    if (i + 1 >= len || line[i] != ')' || line[i + 1] != ' ') return false;
    i += 2;
    size_t tag_start = i;
    while (i < len && line[i] != ':' && line[i] != ' ' && line[i] != '\n') i++;
    if (i >= len || line[i] != ':') return false;
    size_t tag_len = i - tag_start;
    if (tag_len == 0) return false;

    for (size_t t = 0; t < sizeof(ALLOWED_TAGS) / sizeof(ALLOWED_TAGS[0]); t++) {
        size_t alen = strlen(ALLOWED_TAGS[t]);
        if (alen > tag_len) continue;
        if (memcmp(line + tag_start, ALLOWED_TAGS[t], alen) != 0) continue;
        /* exact match, or allowlisted prefix followed by a sub-tag dot
           (e.g. "lcd_panel.st7789", "i2c.master") */
        if (alen == tag_len || line[tag_start + alen] == '.') return true;
    }
    return false;
}

bool boot_log_buf_append(boot_log_buf_t *b, const char *line, size_t len)
{
    if (b->truncated || len == 0) return false;
    if (b->len + len > BOOT_LOG_CAP - BOOT_LOG_TAIL_RESERVE) {
        /* All-or-nothing: discard the whole line, mark once (FR-004). */
        memcpy(b->buf + b->len, TRUNC_MARKER, sizeof(TRUNC_MARKER) - 1);
        b->len += sizeof(TRUNC_MARKER) - 1;
        b->truncated = true;
        return false;
    }
    memcpy(b->buf + b->len, line, len);
    b->len += len;
    return true;
}

/* ── Runtime plumbing (device only) ── */

typedef enum {
    BOOT_LOG_INACTIVE,  /* power-on default: hook not installed */
    BOOT_LOG_CAPTURING, /* boot_log_init() done: allowlisted lines appended */
    BOOT_LOG_CLOSED,    /* boot_log_close() done: buffer flushed, appends stop */
} boot_log_state_t;

static boot_log_buf_t     s_log;
static volatile boot_log_state_t s_state = BOOT_LOG_INACTIVE;
static SemaphoreHandle_t  s_mutex;
static vprintf_like_t     s_prev_vprintf;

/* Tee: always forward to the previous vprintf (serial output unchanged);
   additionally capture allowlisted lines while CAPTURING. Must never call
   ESP_LOG itself (recursion) and must never touch flash (FR-008). */
static int boot_log_vprintf(const char *fmt, va_list args)
{
    int ret;
    if (s_state == BOOT_LOG_CAPTURING) {
        char line[256];
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(line, sizeof(line), fmt, copy);
        va_end(copy);
        if (n > 0) {
            size_t len = (n < (int)sizeof(line)) ? (size_t)n : sizeof(line) - 1;
            len = boot_log_strip_ansi(line, len);
            if (boot_log_tag_allowed(line, len) &&
                xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (s_state == BOOT_LOG_CAPTURING)
                    boot_log_buf_append(&s_log, line, len);
                xSemaphoreGive(s_mutex);
            }
        }
    }
    ret = s_prev_vprintf ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);
    return ret;
}

void boot_log_init(void)
{
    if (s_state != BOOT_LOG_INACTIVE) return;
    /* Stale log gone first, so a crash mid-boot never leaves a misleading
       old file behind (FR-003). Failure (e.g. no storage) is fine. */
    remove(BOOT_LOG_FILE_PATH);

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return; /* no capture, but boot proceeds (FR-008) */

    s_state = BOOT_LOG_CAPTURING;
    s_prev_vprintf = esp_log_set_vprintf(boot_log_vprintf);
}

void boot_log_close(void)
{
    if (s_state != BOOT_LOG_CAPTURING) return;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state = BOOT_LOG_CLOSED; /* appends stop; hook keeps forwarding */
    xSemaphoreGive(s_mutex);

    if (s_log.len == 0) return;
    FILE *f = fopen(BOOT_LOG_FILE_PATH, "w");
    if (!f) {
        ESP_LOGW(TAG, "could not persist boot log (storage unavailable)");
        return;
    }
    size_t written = fwrite(s_log.buf, 1, s_log.len, f);
    fclose(f);
    if (written != s_log.len)
        ESP_LOGW(TAG, "boot log persisted partially (%u/%u bytes)",
                 (unsigned)written, (unsigned)s_log.len);
    else
        ESP_LOGI(TAG, "boot log persisted (%u bytes)", (unsigned)s_log.len);
}

const char *boot_log_get(size_t *len)
{
    size_t snapshot = s_log.len;
    /* Appends only grow the buffer and never rewrite [0, len), so a length
       snapshot plus the stable pointer is a consistent read. */
    *len = snapshot;
    return s_log.buf;
}
