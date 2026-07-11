#include "app_ctx.h"

/* Global application state — guarded by app_state_mutex */
app_state_t       app_state       = {0};
SemaphoreHandle_t app_state_mutex = NULL;

/* Event base definition — must appear in exactly one translation unit */
ESP_EVENT_DEFINE_BASE(APP_EVENT);
