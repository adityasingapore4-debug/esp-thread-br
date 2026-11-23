idf_component_register(
    SRCS "compile_only.c" "../../../wifi_codec.c" "../../../wifi_transaction.c"
         "../../../wifi_store.c" "../../../wifi_store_nvs.c" "../../../wifi_recovery.c"
    INCLUDE_DIRS "../../.."
    REQUIRES nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror)
# The SDK newlib stdatomic.h uses GCC #include_next. With -Werror,
# -pedantic makes the accepted wifi_recovery.c fail against ESP-IDF v5.5.4.
# Keep -pedantic for portable core units; recovery still uses -Wall/-Wextra/
# -Werror here and retains -pedantic in the separate strict-C11 host tests.
# The real NVS adapter remains compiled without -pedantic as before.
set_source_files_properties("../../../wifi_codec.c" "../../../wifi_transaction.c"
    "../../../wifi_store.c" PROPERTIES COMPILE_OPTIONS "-pedantic")
