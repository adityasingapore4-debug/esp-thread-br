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
         "${UN_FIRMWARE_ROOT}/components/thread_runtime_resolution/thread_runtime_resolution.c"
         "${UN_FIRMWARE_ROOT}/components/mesh_session_control/mesh_session_control.c"
         "${UN_FIRMWARE_ROOT}/components/response_transfer_core/response_transfer_receive_core.c"
         "${UN_FIRMWARE_ROOT}/components/device_event_delivery/device_event_delivery.c"
         "${UN_FIRMWARE_ROOT}/components/device_event_protocol/device_event_protocol.c"
         "${UN_FIRMWARE_ROOT}/components/synchronization_protocol/synchronization_protocol.c"
    INCLUDE_DIRS "include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/device_event_delivery/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/device_event_protocol/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/runtime_state_update/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/synchronization_protocol/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/response_transfer_core/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/thread_runtime_resolution/include"
                 "/home/aditya/esp/projects/universal-node-thread-fw/firmware/components/mesh_session_control/include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES freertos esp_common log utils node_identity control_frame mesh_auth mesh_session security_root provisioning_v2 transport addressing coap_transport openthread esp_wifi esp_http_server esp_ot_br_server
)
