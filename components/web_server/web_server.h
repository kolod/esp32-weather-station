#pragma once

/**
 * @brief Start the web server task (prio 4, 8 KB stack).
 *        The task monitors WIFI_STATE_CHANGED events and switches between
 *        the portal HTTP server (AP mode) and the HTTPS management server (STA mode).
 */
void web_server_start(void);
