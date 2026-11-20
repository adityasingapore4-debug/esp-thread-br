#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
foundation=components/gateway_wifi_foundation
out_dir=$(mktemp -d /tmp/m4w2b-tests.XXXXXX)
echo "Artifacts: $out_dir"
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -I"$foundation")
if [[ ${SANITIZE:-0} == 1 ]]; then
    flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
    export ASAN_OPTIONS=detect_leaks=0
fi
for test_name in test_wifi test_closure test_review test_codec_qualification test_transaction_qualification fuzz_request test_storage; do
    cc "${flags[@]}" "$foundation/wifi_codec.c" "$foundation/wifi_transaction.c" \
        "$foundation/wifi_store.c" "$foundation/test/$test_name.c" -o "$out_dir/$test_name"
    "$out_dir/$test_name"
done
for variant in locked synthetic; do
    defines=()
    if [[ $variant == synthetic ]]; then defines+=(-DWIFI_NVS_ISOLATED_SYNTHETIC_TEST); fi
    cc "${flags[@]}" "${defines[@]}" -I"$foundation/test/nvs_stub" \
        "$foundation/wifi_store_nvs.c" "$foundation/test/test_nvs_adapter.c" \
        -o "$out_dir/nvs_$variant"
    "$out_dir/nvs_$variant"
done
python3 "$foundation/test/test_vectors.py"
