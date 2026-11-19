# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# This ESP32-S3 Gateway board has no physical relay outputs.  The Universal
# Node board profile remains capability-driven: another Gateway SKU may set a
# nonzero BOARD_RELAY_COUNT without any Gateway-specific firmware branch.
add_compile_definitions(BOARD_RELAY_COUNT=0)

include("${CMAKE_CURRENT_LIST_DIR}/cmake/universal_node_firmware.cmake")

set(EXTRA_COMPONENT_DIRS
    "${UN_FIRMWARE_ROOT}/components/actuator_driver"
    "${UN_FIRMWARE_ROOT}/components/actuator_manager"
    "${UN_FIRMWARE_ROOT}/components/addressing"
    "${UN_FIRMWARE_ROOT}/components/attribute_publication_manager"
    "${UN_FIRMWARE_ROOT}/components/attribute_notification_backend"
    "${UN_FIRMWARE_ROOT}/components/auth_domains"
    "${UN_FIRMWARE_ROOT}/components/authorization"
    "${UN_FIRMWARE_ROOT}/components/automation_coap"
    "${UN_FIRMWARE_ROOT}/components/automation_registry"
    "${UN_FIRMWARE_ROOT}/components/automation_storage"
    "${UN_FIRMWARE_ROOT}/components/ble_bridge"
    "${UN_FIRMWARE_ROOT}/components/ble_pairing_policy"
    "${UN_FIRMWARE_ROOT}/components/board"
    "${UN_FIRMWARE_ROOT}/components/canonical_relay_event_router"
    "${UN_FIRMWARE_ROOT}/components/coap_transport"
    "${UN_FIRMWARE_ROOT}/components/command_coalescer"
    "${UN_FIRMWARE_ROOT}/components/command_delivery"
    "${UN_FIRMWARE_ROOT}/components/command_router"
    "${UN_FIRMWARE_ROOT}/components/control_frame"
    "${UN_FIRMWARE_ROOT}/components/control_frame_response"
    "${UN_FIRMWARE_ROOT}/components/control_response_consumer"
    "${UN_FIRMWARE_ROOT}/components/debug_utils"
    "${UN_FIRMWARE_ROOT}/components/device_attribute"
    "${UN_FIRMWARE_ROOT}/components/device_capability"
    "${UN_FIRMWARE_ROOT}/components/device_core"
    "${UN_FIRMWARE_ROOT}/components/device_event_bus"
    "${UN_FIRMWARE_ROOT}/components/device_event_delivery"
    "${UN_FIRMWARE_ROOT}/components/device_event_distributor"
    "${UN_FIRMWARE_ROOT}/components/device_event_protocol"
    "${UN_FIRMWARE_ROOT}/components/device_descriptor"
    "${UN_FIRMWARE_ROOT}/components/device_type"
    "${UN_FIRMWARE_ROOT}/components/device_metadata"
    "${UN_FIRMWARE_ROOT}/components/hw_hal"
    "${UN_FIRMWARE_ROOT}/components/join_existing"
    "${UN_FIRMWARE_ROOT}/components/manufacturing_identity"
    "${UN_FIRMWARE_ROOT}/components/mesh_auth"
    "${UN_FIRMWARE_ROOT}/components/mesh_command"
    "${UN_FIRMWARE_ROOT}/components/mesh_control_plane"
    "${UN_FIRMWARE_ROOT}/components/mesh_ingress"
    "${UN_FIRMWARE_ROOT}/components/mesh_session"
    "${UN_FIRMWARE_ROOT}/components/mesh_session_control"
    "${UN_FIRMWARE_ROOT}/components/mesh_topology"
    "${UN_FIRMWARE_ROOT}/components/node_identity"
    "${UN_FIRMWARE_ROOT}/components/node_registry"
    "${UN_FIRMWARE_ROOT}/components/notification_fragmentation"
    "${UN_FIRMWARE_ROOT}/components/operation_context"
    "${UN_FIRMWARE_ROOT}/components/p4_access_session"
    "${UN_FIRMWARE_ROOT}/components/p4_server_identity"
    "${UN_FIRMWARE_ROOT}/components/physical_presence"
    "${UN_FIRMWARE_ROOT}/components/provisioning_v2"
    "${UN_FIRMWARE_ROOT}/components/registry_revision_security"
    "${UN_FIRMWARE_ROOT}/components/relay_control"
    "${UN_FIRMWARE_ROOT}/components/request_replay"
    "${UN_FIRMWARE_ROOT}/components/resource_bindings"
    "${UN_FIRMWARE_ROOT}/components/resource_query"
    "${UN_FIRMWARE_ROOT}/components/response_profile"
    "${UN_FIRMWARE_ROOT}/components/response_transfer"
    "${UN_FIRMWARE_ROOT}/components/response_transfer_core"
    "${UN_FIRMWARE_ROOT}/components/reverse_route_registry"
    "${UN_FIRMWARE_ROOT}/components/rts"
    "${UN_FIRMWARE_ROOT}/components/runtime_state_update"
    "${UN_FIRMWARE_ROOT}/components/security_root"
    "${UN_FIRMWARE_ROOT}/components/snapshot_publication_backend"
    "${UN_FIRMWARE_ROOT}/components/synchronization_protocol"
    "${UN_FIRMWARE_ROOT}/components/synchronization_router"
    "${UN_FIRMWARE_ROOT}/components/system_state"
    "${UN_FIRMWARE_ROOT}/components/thread_commissioning"
    "${UN_FIRMWARE_ROOT}/components/thread_device_event_adapter"
    "${UN_FIRMWARE_ROOT}/components/thread_runtime"
    "${UN_FIRMWARE_ROOT}/components/thread_runtime_resolution"
    "${UN_FIRMWARE_ROOT}/components/thread_sync_adapter"
    "${UN_FIRMWARE_ROOT}/components/thread_transport"
    "${UN_FIRMWARE_ROOT}/components/transport"
    "${UN_FIRMWARE_ROOT}/components/transport_backend_selector"
    "${UN_FIRMWARE_ROOT}/components/unrr"
    "${UN_FIRMWARE_ROOT}/components/unrs"
    "${UN_FIRMWARE_ROOT}/components/universal_node_cli"
    "${UN_FIRMWARE_ROOT}/components/utils")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp_ot_br)

# ESP-IDF's Border Router OpenThread archive references the managed mDNS
# component.  Keep that dependency on the OpenThread owner so static-link
# ordering remains correct even while product HTTP startup is gated off.
cmake_policy(SET CMP0079 NEW)
idf_component_get_property(un_openthread openthread COMPONENT_LIB)
idf_component_get_property(un_mdns espressif__mdns COMPONENT_LIB)
target_link_libraries(${un_openthread} PRIVATE ${un_mdns})

# Reject a shadow security owner from a different worktree.
foreach(un_owner manufacturing_identity p4_server_identity node_identity)
    idf_component_get_property(un_owner_dir ${un_owner} COMPONENT_DIR)
    get_filename_component(un_owner_dir "${un_owner_dir}" REALPATH)
    if(NOT un_owner_dir STREQUAL "${UN_FIRMWARE_ROOT}/components/${un_owner}")
        message(FATAL_ERROR "Mixed firmware owner ${un_owner}: ${un_owner_dir}")
    endif()
endforeach()
