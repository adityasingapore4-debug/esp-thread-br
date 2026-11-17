#!/usr/bin/env bash
# HOST_UNIT ONLY: production callbacks with substituted ESP-IDF and TLS APIs.
set -euo pipefail
cd "$(dirname "$0")/.."
export P4_HTTPS_RUNTIME_MOCK=1
export P4_TEST_SOURCE=components/universal_node_gateway/test/gateway_p4_https_lifecycle_test.c
export P4_TEST_OUTPUT=/tmp/gateway-p4-https-lifecycle-test
bash scripts/run_p4_auth_host_tests.sh
