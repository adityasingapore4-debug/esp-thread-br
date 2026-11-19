# Explicit candidate initial cache. Use only with a fresh build directory.
# Relative sibling layout; no machine-specific path or global default change.
get_filename_component(_un_adr090_candidate
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../un-fw-adr090/firmware" ABSOLUTE)
set(UN_FIRMWARE_ROOT "${_un_adr090_candidate}" CACHE PATH "ADR-090 candidate firmware worktree")
set(UN_FIRMWARE_EXPECTED_REVISION "7387eec914c561f83998944c574d74fa0f03c2e9" CACHE STRING "ADR-090 M3 revision (replace only after review)")
set(UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY ON CACHE BOOL "Inert producer compile/link qualification, not activation")
