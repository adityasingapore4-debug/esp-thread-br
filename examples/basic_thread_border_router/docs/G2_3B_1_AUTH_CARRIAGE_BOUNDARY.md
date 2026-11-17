# G2.3B-1 — ADR-089 HTTPS authentication implementation evidence

Status: **PARTIAL / IN_PROGRESS**. See
[G2.3B-1R runtime qualification](G2_3B_1R_RUNTIME_QUALIFICATION.md) for current
source, environment and test evidence. The runtime follow-up removed the
HTTPS receive override, converted the handler to `httpd_req_recv`, and closed
product startup with `ESP_ERR_NOT_SUPPORTED` until supported original-header
admission exists. The SDK-owned raw private-key copy also lacks an explicit
wipe at free. No actual ESP-IDF server or S3 hardware has been executed.
The host HTTPS results below remain host-only evidence.

This replaces the earlier “HTTP mapping unresolved” implementation note.
ADR-089 is frozen and is now the carriage authority. No ADR was changed.

## Implemented boundary

`universal_node_gateway_p4_http.c` contains the bounded raw HTTP admission,
canonical token codec, rolling admission window, auth response projection,
future bearer validation seam, response serializer and failed-delivery cleanup.
It calls the existing Gateway adapter, ADR-073, ADR-074 and P4 access-session
owners. There is no second SCRAM verifier or business dispatcher.

The raw boundary checks original field occurrences before HTTP API
normalization: exact HTTP/1.1 route/method/type, Host, one decimal Content-Length,
no transfer/content coding, folding, duplicate framing or Authorization,
no Origin, maximum 4096-byte header section and 532-byte body. AUTH_START is
additionally limited to 519 bytes and LOCAL_SESSION; both commands use the
existing strict ADR-073 codecs. Unknown product paths never enter a queue or
router. All failures are empty and no-store; successful payloads preserve the
command and unchanged logical response. Proof success issues exactly one
canonical 43-character P4-Access-Token header. Failed delivery revokes its
P4 association and underlying Local session.

The separate bearer seam accepts exactly `Bearer <43 canonical characters>`
and validates the P4 association and canonical Local session. It grants no
product command authority. Duplicate fields are rejected at raw admission,
including case variants; malformed/unknown/expired/revoked tokens cannot grant
Local authority. The future 401 serializer includes the frozen challenge.

Rate admission retains eight global and four per-connection AUTH_START times
in a rolling 60-second window, before verifier access. Global state survives
connection churn and listener restart; reset belongs to runtime reset, not
connection opening. Tables do not evict authenticated sessions. The minimal
adapter extension distinguishes canonical proof failure (403) from failed
session/token creation (503), and observes canonical transaction-allocation RNG
failure without changing its allocation or SCRAM rules.

## ESP-IDF integration and ownership

Current integration and its fail-closed start gate are documented in
[G2.3B-1R](G2_3B_1R_RUNTIME_QUALIFICATION.md). The former raw receive override
was removed because it replaced HTTPS-managed transport ownership. No custom
send/receive/pending/transport or socket-close callback is installed now.
Public request-header lookup alone cannot prove duplicate-header absence, so
production startup fails before key preparation or listener creation. The
remaining handler and lifecycle source are exercised only by the explicitly
classified host-unit fixture. SDK-owned raw key-copy cleanup remains open.

The existing plaintext server and route registration are untouched. Product
traffic remains disabled; HMAC key ID remains -1. There is no trust-bootstrap
endpoint, global response queue, Device Events, mesh session-start reuse,
Admin/Recovery grant, Thread instance, partition edit or hardware operation.

## Executed validation

From the Gateway example directory:

```sh
P4_TLS_FIXTURE=/tmp/p488-generated bash scripts/run_p4_https_tests.sh
P4_TLS_FIXTURE=/tmp/p488-generated bash scripts/run_p4_verified_https_test.sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
idf.py -B /tmp/un-p4b-s3-build build
git diff --check
```

The first script executes the production adapter tests, raw HTTP/session tests,
and real loopback HTTPS tests. OpenSSL supplies the host TLS backend; the exact
production HTTP/authentication core, serializer and canonical firmware services
process requests. It does not execute `universal_node_gateway_p4_https.c` or the
ESP-IDF HTTP parser. Test-only RNG and verifier fault injection exists only in
the host fixture. No authentication-success flag is substituted.

Observed host results: two concurrent real Local SCRAM clients, transaction and
server-final correlation, independent tokens, exact token header and encoding,
lookup on a new TLS connection, cross-client proof rejection, replay, pending
close cleanup, equivalent deterministic context/descriptor reuse, malformed
headers and bodies, duplicate fields, folding, pipelining, wrong method/path/
type/scope/version, unknown product routes, plaintext rejection, invalid Local
proof, unavailable verifier, token RNG failure, full session/token table,
per-connection/global limits and rolling-window recovery all passed. Focused
owner tests also passed token expiry, Local generation rotation, canonical
session reset/revocation, P4 reset, failed delivery revocation, transaction expiry
and canonical allocation RNG failure.

The second script passed one end-to-end synthetic manufacturer-chain test.
It uses the existing read-only Companion manufacturer verifier, P4 proof and
certificate verifier, actual-peer TLS connector, ADR-073 codecs and SCRAM client.
It verifies a synthetic manufacturer -> device -> P4 delegation before sending
Local proof, rejects a correctly signed delegation for a different TLS key,
checks the SCRAM server-final and transaction ID before accepting the token,
then reuses the token over a newly verified connection. Its temporary Flutter
project and generated test file live under /tmp. No Companion source was edited;
G2.3B-2 product integration was not started.

From the firmware directory, each command was executed and passed:

```sh
bash scripts/run_host_tests.sh adr073_auth_payload_codec
bash scripts/run_host_tests.sh adr073_auth_transaction
bash scripts/run_host_tests.sh adr073_auth_start_service
bash scripts/run_host_tests.sh adr073_auth_proof_service
bash scripts/run_host_tests.sh local_access_session
bash scripts/run_host_tests.sh p4_access_session
bash scripts/run_host_tests.sh admin_operation_authorizer
bash scripts/run_host_tests.sh recovery_operation_authorizer
bash scripts/run_host_tests.sh auth_domains_verifier
bash scripts/run_host_tests.sh provisioning_v2
git diff --check
```

All ten regression targets passed. The C6 build was not run because no shared
production component changed in this task. Existing earlier worktree changes,
including the dedicated partition and identity implementations, were preserved.
No test result in this note represents a hardware run.

## Remaining acceptance boundary

Before marking G2.3B-1 COMPLETE, execute the supported original-header admission, standard ESP-IDF receive,
HTTP handler, session-context destruction, deadline/slow-request retirement,
pipelining checks and TLS shutdown/key-buffer lifetime with a qualified
synthetic ESP-IDF fixture. The host backend bypasses these hooks, so their
runtime behavior cannot be inferred from the passing host HTTPS tests.
G2.1C remains pending and production trust-bootstrap delivery remains
NOT_IMPLEMENTED. No FC61 flash, eFuse programming, NVS erase, credential or
Dataset change, or product listener activation occurred. Do not start G2.3B-2
as part of this closeout.

## Previous G2.3B-1 build and change manifest

Previous G2.3B-1 S3 binary (superseded by the G2.3B-1R build):
**1,403,712 bytes**; each existing OTA slot remains
2,097,152 bytes. Headroom is **693,440 bytes (33.07%)**, a 51,632-byte increase
from the supplied 1,352,080-byte baseline. Both 2 MiB slots and the partition
table were unchanged by this task. This was the previous integration measurement. Current G2.3B-1R binary:
**1,352,096 bytes**, headroom **745,056 bytes (35.53%)**. Its constant product
start gate permits the linker to discard inactive listener code.

Previous G2.3B-1 task firmware change: `docs/PROJECT_STATE.md` only. Firmware production
components, frozen ADRs and Companion sources were not changed in this task.
Previous G2.3B-1 task Gateway changes, relative to the example directory:

- `components/universal_node_gateway/CMakeLists.txt`
- `components/universal_node_gateway/universal_node_gateway_p4_auth.c`
- `components/universal_node_gateway/private_include/universal_node_gateway_p4_auth.h`
- `components/universal_node_gateway/universal_node_gateway_p4_http.c`
- `components/universal_node_gateway/private_include/universal_node_gateway_p4_http.h`
- `components/universal_node_gateway/universal_node_gateway_p4_https.c`
- `components/universal_node_gateway/private_include/universal_node_gateway_p4_https.h`
- `components/universal_node_gateway/test/gateway_p4_auth_adapter_test.c`
- `components/universal_node_gateway/test/gateway_p4_http_test.c`
- `components/universal_node_gateway/test/gateway_p4_https_fixture.c`
- `components/universal_node_gateway/test/p4_verified_https_test.dart.in`
- `scripts/run_p4_auth_host_tests.sh`
- `scripts/run_p4_https_tests.sh`
- `scripts/run_p4_verified_https_test.sh`
- `scripts/test_p4_https.py`
- `docs/G2_3B_1_AUTH_CARRIAGE_BOUNDARY.md`

The full-chain fixture obtains its synthetic canonical installation, principal
and Node ID through launcher stdout before making a TLS connection. This is
out-of-band test setup, not a production discovery/bootstrap endpoint. Its P4
proof and expected trust context match those canonical fixture identities.

`git diff --check` passed in both affected repositories; shell/Python syntax
checks and explicit whitespace checks of the new/untracked files also passed.
No commit was created. Earlier unrelated/uncommitted work remains present.
