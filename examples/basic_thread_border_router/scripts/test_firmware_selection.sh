#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
selection_project="$PWD"
selection_output="$(mktemp -d "${TMPDIR:-/tmp}/firmware-selection.XXXXXX")"
selection_helper="$selection_project/cmake/universal_node_firmware.cmake"
selection_default="$(realpath ../../../../universal-node-thread-fw/firmware)"
selection_candidate="$(realpath ../../../../un-fw-adr090/firmware)"
cmake -P "$selection_helper" > "$selection_output/default.log" 2>&1
rg -Fq "Universal Node firmware: $selection_default @" "$selection_output/default.log"
rg -Fq 'Inert identity evidence qualification: OFF' "$selection_output/default.log"
cmake -C "$selection_project/config/adr090-identity-evidence.cmake" -P "$selection_helper" > "$selection_output/candidate.log" 2>&1
rg -Fq "Universal Node firmware: $selection_candidate @" "$selection_output/candidate.log"
rg -Fq 'Inert identity evidence qualification: ON' "$selection_output/candidate.log"
if cmake -DUN_FIRMWARE_ROOT="$selection_output/missing" -P "$selection_helper" > "$selection_output/missing.log" 2>&1; then exit 1; fi
rg -q 'not a complete firmware tree' "$selection_output/missing.log"
if cmake -DUN_FIRMWARE_ROOT="$selection_candidate" -DUN_FIRMWARE_EXPECTED_REVISION=wrong -P "$selection_helper" > "$selection_output/revision.log" 2>&1; then exit 1; fi
rg -q 'Firmware revision mismatch' "$selection_output/revision.log"
if cmake -DUN_FIRMWARE_ROOT="$selection_candidate" -DUN_FIRMWARE_BOUND_ROOT="$selection_default" -P "$selection_helper" > "$selection_output/mixed.log" 2>&1; then exit 1; fi
rg -q 'fresh build directory' "$selection_output/mixed.log"
if cmake -DUN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY=ON -P "$selection_helper" > "$selection_output/legacy.log" 2>&1; then exit 1; fi
rg -q 'requires the ADR-090 CIB storage API' "$selection_output/legacy.log"
echo 'Firmware selection: 6 scenarios PASS (default, pinned candidate, missing root, wrong revision, mixed tree, missing API)'
echo "Evidence: $selection_output"
