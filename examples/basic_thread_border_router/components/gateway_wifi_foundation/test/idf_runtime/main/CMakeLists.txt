set(foundation "${CMAKE_CURRENT_LIST_DIR}/../../..")
idf_component_register(SRCS "main.c" "runtime_owner.c"
    "${foundation}/wifi_codec.c" "${foundation}/wifi_transaction.c"
    "${foundation}/wifi_store.c" "${foundation}/wifi_recovery.c"
    INCLUDE_DIRS "." "${foundation}"
    REQUIRES esp_timer freertos)
target_compile_definitions(${COMPONENT_LIB} PRIVATE ADR091_SYNTHETIC_RUNTIME_SELFTEST=1)
