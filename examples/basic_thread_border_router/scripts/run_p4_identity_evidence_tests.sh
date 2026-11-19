#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [[ $# != 2 || "$1" != --firmware-root ]]; then
    echo "Usage: bash $0 --firmware-root /absolute/ADR090/firmware" >&2
    exit 2
fi
identity_fw="$(realpath "$2")"
identity_linked_fw="$(realpath ../../../../universal-node-thread-fw/firmware)"
if [[ ! -f "$identity_fw/components/manufacturing_identity/include/manufacturing_cib_storage.h" ]] ||
   ! rg -q 'p4_server_identity_build_proof_checked' "$identity_fw/components/p4_server_identity/include/p4_server_identity.h"; then
    echo "BLOCKED: explicitly selected firmware lacks ADR-090/M3 evidence APIs: $identity_fw" >&2
    exit 2
fi
echo "HOST-ONLY candidate headers/sources: $identity_fw"
echo "Gateway default dependency (explicit CMake overrides are separate): $identity_linked_fw"
identity_output="$(mktemp -d "${TMPDIR:-/tmp}/gateway-identity-tests.XXXXXX")"
identity_includes=(-Icomponents/universal_node_gateway/private_include
    -I"$identity_fw/tests/stubs" -I"$identity_fw/tests/support"
    -I"$identity_fw/components/manufacturing_identity/include"
    -I"$identity_fw/components/manufacturing_identity/third_party/monocypher"
    -I"$identity_fw/components/p4_server_identity/include"
    -I"$identity_fw/components/p4_server_identity/private_include"
    -I"$identity_fw/components/provisioning_v2/include"
    -I"$identity_fw/components/security_root/include"
    -I"$identity_fw/components/node_identity/include"
    -I"$identity_fw/components/utils/include" -I"$identity_fw/components/auth_domains/include")
identity_source=components/universal_node_gateway/universal_node_gateway_p4_identity.c
identity_flags=(-std=gnu11 -Wall -Wextra -Werror -O2 -g)
gcc "${identity_flags[@]}" "${identity_includes[@]}" -fstack-usage -c "$identity_source" -o "$identity_output/producer.o"
identity_wraps=(node_identity_get node_identity_principal_eui64_get
    manufacturing_identity_get_attestation manufacturing_identity_cib_load
    p4_server_identity_build_proof_checked p4_server_identity_public)
identity_link=()
for symbol in "${identity_wraps[@]}"; do identity_link+=("-Wl,--wrap=$symbol"); done
gcc "${identity_flags[@]}" "${identity_includes[@]}" -DP4_HOST_TEST -DCONFIG_UN_MANUFACTURING_IDENTITY_DEVELOPMENT=1 \
    components/universal_node_gateway/test/gateway_p4_identity_test.c "$identity_output/producer.o" \
    "$identity_fw/tests/support/test_nvs.c" "$identity_fw/tests/support/test_esp_random.c" \
    "$identity_fw/components/manufacturing_identity/manufacturing_identity.c" \
    "$identity_fw/components/manufacturing_identity/manufacturing_identity_codecs.c" \
    "$identity_fw/components/manufacturing_identity/manufacturing_cib_storage.c" \
    "$identity_fw/components/manufacturing_identity/third_party/monocypher/monocypher.c" \
    "$identity_fw/components/manufacturing_identity/third_party/monocypher/monocypher-ed25519.c" \
    "$identity_fw/components/p4_server_identity/p4_server_identity.c" \
    "$identity_fw/components/p4_server_identity/p4_server_proof.c" \
    "$identity_fw/components/p4_server_identity/p4_crypto.c" \
    "$identity_fw/components/node_identity/node_identity.c" "$identity_fw/components/utils/crc16.c" \
    "${identity_link[@]}" -lcrypto -o "$identity_output/identity-test"
"$identity_output/identity-test"
nm -u "$identity_output/producer.o" > "$identity_output/imports.txt"
if rg -i '(malloc|calloc|nvs_|token|session|dispatch|install|_sign)' "$identity_output/imports.txt"; then
    echo "Unexpected mutation/authorization/allocation import" >&2; exit 1
fi
echo "No allocation, storage-mutation, signing, dispatch or session/token imports."
echo "Host evidence artifacts: $identity_output"
