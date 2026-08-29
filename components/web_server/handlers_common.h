#pragma once
#include "esp_http_server.h"

/** @brief Register GET /api/timezones on the given server handle. */
void register_timezones_handler(httpd_handle_t server);

/**
 * @brief Pick the page language for a request from its Accept-Language header.
 *
 * Reads up to 255 characters of the header (longer values are truncated,
 * never an error) and runs accept_language_pick(). Any read failure or a
 * missing header selects "en".
 *
 * @param req  The request.
 * @param lang Buffer for the 2-char language code + NUL (>= 3 bytes).
 */
void pick_request_lang(httpd_req_t *req, char lang[3]);

/**
 * @brief Send an embedded HTML asset with its <html lang="…"> value patched.
 *
 * Sends the asset as three chunks (prefix / 2-char code / suffix) so no RAM
 * copy is made. Sets Content-Type, Content-Language and Cache-Control:
 * no-cache; the caller may set additional headers beforehand. If the asset
 * has no lang="…" attribute it is sent unmodified.
 *
 * Contract: specs/004-fix-web-i18n/contracts/i18n-http.md §1.
 */
esp_err_t send_html_lang_patched(httpd_req_t *req, const uint8_t *start,
                                 const uint8_t *end, const char lang[3]);

/**
 * @brief Register GET /i18n/<code>.json (language packs, wildcard route)
 *        and GET /i18n.js (shared applier script) on the given server handle.
 *
 * Requires the server to use httpd_uri_match_wildcard.
 * Contract: specs/004-fix-web-i18n/contracts/i18n-http.md §2–§3.
 */
void register_i18n_handlers(httpd_handle_t server);
