#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "boot_log.h"

/* Pure buffer-logic tests (data-model.md §1 validation rules). */

static boot_log_buf_t s_buf;

static void reset_buf(void)
{
    memset(&s_buf, 0, sizeof(s_buf));
}

TEST_CASE("boot_log: append stores line verbatim", "[boot_log]")
{
    reset_buf();
    const char line[] = "I (123) sensor: Sensor mode: BMP280\n";
    TEST_ASSERT_TRUE(boot_log_buf_append(&s_buf, line, sizeof(line) - 1));
    TEST_ASSERT_EQUAL_size_t(sizeof(line) - 1, s_buf.len);
    TEST_ASSERT_EQUAL_MEMORY(line, s_buf.buf, s_buf.len);
    TEST_ASSERT_FALSE(s_buf.truncated);
}

TEST_CASE("boot_log: cap enforced, line never split, marker written once", "[boot_log]")
{
    reset_buf();
    char line[101];
    memset(line, 'x', sizeof(line) - 1);
    line[sizeof(line) - 2] = '\n';
    line[sizeof(line) - 1] = '\0';

    int stored = 0;
    while (boot_log_buf_append(&s_buf, line, 100))
        stored++;

    /* Overflow happened, marker present exactly once, no wrap. */
    TEST_ASSERT_TRUE(s_buf.truncated);
    TEST_ASSERT_LESS_OR_EQUAL(BOOT_LOG_CAP, s_buf.len);
    TEST_ASSERT_EQUAL_INT((BOOT_LOG_CAP - BOOT_LOG_TAIL_RESERVE) / 100, stored);
    TEST_ASSERT_NOT_NULL(memmem(s_buf.buf, s_buf.len, "[boot log full", 14));

    /* Everything after the cap is discarded — len frozen. */
    size_t len_after_marker = s_buf.len;
    TEST_ASSERT_FALSE(boot_log_buf_append(&s_buf, line, 100));
    TEST_ASSERT_EQUAL_size_t(len_after_marker, s_buf.len);
}

TEST_CASE("boot_log: allowlisted tags pass, others rejected", "[boot_log]")
{
    static const char *allowed[] = {
        "I (10) sensor: mode selected\n",
        "W (10) bmp280: BMP280 not found at 0x76 or 0x77\n",
        "I (10) display: panel ready\n",
        "E (10) i2c.master: probe timeout\n",
        "I (10) lcd_panel.st7789: init ok\n",
        "W (10) onewire: reset failed\n",
        "I (10) ds18b20: found\n",
        "I (10) spi_master: bus added\n",
    };
    static const char *rejected[] = {
        "I (10) wifi: connected to AP\n",         /* not allowlisted */
        "I (10) sensors: near-miss tag\n",        /* prefix but no dot */
        "I (10) main: free heap\n",
        "garbage without log shape\n",
        "I (10) : empty tag\n",
        "I sensor: missing millis\n",
    };
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++)
        TEST_ASSERT_TRUE_MESSAGE(
            boot_log_tag_allowed(allowed[i], strlen(allowed[i])), allowed[i]);
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++)
        TEST_ASSERT_FALSE_MESSAGE(
            boot_log_tag_allowed(rejected[i], strlen(rejected[i])), rejected[i]);
}

TEST_CASE("boot_log: ANSI escape sequences stripped", "[boot_log]")
{
    char colored[] = "\033[0;32mI (99) display: UI initialized\033[0m\n";
    size_t len = boot_log_strip_ansi(colored, sizeof(colored) - 1);
    static const char expect[] = "I (99) display: UI initialized\n";
    TEST_ASSERT_EQUAL_size_t(sizeof(expect) - 1, len);
    TEST_ASSERT_EQUAL_MEMORY(expect, colored, len);
    TEST_ASSERT_TRUE(boot_log_tag_allowed(colored, len));
}

TEST_CASE("boot_log: empty append is a no-op", "[boot_log]")
{
    reset_buf();
    TEST_ASSERT_FALSE(boot_log_buf_append(&s_buf, "", 0));
    TEST_ASSERT_EQUAL_size_t(0, s_buf.len);
    TEST_ASSERT_FALSE(s_buf.truncated);
}

/* FR-008 lifecycle resilience (T020). Runs in the test app where /storage is
   not mounted, so every file operation fails — init/close must still be safe.
   Uses the real singleton, so this case intentionally ends with capture
   CLOSED; the pure-buffer cases above are unaffected (own boot_log_buf_t). */
TEST_CASE("boot_log: close with empty buffer and append-after-close are no-ops",
          "[boot_log]")
{
    size_t len = 99;

    /* close before init: state is INACTIVE, must be harmless */
    boot_log_close();
    TEST_ASSERT_NOT_NULL(boot_log_get(&len));
    TEST_ASSERT_EQUAL_size_t(0, len);

    /* init with storage absent: stale-delete and hook install must not
       abort; close with an empty buffer must not create/write a file */
    boot_log_init();
    boot_log_close();
    TEST_ASSERT_NOT_NULL(boot_log_get(&len));
    TEST_ASSERT_EQUAL_size_t(0, len);

    /* capture is CLOSED: an allowlisted line is forwarded to serial but no
       longer appended to the buffer */
    ESP_LOGI("sensor", "late line after close");
    boot_log_get(&len);
    TEST_ASSERT_EQUAL_size_t(0, len);

    /* re-init after close is rejected (one capture per boot) */
    boot_log_init();
    ESP_LOGI("sensor", "line after rejected re-init");
    boot_log_get(&len);
    TEST_ASSERT_EQUAL_size_t(0, len);
}
