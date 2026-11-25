#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../../.."
f=components/gateway_wifi_foundation
runtime="$f/test/idf_runtime"
out=$(mktemp -d /tmp/adr091-runtime-host.XXXXXX)
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -DADR091_SYNTHETIC_RUNTIME_SELFTEST=1 -I"$f" -I"$runtime/main" -I"$runtime/host/include")
case "${SANITIZER:-none}" in
 none) ;;
 address) flags+=(-fsanitize=address -fno-omit-frame-pointer -fno-pie -no-pie); export ASAN_OPTIONS=detect_leaks=0 ;;
 undefined) flags+=(-fsanitize=undefined -fno-omit-frame-pointer); export UBSAN_OPTIONS=halt_on_error=1 ;;
 *) exit 2 ;;
esac
cc "${flags[@]}" "$runtime/main/runtime_owner.c" "$runtime/host/test_runtime.c" "$f/wifi_codec.c" "$f/wifi_transaction.c" "$f/wifi_store.c" "$f/wifi_recovery.c" -o "$out/runtime_test"
"$out/runtime_test" | tee "$out/output"
! grep -q 'ADR091_RUNTIME_SELFTEST: FAIL' "$out/output"
grep -q 'ADR091_RUNTIME_SELFTEST: PASS owner_bytes=' "$out/output"
