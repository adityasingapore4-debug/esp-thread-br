idf_component_register(SRCS "esp_ot_br.c"
                       INCLUDE_DIRS "."
                       REQUIRES universal_node_gateway universal_node_cli provisioning_v2 p4_server_identity esp_ot_cli_extension mdns
                       PRIV_REQUIRES spiffs
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
                       )
