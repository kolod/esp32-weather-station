#pragma once
#include <stddef.h>

/**
 * @brief Pick the best supported language from an HTTP Accept-Language header value.
 *
 * Supported languages: "en", "de", "fr", "uk".
 * Rules: primary subtag matching (e.g. "de-AT" → "de"), q-value weighting,
 *        fallback to "en" when no supported language matches.
 *
 * @param header   Value of the Accept-Language header (may be NULL or empty).
 * @param out_lang Buffer to receive the chosen 2-char language code + NUL.
 * @param len      Buffer length (must be >= 3).
 */
void accept_language_pick(const char *header, char *out_lang, size_t len);
