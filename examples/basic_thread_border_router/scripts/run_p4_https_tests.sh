#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
p4_fw="${UN_FIRMWARE_ROOT:-/home/aditya/esp/projects/universal-node-thread-fw/firmware}"
export P4_TLS_FIXTURE="${P4_TLS_FIXTURE:-/tmp/p489-synthetic}"
# The existing qualified generator exports only its synthetic scalar-1 fixture.
if [[ ! -f "$P4_TLS_FIXTURE.der" || ! -f "$P4_TLS_FIXTURE.key" ]]; then
    IDF_PATH="${IDF_PATH:-/home/aditya/esp/esp-idf-5.5.4}" bash "$p4_fw/scripts/run_p4_tls_host_tests.sh" "$P4_TLS_FIXTURE" > /tmp/p489-tls-fixture.log 2>&1
fi
bash scripts/run_p4_auth_host_tests.sh
P4_TEST_SOURCE=components/universal_node_gateway/test/gateway_p4_http_test.c P4_TEST_OUTPUT=/tmp/gateway-p4-http-test bash scripts/run_p4_auth_host_tests.sh
P4_TEST_SOURCE=components/universal_node_gateway/test/gateway_p4_https_fixture.c P4_TEST_OUTPUT=/tmp/gateway-p4-https-fixture P4_TEST_BUILD_ONLY=1 bash scripts/run_p4_auth_host_tests.sh
python3 scripts/test_p4_https.py
