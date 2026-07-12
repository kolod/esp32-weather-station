idf_component_register(
    SRCS "test_rtc_time.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES unity rtc_time app_ctx
)
