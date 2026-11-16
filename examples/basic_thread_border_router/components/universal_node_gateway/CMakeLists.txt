get_filename_component(UN_FIRMWARE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../universal-node-thread-fw/firmware"
    ABSOLUTE)

idf_component_register(
    SRCS "universal_node_gateway.c"
         "universal_node_gateway_diagnostic.c"
         "universal_node_gateway_envelope.c"
         "universal_node_gateway_http_transport.c"
         "universal_node_gateway_pending.c"
         "universal_node_gateway_thread_rx_adapter.c"
         "universal_node_gateway_thread_comm_path.c"
         "universal_node_gateway_thread_tx_adapter.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES freertos esp_common log utils node_identity manufacturing_identity control_frame mesh_auth mesh_session security_root provisioning_v2 transport transport_backend_selector addressing coap_transport openthread esp_wifi esp_http_server esp_ot_br_server device_event_bus device_event_delivery device_event_distributor device_event_protocol device_capability device_metadata node_registry rts authorization command_router device_core resource_query response_profile response_transfer response_transfer_core control_response_consumer command_delivery mesh_session_control synchronization_protocol thread_runtime_resolution thread_sync_adapter thread_device_event_adapter join_existing thread_commissioning ble_bridge ble_pairing_policy
)
