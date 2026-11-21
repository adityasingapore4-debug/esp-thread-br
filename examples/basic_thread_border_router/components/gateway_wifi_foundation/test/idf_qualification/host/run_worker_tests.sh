#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../../.."
foundation=components/gateway_wifi_foundation
host="$foundation/test/idf_qualification/host"
out=$(mktemp -d /tmp/adr091-worker.XXXXXX)
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -pthread -DWIFI_RECOVERY_HOST_TEST -I"$foundation" -I"$host")
if [[ ${SANITIZE:-0} == 1 ]]; then
  flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie)
  export ASAN_OPTIONS=detect_leaks=0
fi
cc "${flags[@]}" "$foundation/wifi_codec.c" "$foundation/wifi_transaction.c" \
  "$foundation/wifi_store.c" "$foundation/wifi_recovery.c" \
  "$host/worker_scheduler.c" "$host/test_worker_integration.c" -o "$out/test_worker"
"$out/test_worker"
echo "Artifacts: $out"
