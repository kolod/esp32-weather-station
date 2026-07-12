idf_component_register(
    SRCS "rtc_time.c"
    INCLUDE_DIRS "."
    REQUIRES app_ctx
    PRIV_REQUIRES esp_event esp_rom
)

# Firmware build time (UTC, Unix seconds) — lower plausibility bound for restored time (FR-004)
string(TIMESTAMP RTC_TIME_BUILD_EPOCH "%s" UTC)
target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH=${RTC_TIME_BUILD_EPOCH}LL)
