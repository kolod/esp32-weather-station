#include "unity.h"
#include "i18n.h"
#include <string.h>

TEST_CASE("accept_language_pick: simple de", "[i18n]")
{
    char out[3];
    accept_language_pick("de", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("de", out);
}

TEST_CASE("accept_language_pick: fr subtag fr-FR", "[i18n]")
{
    char out[3];
    accept_language_pick("fr-FR,fr;q=0.9,en;q=0.8", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("fr", out);
}

TEST_CASE("accept_language_pick: unknown lang falls back to en", "[i18n]")
{
    char out[3];
    accept_language_pick("zh-TW", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("en", out);
}

TEST_CASE("accept_language_pick: uk preferred over en via q-value", "[i18n]")
{
    char out[3];
    accept_language_pick("uk,en;q=0.5", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("uk", out);
}

TEST_CASE("accept_language_pick: empty header returns en", "[i18n]")
{
    char out[3];
    accept_language_pick("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("en", out);
}

TEST_CASE("accept_language_pick: NULL header returns en", "[i18n]")
{
    char out[3];
    accept_language_pick(NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("en", out);
}

TEST_CASE("accept_language_pick: de-AT primary subtag match", "[i18n]")
{
    char out[3];
    accept_language_pick("de-AT,en;q=0.8", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("de", out);
}
