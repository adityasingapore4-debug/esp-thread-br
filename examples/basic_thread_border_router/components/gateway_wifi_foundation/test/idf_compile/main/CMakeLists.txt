idf_component_register(
    SRCS "compile_only.c" "../../../wifi_codec.c" "../../../wifi_transaction.c"
         "../../../wifi_store.c" "../../../wifi_store_nvs.c"
    INCLUDE_DIRS "../../.."
    REQUIRES nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror)
# IDF newlib uses #include_next, which rejects -pedantic in the NVS adapter.
# Keep the portable core pedantic; compile the adapter against real SDK headers.
set_source_files_properties("../../../wifi_codec.c" "../../../wifi_transaction.c"
    "../../../wifi_store.c" PROPERTIES COMPILE_OPTIONS "-pedantic")
