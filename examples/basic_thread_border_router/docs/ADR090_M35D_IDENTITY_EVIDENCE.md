# ADR-090 M3.5D — isolated public identity evidence

2026-11-19. **PARTIAL**: bounded producer/encoder and host tests implemented.
No new HTTP handler exists, is registered or is reachable. This document is an
implementation report, **not architecture approval or an ADR addendum**.

## Architecture and source-linkage audit

Gateway repository: `~/esp/projects/esp-otbr/esp-thread-br`; branch
`dev/g2-dev-1b-lan-bringup`, starting HEAD `3720ae9`. Existing dirty
`config/dev1b-dma.defaults` and `docs/G2_DEV_1B_DMA_MEMORY_CORRECTION.md` were
preserved. Only new files under the basic-thread-border-router example were
written. Neither firmware worktree nor Companion was changed.

Both the project CMake and the Gateway component's relative CMake resolution
select `/home/aditya/esp/projects/universal-node-thread-fw/firmware`, branch
`fix/p4-s3-principal-node-binding`, HEAD `d16a510`. This checkout has three
pre-existing P4 diagnostic source changes; it lacks the ADR-090 CIB storage
header and checked proof producer. It was not replaced or modified.

The separate `/home/aditya/esp/projects/un-fw-adr090/firmware` worktree contains
the frozen ADR-090 and M3 APIs. It is used **only when explicitly named by the
new host-test command and isolated object compile**. This does not alter live
Gateway source selection. The host runner refuses a selected firmware tree
without those APIs; there is no implicit fallback or compatibility shim.

The actual ADR-066, ADR-073/074, ADR-077/079, ADR-085/086/089 and ADR-090
boundaries were reviewed, using the ADR-090 worktree where the linked checkout
lacks the later contract. ADR-089 still says:

> Before Local authentication, only this endpoint and these two Local auth commands

Its sole defined endpoint is `POST /un/p4/auth`; it explicitly does not define a
bootstrap endpoint. ADR-090 R5 requires bounded public evidence delivery but
leaves the concrete endpoint/carrier, owner contract and rate limiting pending.
No approving M3.5C pre-authentication exception/addendum was found in the
inspected Gateway or firmware architecture documents. The requested conditional
fallback therefore applies: implement the isolated producer/encoder/tests only.
No approval was fabricated and frozen ADR text was not changed.

## Implemented owner boundary

New private interface:

```c
gateway_p4_identity_result_t gateway_p4_identity_evidence_build(
    uint8_t *output, size_t capacity, size_t *length);
```

The caller owns one bounded 475-byte output workspace. Attestation, CIB and
proof bytes are placed directly into their final positions. No heap allocation,
static evidence cache, persistent copy, second 475-byte stack buffer, token
operation or manufacturing write interface is introduced.

The producer:

1. Retrieves and encodes the existing manufacturer attestation through its owner.
2. Loads the actual local CIB through `manufacturing_identity_cib_load()` and
   requires `CIB_VALID` and exactly 137 bytes.
3. Captures the current P4 public key/generation and calls the existing
   `p4_server_identity_build_proof_checked()`; true plus `CIB_VALID` is required.
4. Reuses `manufacturing_identity_verify_p4_server_key_proof()` on the actual
   delivered pair/proof. That owner reauthenticates the CIB and attestation
   internally, avoiding a redundant Gateway verification and extra verified-state
   stack object. No cryptography or signing implementation is duplicated.
5. Compares the proof key/generation with the initial owner snapshot, then reads
   the owner public key/generation again to detect changes during collection.
6. Writes the envelope header and publishes `*length=475` only after all checks.

Future integration must serialize calls with identity/root/key changes through
the existing security owner. The consistency checks do not create a new lock or
rotation protocol. A coherent signed response is public evidence, not a grant
of installation membership, Local/Admin authority, TLS possession or a session.

Failures return only UNAVAILABLE or NO_SPACE, set length zero, and clear the
bounded output region. No partial successful body is exposed. The code reads
no manufacturer private key, device private key, verifier, credential or Dataset.
The P4 owner retains its existing confined device signing operation.

## Proposed M3.5C body encoding

This is a **proposed, host-tested** envelope, not an approved live wire surface:

| Offset | Size | Encoding |
| --- | --- | --- |
| 0 | 4 | ASCII `UNIB` |
| 4 | 1 | Version 1 |
| 5 | 1 | Reserved 0 |
| 6 | 2 | Attestation length, BE: `00 8d` |
| 8 | 2 | CIB length, BE: `00 89` |
| 10 | 2 | Proof length, BE: `00 b9` |
| 12 | 141 | Exact existing signed manufacturer attestation |
| 153 | 137 | Exact actual verified CIB |
| 290 | 185 | Exact existing checked P4 proof |
| Total | 475 | No padding, terminator or compiler-structure serialization |

## Activation status and preserved listener restrictions

`universal_node_gateway_p4_identity.c` is intentionally **not listed in active
CMake**. No GET handler or HTTP serializer was added. The proposed
`GET /un/p4/identity` is not registered and is not reachable. The original raw
parser continues returning 404 for it; its existing query/Origin rejection and
token checks remain unchanged.

The following files have no changes relative to Gateway HEAD:

- `components/universal_node_gateway/CMakeLists.txt`
- `universal_node_gateway_p4_https.c`
- `universal_node_gateway_p4_http.c`
- `universal_node_gateway_p4_auth.c`

Development TLS-smoke still returns before registering any HTTP handler.
Normal product mode still fails closed when original raw HTTP-header admission
is unavailable. Product P4 has not been enabled. `POST /un/p4/auth`, AUTH_START,
AUTH_PROOF, SCRAM, connection ownership, Local sessions and token issuance are
unchanged. There is no pre-auth Control Frame/business-operation path.

Required future HTTP integration remains pending in full: HTTPS-only exact GET,
no body/query, `application/octet-stream`, exact 475-byte success,
`Cache-Control: no-store`, generic bounded empty failures, no redirect/plaintext
fallback/CORS authorization, capacity/rate/admission controls. This report does
not claim these policies are implemented by a body-only producer.

## Executed host validation

From `examples/basic_thread_border_router`:

```sh
bash scripts/run_p4_identity_evidence_tests.sh --firmware-root /home/aditya/esp/projects/un-fw-adr090/firmware
```

**4,552 checks PASS** using actual manufacturer attestation/CIB/P4 production
verification, synthetic nonproduction keys, existing NVS test seams and bounded
host identity/key-storage fixtures. Linker fault injection tests missing
attestation, absent/invalid/I/O CIB states, incorrect CIB length, corrupt signed
bytes, missing/invalid proof, mismatched identity, revoked root, key/generation
changes, all capacities 0..474, output guards and all-zero failure output.
All three source artifacts are compared byte-for-byte on success for synthetic
S3 FC61 and native C6 fixtures, including native FF:FE.

The producer object imports no allocation, storage mutation, direct signing,
dispatch, token or session functions. Failures disclose zero body bytes. No
additional authorization interface is exposed. The test runner explicitly
selecting the actual linked legacy firmware exits 2 with a clear missing-API
blocker; it does not silently use the candidate tree.

Existing regression commands (all using the unchanged actual linked firmware):

```sh
UN_FIRMWARE_ROOT=/home/aditya/esp/projects/universal-node-thread-fw/firmware P4_TEST_OUTPUT=/tmp/adr090-m35d/auth-test bash scripts/run_p4_auth_host_tests.sh
UN_FIRMWARE_ROOT=/home/aditya/esp/projects/universal-node-thread-fw/firmware P4_TEST_SOURCE=components/universal_node_gateway/test/gateway_p4_http_test.c P4_TEST_OUTPUT=/tmp/adr090-m35d/http-test bash scripts/run_p4_auth_host_tests.sh
UN_FIRMWARE_ROOT=/home/aditya/esp/projects/universal-node-thread-fw/firmware P4_TEST_SOURCE=components/universal_node_gateway/test/gateway_p4_identity_surface_test.c P4_TEST_OUTPUT=/tmp/adr090-m35d/surface-test bash scripts/run_p4_auth_host_tests.sh
UN_FIRMWARE_ROOT=/home/aditya/esp/projects/universal-node-thread-fw/firmware bash scripts/run_p4_https_lifecycle_tests.sh
```

All exit 0. The HTTP suite reports header/token/expiry/rotation/revocation/reset,
failed delivery, RNG and stale-context preservation. The new inactive-surface
test passes 6 checks. Existing HTTPS lifecycle mocks pass 26 dispatched requests
and 21 context releases, including admission failure behavior. These are host
unit tests, not live HTTPS/HIL. `git diff --check` passes; existing dirty files
remain unchanged by this task.

## ESP32-S3 compilation and memory evidence

ESP-IDF v5.5.4 was loaded from `/home/aditya/esp/esp-idf-5.5.4/export.sh`.
The isolated producer was compiled with `xtensa-esp32s3-elf-gcc`, `-Os`,
`-std=gnu11 -Wall -Wextra -Werror -fstack-usage`, using the Gateway private header
path and explicitly selected ADR-090 candidate manufacturing/P4 public headers.
This compile does not link it into the Gateway or qualify the candidate
firmware's complete S3 owner implementations.

`xtensa-esp32s3-elf-size /tmp/adr090-m35d/identity-s3.o` reports:

```text
text: 401 bytes
data: 0 bytes
bss: 0 bytes
compiler-reported producer frame: 272 bytes (static)
```

The frame excludes called security-owner functions, caller-provided 475-byte
storage, HTTP/TLS stack and concurrency. Object text is **not** a measured linked
flash increment for an activated endpoint. No runtime heap/stack/HIL value is
claimed.

A separate actual Gateway build retained the original firmware linkage and used
the existing DEV-1B profile (including the preserved dirty DMA overlay):

```sh
idf.py -B /tmp/adr090-m35d/gateway-build -D SDKCONFIG=/tmp/adr090-m35d/sdkconfig -D "SDKCONFIG_DEFAULTS=$PWD/build-dev1b-psram/sdkconfig.resolved;$PWD/config/dev1b-dma.defaults" -D IDF_TARGET=esp32s3 build
```

CMake, compile, link and binary/partition-size checks **PASS**. Image size is
**1,693,728 bytes**, smallest slot **2,097,152 bytes**, free **403,424 bytes**.
SHA-256 `c437599472a5a1274d8b9af29cc01552522dffdd0fa0303b4f194a0871b9f641`.
No flash/reset/device command was executed.

`python -m esp_idf_size --format json` on the actual map measured:

| Metric | Recorded existing DMA-profile reference | This build | Difference |
| --- | ---: | ---: | ---: |
| Binary bytes | 1693728 | 1693728 | 0 |
| DIRAM initialized data | 22436 | 22436 | 0 |
| DIRAM BSS | 146704 | 146704 | 0 |
| DIRAM text | 98095 | 98095 | 0 |
| DIRAM total | 267235 | 267235 | 0 |
| Linker-unassigned DIRAM | 74525 | 74525 | 0 |

Reference: existing `docs/G2_DEV_1B_DMA_MEMORY_CORRECTION.md`. This is comparison
with recorded profile totals, **not** a controlled before/after activated-code
measurement or proof of byte-identical images. The configuration base is an
ignored historical `sdkconfig.resolved`, so clean tracked-input reproducibility
remains a known limitation. Actual compiler manifest confirms the producer is
absent and manufacturing sources come from the original firmware checkout.
The build validates preserved Gateway composition, not live evidence delivery.

## Remaining blockers

1. Architecture-owner approval of the scoped ADR-089 pre-auth exception and
   concrete M3.5C envelope/HTTP/admission/rate contract.
2. Explicitly authorized Gateway firmware source integration bringing the
   ADR-090/M3 APIs into its actual dependency tree. No substitution was made.
3. Manufacturer issuance and authorized immutable CIB provisioning/backfill,
   production protected storage and root-policy lifecycle qualification.
4. Companion CIB verification plus independent installation/TLS provenance.
5. Supported original-header admission under the frozen listener contract;
   neither development smoke nor host support may bypass that production gate.
6. Clean configuration reproducibility, activated-code flash/RAM/concurrency
   measurements and live HTTPS/HIL qualification.

No BLE, commissioning, Join Existing carrier, Thread Dataset, Gateway Envelope,
mesh authorization, Registry, Matter, Companion or manufacturing authority was
changed. Live endpoint activation and production deployment are not complete.
