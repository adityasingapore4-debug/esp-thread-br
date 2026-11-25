# This component has no code or startup hook unless explicitly selected.
if(CONFIG_UN_GATEWAY_DEV_WIFI_SYNTHETIC_SELFTEST)
    set(foundation "${CMAKE_CURRENT_LIST_DIR}/../gateway_wifi_foundation")
    idf_component_register(SRCS
        "${foundation}/test/idf_runtime/main/runtime_owner.c"
        "${foundation}/wifi_codec.c"
        "${foundation}/wifi_transaction.c"
        "${foundation}/wifi_store.c"
        "${foundation}/wifi_recovery.c"
        INCLUDE_DIRS "${foundation}/test/idf_runtime/main"
        PRIV_INCLUDE_DIRS "${foundation}"
        REQUIRES esp_timer freertos)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ADR091_SYNTHETIC_RUNTIME_SELFTEST=1)
else()
    idf_component_register(REQUIRES esp_timer freertos)
endif()
