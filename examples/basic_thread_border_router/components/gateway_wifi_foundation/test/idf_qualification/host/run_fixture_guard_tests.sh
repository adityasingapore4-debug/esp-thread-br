#!/usr/bin/env bash
set -euo pipefail
qualification_dir=$(cd "$(dirname "$0")/.." && pwd)
artifact_dir=$(mktemp -d /tmp/adr091-fixture-guard.XXXXXX)
cc -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$qualification_dir/main" \
    "$qualification_dir/main/fixture_guard.c" \
    "$qualification_dir/main/fixture_allowlist.c" \
    "$qualification_dir/main/result_protocol.c" \
    "$qualification_dir/main/qualification_main.c" \
    "$qualification_dir/host/test_fixture_guard.c" \
    -o "$artifact_dir/test_fixture_guard"
"$artifact_dir/test_fixture_guard"
echo "Artifacts: $artifact_dir"
