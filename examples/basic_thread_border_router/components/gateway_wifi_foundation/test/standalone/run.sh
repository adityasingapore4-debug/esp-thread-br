#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../../.."
foundation=components/gateway_wifi_foundation
out=$(mktemp -d /tmp/adr091-service.XXXXXX)
flags=(-std=c11 -Wall -Wextra -Werror -pedantic -DADR091_STANDALONE_HOST_TEST -I"$foundation" -I"$foundation/test/standalone")
case "${SANITIZER:-none}" in
  none) ;;
  address) flags+=(-fsanitize=address -fno-omit-frame-pointer -fno-pie -no-pie); export ASAN_OPTIONS=detect_leaks=0 ;;
  undefined) flags+=(-fsanitize=undefined -fno-omit-frame-pointer); export UBSAN_OPTIONS=halt_on_error=1 ;;
  *) echo 'Unsupported sanitizer' >&2; exit 1 ;;
esac
cc "${flags[@]}" "$foundation/wifi_codec.c" "$foundation/wifi_transaction.c" "$foundation/wifi_store.c" "$foundation/wifi_recovery.c" "$foundation/test/standalone/service.c" "$foundation/test/standalone/test_service.c" -o "$out/test_service"
"$out/test_service"
