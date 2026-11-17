# G2.3B-1R — HTTPS runtime qualification evidence

Verdict: **PARTIAL / IN_PROGRESS**. No actual ESP-IDF HTTP/HTTPS server or S3
hardware was executed. Host lifecycle tests and synthetic HTTPS regressions
passed; these do not qualify the actual ESP-IDF lifecycle. Firmware, Companion,
the SDK, partitions, credentials, hardware and frozen ADRs were not modified.

## Available environment

| Candidate | Observation | Classification |
| --- | --- | --- |
| Independently approved S3 fixture | None identified/authorized in the session; no Universal Node HIL tools exposed | NOT_RUN |
| FC61 | Explicitly excluded; no discovery, reset, flash or serial operation attempted | NOT_RUN |
| Installed IDF 5.5.4 Linux preview target | Genuine SDK compile/link probe attempted; build failed on missing `bsd/sys/cdefs.h` and `bsd/string.h` | BUILD_ONLY — FAIL |
| Gateway lifecycle fixture | Actual application owner/handler compiled with substituted HTTP/HTTPS, identity provider, timer and mutex APIs; no network server | HOST_UNIT — PASS |
| Existing loopback HTTPS fixture | Production raw HTTP/authentication core with OpenSSL TLS and synthetic credentials | HOST_SYNTHETIC_HTTPS — PASS |
| S3 Gateway | Build passed, no image executed | BUILD_ONLY — PASS |

The Linux probe did not establish compatibility with the exact server APIs.
It failed before server execution. Missing libbsd development headers are the
first observed dependency failure, not evidence that installing them alone will
make all HTTPD, FreeRTOS, lwIP and TLS APIs usable on Linux. No SDK or system
package was patched or installed to disguise this failure. No Xtensa QEMU
executable was found on PATH. This assessment describes the available session;
it does not assert that no usable runtime exists elsewhere.

Reproducible probe source was created under `/tmp/p4-idf-runtime-probe`:
`main/probe.c` includes `esp_https_server.h` and references `httpd_ssl_start` and
`httpd_ssl_stop`. The normal IDF project uses MINIMAL_BUILD and requires the
real `esp_https_server` component. Defaults enable lwIP and Mbed TLS server
support. No source, task, socket or TLS implementation was substituted in that
probe. It was never run. Command and failure log:

```sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
idf.py --preview -C /tmp/p4-idf-runtime-probe \
  -B /tmp/p4-idf-runtime-probe/build -D IDF_TARGET=linux build
# exit 2; /tmp/p4-runtime-probe.log
```

## API ownership defect and fail-closed correction

The preceding integration installed its own HTTPS receive override. This
violated this task's explicit instruction to preserve HTTPS-managed send,
receive, pending and transport callbacks. That interception is removed, along
with direct `esp_tls_conn_read`, socket flag manipulation and custom raw TLS
receiving. No send/receive/pending/transport-context or socket-close callback
is replaced. Only the documented user open hook and session user-context free
function remain. Output uses `httpd_socket_send`, which calls the HTTPS-managed
send function; it does not replace it.

**Unresolved integration constraint:** ADR-089 requires original header-field
occurrences for duplicate/framing rejection. The installed public
`httpd_req_get_hdr_value_len/str` functions return the first matching value.
They cannot establish duplicate Authorization absence. The inspected public
header has no original-header iterator or pre-normalization admission hook.
There is no first-value fallback and no private IDF structure cast.

Product `gateway_p4_https_start()` now returns `ESP_ERR_NOT_SUPPORTED` before
preparing a P4 identity or opening a listener. This is an unconditional product
safety gate, not a new architecture decision or a synthetic product option.
No startup caller exists. The host fixture can bypass only this gate using
`P4_HTTPS_HOST_TEST`, and compilation explicitly rejects that macro when
`ESP_PLATFORM` is defined. There is no production setter that admits requests.

The bounded application handler uses `httpd_req_recv` to assemble the actual
body in a 532-byte buffer, verifies complete length, and aborts incomplete
exchanges on timeout, error, EOF, truncation or invalid lengths. It checks an
absolute monotonic 10-second request deadline before/after receiving. Partial
reads never refresh it. A delivery guard mirrors the canonical 30-second
ADR-073 transaction deadline without extending or replacing canonical checks.
If a receive or proof crosses a deadline before delivery, any newly issued
association is revoked and no token is returned.

Admission remains false until route and timer setup finish. Allocation/table
exhaustion queues session closure because the installed HTTPS implementation
ignores the configured user's open-callback return. Session context cleanup
aborts pending transactions and frees only application context. It leaves
issued tokens under the existing P4/ADR-074 owners. Server shutdown stops
admission/timer work and waits for `httpd_ssl_stop` before releasing identity
material. A failed stop retains the owner handle and source key buffers,
rejects authentication, and permits a stop retry. No protected NVS is erased.

## Installed SDK lifetime findings

Sources inspected under `/home/aditya/esp/esp-idf-5.5.4`:

- `esp_https_server/src/https_server.c`: `httpd_ssl_open` establishes TLS,
  installs all transport callbacks and then invokes the user open hook. Its
  user-open return is ignored. `httpd_ssl_close` deletes the ESP-TLS session.
- `esp_http_server/src/httpd_sess.c`: session deletion closes the socket and
  clears user context once, then clears transport context. A second clear sees
  NULL. Do not call the application destructor twice on a freed pointer.
- `esp_http_server/src/httpd_main.c`: successful `httpd_stop` waits until the
  server task stops, then releases the global transport context and HTTPD state.
- `esp_https_server/src/https_server.c`: `httpd_ssl_stop` delegates to that
  `httpd_stop`; `create_secure_context` mallocs and copies the certificate and
  the private key into HTTPS-owned buffers. The key is **not merely borrowed**
  from `p4_https_identity`.
- `esp-tls/esp_tls_mbedtls.c` and `private_include/esp_tls_private.h`: each TLS
  session parses the server key into its own Mbed TLS PK context; server/client
  key names alias the same union member. Session deletion calls Mbed TLS cleanup.

P4 source-material ownership is unchanged: protected storage is read into the
identity owner's temporary record, the scalar is converted to a static bounded
PEM buffer, the temporary record is wiped, and the configuration borrows those
owner buffers. `p4_tls_material_clear` performs a volatile full-buffer wipe.
The Gateway conservatively retains the source buffers until successful stop.

**Concrete cleanup gap:** SDK `free_secure_context` calls
`free(cfg->serverkey_buf)` without an explicit zeroization of that malloc'd raw
private-key copy. Wiping the P4 source buffer after stop does not wipe that
SDK allocation. The production cleanup gate therefore remains **OPEN**, even
if host tests prove correct ordering of source-buffer release. The SDK was not
modified: SDK writes are outside this task's Gateway scope. Fixing this requires
an authorized SDK/upstream cleanup change covering both normal and failure
paths, followed by genuine runtime evidence. No raw TLS/global context was
replaced to work around it.

SDK source SHA-256 for audit reproducibility:

```text
https_server.c 80cf8b2a90d38535a6f9c819b008927fed00a9b1c7c6bc45160c94a3d2243067
httpd_sess.c  7b15704f9a1894506033566e50f84d4e4c9852be242b380f56548b14f8478016
httpd_parse.c db7fbbb322bccb4a21bc1607208a6bb7d1f4c0d7ecc3299de10c9e60edaa943b
```

## Executed test matrix

All application callback results in the next table are **HOST_UNIT**. The
HTTPD request object, session store/destruction, receive results, TLS ownership,
clock, queue and stop behavior are substitutes. They cannot prove actual IDF
teardown, threading, socket timeout, TLS cleanup or callback ordering.

| Focused lifecycle case | HOST_UNIT result | ESP_IDF_RUNTIME |
| --- | --- | --- |
| Missing original-header capability: no key load/listener | PASS | NOT_RUN |
| Identity configuration failure; start/register/timer failure | PASS | NOT_RUN |
| Connection arriving before setup commit | PASS: rejected | NOT_RUN |
| Complete START/PROOF, one-byte and short receives | PASS | NOT_RUN |
| Timeout/error/zero-byte receive during proof | PASS: empty failure, pending abort, no token | NOT_RUN |
| Truncated and excessive Content-Length; unsupported command | PASS: no authority | NOT_RUN |
| Repeated partial reads exhaust request deadline | PASS: no extension | NOT_RUN |
| Canonical transaction expired before proof | PASS: no token | NOT_RUN |
| Deadline crossed after proof creation but before delivery | PASS: new token/session revoked | NOT_RUN |
| Failed token delivery | PASS: association revoked | NOT_RUN |
| Context cleanup and duplicate clear | PASS: one modeled destruction | NOT_RUN |
| Pending disconnect and reused descriptor | PASS: retired transaction, new context generation | NOT_RUN |
| Issued token after connection closure and listener restart | PASS: canonical/P4 validation retained | NOT_RUN |
| Four connections/four transactions, fifth rejected | PASS: bounded, no eviction | NOT_RUN |
| Timer work coalescing and expired-context retirement | PASS | NOT_RUN |
| Stop with connected/pending clients, failed stop and retry | PASS: source material retained until modeled TLS teardown | NOT_RUN |
| Repeated start/stop and source release order | PASS | NOT_RUN |
| Actual source-buffer/SDK-copy zeroization under TLS | Not established by mock provider | NOT_RUN |

The final lifecycle run dispatched 26 requests and observed 21 modeled context
releases. AddressSanitizer, UndefinedBehaviorSanitizer and LeakSanitizer passed
on this host fixture. The first sandboxed LeakSanitizer attempt could not run
under ptrace; the final unsandboxed authorized run completed successfully.
This is not a leak or zeroization test of the real SDK.

Commands from the Gateway example directory:

```sh
bash scripts/run_p4_https_lifecycle_tests.sh
P4_TEST_SANITIZE=1 ASAN_OPTIONS=detect_leaks=1 \
  bash scripts/run_p4_https_lifecycle_tests.sh
P4_TLS_FIXTURE=/tmp/p488-generated bash scripts/run_p4_https_tests.sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
idf.py -B /tmp/un-p4b-s3-build build
git diff --check
```

- `HOST_UNIT`: lifecycle tests, existing Gateway adapter tests and ADR-089 core
  tests passed. Test headers intentionally expose no callback-override API.
- `HOST_SYNTHETIC_HTTPS`: the existing loopback integration passed two-client
  Local SCRAM/token delivery, reconnect validation, pending disconnect,
  cross-client/replay rejection, HTTP negatives, rate limits and resource faults.
  This fixture does not execute the ESP-IDF owner, HTTP parser or TLS server.
- `HOST_UNIT`: from the firmware directory, each of these six commands passed:

```sh
bash scripts/run_host_tests.sh adr073_auth_payload_codec
bash scripts/run_host_tests.sh adr073_auth_transaction
bash scripts/run_host_tests.sh adr073_auth_start_service
bash scripts/run_host_tests.sh adr073_auth_proof_service
bash scripts/run_host_tests.sh local_access_session
bash scripts/run_host_tests.sh p4_access_session
```

The earlier full manufacturer/certificate verification test was not repeated;
that implementation did not change. C6 build: NOT_RUN; shared firmware sources
were read-only and unchanged. No new hardware evidence is claimed.

## Resources and build

Source limits remain four TLS connections, four authentication transactions,
four Local sessions/P4 tokens, 4096 raw-header bytes, 532 request/response body
bytes, 10-second request/handshake bound and canonical 30-second transaction
expiry. Rate limits remain 8 global and 4 per connection per rolling 60 seconds.
The handler has fixed 532-byte body and 1024-byte serialized response buffers;
its response object is also fixed-size. The prior per-connection raw request
buffer and custom TLS receive loop were removed. Contexts are bounded by four.
The configured HTTP task stack remains 16384 bytes. Mbed TLS input/output
content limits in the observed S3 configuration are 16384/4096 bytes. These
values are limits, not measured live TLS heap or stack high-water marks.
Runtime heap/stack usage and concurrency behavior: NOT_RUN.

Final S3 binary: **1,352,096 bytes**. Each unchanged OTA slot is 2,097,152 bytes;
headroom **745,056 bytes (35.53%)**. The decrease from 1,403,712 is expected:
constant product admission failure allows the linker to discard inactive
listener code. This smaller image is not a successfully qualified HTTPS server.
No partition or product-traffic configuration changed.

## Remaining fixture and acceptance boundary

G2.3B-1 remains PARTIAL. Required before completion:

1. A supported original-header admission facility preserving HTTPS-managed
   callbacks, duplicate/framing checks and token-log suppression. It must not
   expose an unvalidated request to authentication. The current start gate must
   remain closed until that integration is implemented and qualified.
2. An authorized SDK/upstream solution that explicitly clears the HTTPS-owned
   raw private-key copy at normal/failure cleanup. P4 source-buffer wiping alone
   cannot satisfy this gate.
3. A demonstrated genuine IDF 5.5.4 runtime: an independently authorized S3
   fixture with a strictly test-only synthetic identity provider, or a Linux
   preview environment with its native prerequisites installed and these exact
   server/TLS APIs built and executed unmodified. Use the actual production
   owner/handler with the supported admission facility. No second HTTP server
   implementation, transport replacement or FC61 modification is acceptable.
4. Run the requested actual dispatch, fragmented/failed receive, slow-request
   and transaction deadlines, disconnect/context reuse, concurrent stop,
   repeated start/stop and key-copy release/zeroization tests. Measure real
   heap/stack if that runtime is S3 hardware. A passing host mock is insufficient.

Production bootstrap remains NOT_IMPLEMENTED; G2.1C remains pending. Product
traffic stays disabled. No FC61 reset/flash, eFuse programming, NVS erase,
production key generation, manufacturing update, credential rotation, Dataset
change, product route or OpenThread instance was introduced. G2.3B-2 was not
started. No commit was created.

Current-task changes are confined to Gateway: the HTTPS owner and its private
header, a lifecycle test plus four runtime-stub headers, the host runner's
mock/sanitizer build options, a lifecycle runner, this evidence note, and the
existing carriage evidence note. Existing unrelated worktree changes remain.

## Current-task file manifest

All paths are relative to the Gateway example directory:

- `components/universal_node_gateway/universal_node_gateway_p4_https.c`
- `components/universal_node_gateway/private_include/universal_node_gateway_p4_https.h`
- `components/universal_node_gateway/test/gateway_p4_https_lifecycle_test.c`
- `components/universal_node_gateway/test/runtime_stubs/esp_https_server.h`
- `components/universal_node_gateway/test/runtime_stubs/esp_timer.h`
- `components/universal_node_gateway/test/runtime_stubs/freertos/FreeRTOS.h`
- `components/universal_node_gateway/test/runtime_stubs/freertos/semphr.h`
- `scripts/run_p4_auth_host_tests.sh`
- `scripts/run_p4_https_lifecycle_tests.sh`
- `docs/G2_3B_1_AUTH_CARRIAGE_BOUNDARY.md`
- `docs/G2_3B_1R_RUNTIME_QUALIFICATION.md`

Gateway `git diff --check`, shell syntax checks and explicit whitespace checks
for the new/untracked files passed. No current-task firmware or Companion
writes occurred; no frozen ADR was edited.
