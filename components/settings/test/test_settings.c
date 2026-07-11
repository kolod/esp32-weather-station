#include "unity.h"
#include "tz_table.h"
#include "wifi_mgr.h"
#include <string.h>

/* ── Timezone table tests ── */

TEST_CASE("tz_find_posix: UTC returns UTC0", "[tz_table]")
{
    const char *p = tz_find_posix("UTC");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_STRING("UTC0", p);
}

TEST_CASE("tz_find_posix: Europe/Kyiv returns EET DST string", "[tz_table]")
{
    const char *p = tz_find_posix("Europe/Kyiv");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "EET") != NULL);
    TEST_ASSERT_TRUE(strstr(p, "EEST") != NULL);
}

TEST_CASE("tz_find_posix: unknown zone returns NULL", "[tz_table]")
{
    TEST_ASSERT_NULL(tz_find_posix("Mars/Olympus"));
    TEST_ASSERT_NULL(tz_find_posix(""));
    TEST_ASSERT_NULL(tz_find_posix(NULL));
}

TEST_CASE("tz_find_posix: Europe/Berlin CET", "[tz_table]")
{
    const char *p = tz_find_posix("Europe/Berlin");
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(strstr(p, "CET") != NULL);
}

TEST_CASE("tz_table_count: non-zero", "[tz_table]")
{
    TEST_ASSERT_GREATER_THAN(40, tz_table_count());
}

/* ── MAC suffix test ── */

TEST_CASE("mac_to_suffix: produces 4 lowercase hex chars", "[wifi_mgr]")
{
    /* mac_to_suffix() uses esp_efuse_mac_get_default() on real hardware;
       test that the output length and character set are correct.            */
    char buf[5] = "";
    mac_to_suffix(buf);
    TEST_ASSERT_EQUAL(4, strlen(buf));
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE((buf[i] >= '0' && buf[i] <= '9') ||
                         (buf[i] >= 'a' && buf[i] <= 'f'));
    }
}
