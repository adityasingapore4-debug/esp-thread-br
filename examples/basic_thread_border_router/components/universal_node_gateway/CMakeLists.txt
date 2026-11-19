idf_component_register(
    SRCS "universal_node_gateway.c"
         "universal_node_gateway_p4_auth.c"
         "universal_node_gateway_p4_runtime.c"
         "universal_node_gateway_p4_http.c"
         "universal_node_gateway_p4_https.c"
         "universal_node_gateway_diagnostic.c"
         "universal_node_gateway_envelope.c"
         "universal_node_gateway_http_transport.c"
         "universal_node_gateway_pending.c"
         "universal_node_gateway_thread_rx_adapter.c"
         "universal_node_gateway_thread_comm_path.c"
         "universal_node_gateway_thread_tx_adapter.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_https_server esp-tls esp_timer p4_access_session p4_server_identity freertos esp_common log utils node_identity manufacturing_identity control_frame mesh_auth mesh_session security_root provisioning_v2 transport transport_backend_selector addressing coap_transport openthread esp_wifi esp_http_server esp_ot_br_server device_event_bus device_event_delivery device_event_distributor device_event_protocol device_capability device_metadata node_registry rts authorization command_router device_core resource_query response_profile response_transfer response_transfer_core control_response_consumer command_delivery mesh_session_control synchronization_protocol thread_runtime_resolution thread_sync_adapter thread_device_event_adapter join_existing thread_commissioning ble_bridge ble_pairing_policy
)

# Retain the inert identity-configuration seam for source/size qualification.
# This does not call it or start a listener.
target_link_libraries(${COMPONENT_LIB} PRIVATE "-u p4_https_identity_prepare")

# Measure inert carriage as well; no listener startup is installed.
target_link_libraries(${COMPONENT_LIB} PRIVATE "-u gateway_p4_https_start" "-u gateway_p4_https_stop")

# Inert build qualification only. There is no runtime caller or HTTP registration.
# Pending architecture approval never enables this option or the listener.
if(UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY)
    target_sources(${COMPONENT_LIB} PRIVATE "universal_node_gateway_p4_identity.c")
    target_link_libraries(${COMPONENT_LIB} PRIVATE "-u gateway_p4_identity_evidence_build")
endif()
