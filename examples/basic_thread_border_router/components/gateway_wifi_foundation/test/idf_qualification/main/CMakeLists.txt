idf_component_register(
    SRCS "qualification_main.c" "fixture_guard.c" "fixture_allowlist.c"
         "fixture_platform.c" "result_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES esp_hw_support spi_flash esp_partition app_update)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=c11 -Wall -Wextra -Werror)
set_source_files_properties("fixture_guard.c" "fixture_allowlist.c" "result_protocol.c"
    PROPERTIES COMPILE_OPTIONS "-pedantic")
