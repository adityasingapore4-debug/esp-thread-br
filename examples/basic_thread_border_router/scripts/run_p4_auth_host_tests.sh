#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
p4_fw="${UN_FIRMWARE_ROOT:-/home/aditya/esp/projects/universal-node-thread-fw/firmware}"
p4_output="${P4_TEST_OUTPUT:-${TMPDIR:-/tmp}/gateway-p4-auth-adapter-test}"
p4_args=(-std=gnu11 -Wall -Wextra -Werror -DP4_ACCESS_HOST_TEST -DSCRAM_VERIFIER_HOST_TEST -include "$p4_fw/tests/stubs/esp_error_check.h" -I"$p4_fw/tests/stubs" -I"$p4_fw/tests" -I"$p4_fw/components/utils/include" -Icomponents/universal_node_gateway/private_include)
if [[ "${P4_HTTPS_RUNTIME_MOCK:-0}" == 1 ]]; then
    p4_args=(-DP4_HTTPS_HOST_TEST -Icomponents/universal_node_gateway/test/runtime_stubs "${p4_args[@]}")
    p4_args+=(-I"$p4_fw/components/p4_server_identity/include")
fi
p4_args+=(-I"$p4_fw/components/manufacturing_identity/include")
p4_args+=(-I"$p4_fw/components/p4_access_session/include")
p4_args+=(-I"$p4_fw/components/p4_access_session/private_include")
p4_args+=(-I"$p4_fw/tests/support")
p4_args+=(-I"$p4_fw/components/auth_domains/include")
p4_args+=(-I"$p4_fw/components/node_identity/include")
p4_args+=(-I"$p4_fw/components/physical_presence/include")
p4_args+=(-I"$p4_fw/components/provisioning_v2/include")
p4_args+=(-I"$p4_fw/components/security_root/include")
p4_args+=(-I"$p4_fw/components/security_root/private_include")
p4_args+=(-I"$p4_fw/components/system_state/include")
if [[ "${P4_TEST_SANITIZE:-0}" == 1 ]]; then
    p4_args+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi
p4_sources=("${P4_TEST_SOURCE:-components/universal_node_gateway/test/gateway_p4_auth_adapter_test.c}" components/universal_node_gateway/universal_node_gateway_p4_auth.c components/universal_node_gateway/universal_node_gateway_p4_http.c)
p4_sources+=("$p4_fw/tests/support/test_nvs.c")
p4_sources+=("$p4_fw/tests/support/test_security_root_crypto.c")
p4_sources+=("$p4_fw/tests/support/test_esp_random.c")
p4_sources+=("$p4_fw/tests/support/scram_test_client.c")
p4_sources+=("$p4_fw/components/p4_access_session/p4_access_session.c")
p4_sources+=("$p4_fw/components/auth_domains/local_access_session.c")
p4_sources+=("$p4_fw/components/auth_domains/adr073_auth_proof_service.c")
p4_sources+=("$p4_fw/components/auth_domains/adr073_auth_start_service.c")
p4_sources+=("$p4_fw/components/auth_domains/adr073_auth_transaction.c")
p4_sources+=("$p4_fw/components/auth_domains/adr073_auth_payload_codec.c")
p4_sources+=("$p4_fw/components/auth_domains/adr073_operation_descriptor.c")
p4_sources+=("$p4_fw/components/auth_domains/domain_scram_auth.c")
p4_sources+=("$p4_fw/components/auth_domains/auth_domains.c")
p4_sources+=("$p4_fw/components/auth_domains/scram_server.c")
p4_sources+=("$p4_fw/components/auth_domains/scram_codec.c")
p4_sources+=("$p4_fw/components/auth_domains/scram_verifier.c")
p4_sources+=("$p4_fw/components/node_identity/node_identity.c")
p4_sources+=("$p4_fw/components/utils/crc16.c")
p4_sources+=("$p4_fw/components/physical_presence/physical_presence.c")
p4_sources+=("$p4_fw/components/provisioning_v2/provisioning_v2.c")
p4_sources+=("$p4_fw/components/security_root/security_root.c")
p4_sources+=("$p4_fw/components/system_state/provisioning_flag.c")
gcc "${p4_args[@]}" "${p4_sources[@]}" -lssl -lcrypto -o "$p4_output"
if [[ "${P4_TEST_BUILD_ONLY:-0}" != 1 ]]; then "$p4_output"; fi
