# One source selection for every Universal Node security/firmware component.
# Default layout is unchanged; explicit candidates require a separate build dir.
get_filename_component(_un_default_firmware
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../universal-node-thread-fw/firmware" ABSOLUTE)
set(UN_FIRMWARE_ROOT "${_un_default_firmware}" CACHE PATH "Universal Node firmware worktree")
get_filename_component(UN_FIRMWARE_ROOT "${UN_FIRMWARE_ROOT}" REALPATH
    BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT EXISTS "${UN_FIRMWARE_ROOT}/components/manufacturing_identity/CMakeLists.txt" OR
   NOT EXISTS "${UN_FIRMWARE_ROOT}/components/p4_server_identity/CMakeLists.txt" OR
   NOT EXISTS "${UN_FIRMWARE_ROOT}/components/node_identity/CMakeLists.txt")
    message(FATAL_ERROR "UN_FIRMWARE_ROOT is not a complete firmware tree: ${UN_FIRMWARE_ROOT}")
endif()
if(DEFINED UN_FIRMWARE_BOUND_ROOT AND NOT UN_FIRMWARE_BOUND_ROOT STREQUAL UN_FIRMWARE_ROOT)
    message(FATAL_ERROR "Firmware worktree changed. Use a fresh build directory; do not mix component trees.")
endif()
set(UN_FIRMWARE_BOUND_ROOT "${UN_FIRMWARE_ROOT}" CACHE INTERNAL "Bound firmware source tree" FORCE)
set(UN_FIRMWARE_EXPECTED_REVISION "" CACHE STRING "Optional exact firmware HEAD required by candidate builds")
execute_process(COMMAND git -C "${UN_FIRMWARE_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE UN_FIRMWARE_RESOLVED_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _un_git_result)
if(NOT _un_git_result EQUAL 0)
    message(FATAL_ERROR "Cannot identify selected firmware revision")
endif()
if(UN_FIRMWARE_EXPECTED_REVISION AND
   NOT UN_FIRMWARE_EXPECTED_REVISION STREQUAL UN_FIRMWARE_RESOLVED_REVISION)
    message(FATAL_ERROR "Firmware revision mismatch: expected ${UN_FIRMWARE_EXPECTED_REVISION}; found ${UN_FIRMWARE_RESOLVED_REVISION}")
endif()
option(UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY "Compile/link inert identity producer for development measurement only" OFF)
if(UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY)
    if(NOT EXISTS "${UN_FIRMWARE_ROOT}/components/manufacturing_identity/include/manufacturing_cib_storage.h")
        message(FATAL_ERROR "Identity qualification requires the ADR-090 CIB storage API; no fallback")
    endif()
    file(READ "${UN_FIRMWARE_ROOT}/components/p4_server_identity/include/p4_server_identity.h" _un_p4_header)
    string(FIND "${_un_p4_header}" "p4_server_identity_build_proof_checked" _un_checked)
    if(_un_checked EQUAL -1)
        message(FATAL_ERROR "Identity qualification requires the ADR-090 checked P4 producer")
    endif()
endif()
message(STATUS "Universal Node firmware: ${UN_FIRMWARE_ROOT} @ ${UN_FIRMWARE_RESOLVED_REVISION}")
message(STATUS "Inert identity evidence qualification: ${UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY}")
