idf_component_register(SRCS "esp_ot_br.c"
                       INCLUDE_DIRS "."
                       REQUIRES universal_node_gateway
                       PRIV_REQUIRES spiffs
                       EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem
                       )
