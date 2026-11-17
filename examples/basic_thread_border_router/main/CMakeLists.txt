idf_component_register(SRCS "esp_ot_br.c" "dev_heap_diagnostics.c"
                       INCLUDE_DIRS "."
                       REQUIRES universal_node_gateway universal_node_cli provisioning_v2 p4_server_identity esp_ot_cli_extension mdns
                       PRIV_REQUIRES spiffs heap esp_wifi esp_rom
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
                       )

if(CONFIG_UN_GATEWAY_DEV_P4_TLS_SMOKE)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_wifi_init")
endif()

if(CONFIG_UN_GATEWAY_DEV_PSRAM)
    idf_component_optional_requires(PRIVATE esp_psram esp_https_server)
endif()
