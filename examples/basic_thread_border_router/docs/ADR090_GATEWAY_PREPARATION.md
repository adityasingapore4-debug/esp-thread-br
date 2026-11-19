# ADR-090 Gateway preparation — pending exception and pinned source selection

2026-11-19. **Preparation/build qualification PASS; architecture approval pending.**
No endpoint is implemented or activated by this work. This report supersedes
only the source-selection/build-readiness portions of the isolated M3.5D report.
It does not confer product, authentication or manufacturing authority.

## Architecture record and approval

The standalone proposal is in the ADR-090 firmware worktree:

`docs/ADR Files V2/ADR-089-Addendum-01 — Public Identity Bootstrap V1.md`.

Status: **PROPOSED / PENDING ARCHITECTURE-OWNER AND USER APPROVAL**.
`PROJECT_WORKFLOW.md` gives final approval to the user and identifies Codex as
implementation engineer; there is no mechanism for Codex to self-approve this
exception. Frozen ADR-089/090 were not edited. The new proposal narrowly covers
public HTTPS `GET /un/p4/identity` and the 475-byte UNIB V1 body, with no Local
authentication prerequisite and no session/product/commissioning grant. It
preserves ADR-073/074/089, TLS peer-key and installation verification, BLE,
ADR-077/079, Dataset and Gateway Envelope boundaries and both listener guards.
It authorizes no route until explicit review/approval and later activation gates.

## Dependency audit

Before this work, the project hard-coded the original firmware path and the
Gateway component separately recalculated that same path. `EXTRA_COMPONENT_DIRS`
therefore always selected the legacy tree regardless of an external candidate
worktree. Host-test scripts already accepted `UN_FIRMWARE_ROOT`, but that did
not select firmware in ESP-IDF builds. No safe candidate CMake profile existed.

The default remains the same relative sibling:
`../../../../universal-node-thread-fw/firmware`, resolved from the example root.
On this machine that is `/home/aditya/esp/projects/universal-node-thread-fw/firmware`
at `d16a51020d0b3b46c26557649c3aef854c3916fb`, with three pre-existing P4
source edits. This checkout lacks the CIB storage API and checked P4 producer.
It was not modified, copied, reset or replaced.

Selection now belongs to `cmake/universal_node_firmware.cmake`:

- `UN_FIRMWARE_ROOT`: CMake PATH cache setting, defaulting to the same relative
  sibling. Explicit `-D` selection is supported without a machine-specific
  path in committed configuration. An environment variable alone does not
  silently change CMake source selection.
- `UN_FIRMWARE_EXPECTED_REVISION`: optional exact HEAD check for explicitly
  selected builds. The supplied candidate profile pins the reviewed M3 commit.
- `UN_FIRMWARE_BOUND_ROOT`: internal build-directory binding. Changing worktrees
  in an existing build directory fails and requires a fresh directory.
- Critical component-directory checks reject shadow Manufacturing Identity,
  P4 Server Identity or Node Identity owners from another source tree.

The component-local second selector was removed. Every existing firmware
component in `EXTRA_COMPONENT_DIRS` uses the same project selection. No owner
code or root policy was copied or altered, and no duplicate implementation was
introduced. Existing `REQUIRES` declarations already cover the producer's
Manufacturing Identity, P4 and Node Identity public interfaces. The host auth
runner needed only the Manufacturing Identity include path for the candidate's
public P4 header dependency.

Candidate cache file: `config/adr090-identity-evidence.cmake`. It selects relative
sibling `un-fw-adr090/firmware`, pins full HEAD
`7387eec914c561f83998944c574d74fa0f03c2e9`, and turns on
`UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY`. A different revision must be reviewed and
explicitly selected; it cannot silently pass this recipe. HEAD checks identify
the commit, not uncommitted file integrity: record worktree status as well. The
candidate owner source was clean; only the proposed architecture/status
records were added during this task.

## Inert qualification option

`UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY` defaults **OFF**. It is a CMake build option,
not a runtime or authentication flag. When explicitly enabled, it appends the
existing producer source and retains its function with a linker undefined-symbol
root so actual integration cost can be measured. It adds no caller, constructor,
HTTP adapter, URI registration, listener startup or public admission policy.
A selected tree without required ADR-090 headers/APIs fails; there is no legacy
fallback. The default configuration excludes the producer exactly as before.

The actual producer/header behavior is unchanged; only their comments now
explain the opt-in inert build. The producer still owns no credential, write,
manufacturer signing, session or product-dispatch authority.

## Reproducible dependency-selection recipe

From `examples/basic_thread_border_router`, after loading the qualified
ESP-IDF v5.5.4 environment:

```sh
cmake -G Ninja -S . -B /tmp/adr090-integration/candidate-build \
  -C config/adr090-identity-evidence.cmake \
  -D SDKCONFIG=/tmp/adr090-integration/sdkconfig-candidate \
  -D "SDKCONFIG_DEFAULTS=$PWD/build-dev1b-psram/sdkconfig.resolved;$PWD/config/dev1b-dma.defaults" \
  -D IDF_TARGET=esp32s3
cmake --build /tmp/adr090-integration/candidate-build
```

The cache profile contains no machine-specific source path. CMake prints the
resolved tree/revision and verifies the selected security-owner directories.
Use a fresh build directory for another firmware root. The existing development
SDK defaults include an ignored historical PSRAM base and the preserved dirty
DMA overlay: source selection is deterministic/revision-pinned, but this work
does **not** claim a fully clean tracked-input SDK configuration or release
reproducibility. Do not treat the profile as a production security configuration.

To reproduce the paired size comparison, first configure the same recipe with
`-D UN_GATEWAY_IDENTITY_EVIDENCE_QUALIFY=OFF`, build, and save the binary/ELF/map.
Then reconfigure that same root/config with the option ON and build. Do not
change other settings between measurements. Baseline copies from this run are
in `/tmp/adr090-integration/candidate-baseline`; build logs and JSON size reports
are under `/tmp/adr090-integration/`.

## Actual build qualification and composition

Both baseline OFF and inert qualification ON builds passed CMake, compilation,
linking, binary generation and app/partition-size checks. Target **esp32s3**,
ESP-IDF **v5.5.4**, compiler installation `esp-14.2.0_20260121`.
All Manufacturing/P4/Node Identity sources resolve to the pinned ADR-090 tree.
These builds compile actual firmware owner sources, not host fixture replacements.
The existing development TLS-smoke profile selects the real project's
`p4_storage_dev_ram.c` development provider; encrypted production storage and
hardware private-key handling are not qualified by this build.

Compiler manifests contain the producer once, contain no firmware test sources
or M1 issuer, and define none of `P4_HOST_TEST`,
`UN_CIB_STORAGE_HOST_TEST`, or `ADR090_SYNTHETIC_ISSUER_TEST`. Final ELF symbols:

| Function | Qualified ELF |
| --- | --- |
| gateway_p4_identity_evidence_build | retained |
| manufacturing_identity_verify_cib_v1 | retained |
| manufacturing_identity_cib_load | retained |
| p4_server_identity_build_proof_checked | retained |
| manufacturing_identity_verify_p4_server_key_proof | retained |
| cib_test_issue | absent |
| manufacturing_identity_cib_test_install | absent |

A separate real default CMake configuration passed, selecting only the legacy
firmware root and excluding the producer from compiler inputs. The default
firmware graph is preserved; no default full-image build was newly claimed.
The prior M3.5D legacy-linked S3 build remains historical evidence.

Existing nonfatal warnings include implicit BLE source extension (CMP0115),
unused variables/functions and existing logging/switch warnings. No new producer
warning was emitted. No warning suppression or unrelated repair was added.

## Matched flash and RAM measurements

| Metric | Candidate OFF | Candidate ON | Delta |
| --- | ---: | ---: | ---: |
| Application image bytes | 1693200 | 1693616 | **+416** |
| Flash code from map | 1277144 | 1277556 | +412 |
| Flash rodata from map | 278744 | 278744 | 0 |
| DIRAM initialized data | 22436 | 22436 | **0** |
| DIRAM BSS | 146704 | 146704 | **0** |
| DIRAM text | 98095 | 98095 | 0 |
| DIRAM used | 267235 | 267235 | 0 |
| DIRAM linker remainder | 74525 | 74525 | 0 |

The image delta includes image framing/alignment; it need not equal the map
code delta. Qualified image SHA-256:
`d87c094ef721db1f0bfdc20e2969113a96be3e5e1282221fa983cad965f1bc1c`.
Smallest unchanged application slot: 2,097,152 bytes; qualified headroom:
**403,536 bytes**. No partition-table change was made. Runtime heap, peak stack,
network concurrency or activated-endpoint RAM were not measured. Earlier isolated
object-frame measurements are not substitutes for those runtime values.

## Tests actually executed

- `bash scripts/test_firmware_selection.sh`: **6 scenarios PASS** (unchanged
  default, pinned candidate, missing root, wrong revision, mixed-tree reuse,
  unavailable ADR-090 API). No SDK/stub substitution is involved in these
  configuration tests.
- `bash scripts/run_p4_identity_evidence_tests.sh --firmware-root
  /home/aditya/esp/projects/un-fw-adr090/firmware`: **4,552 checks PASS** using
  actual owner verification with synthetic credentials and explicit host fault
  injection. This is host evidence, not the actual S3 build described above.
- Existing auth host regression, HTTP regression and HTTPS lifecycle tests using
  explicit candidate `UN_FIRMWARE_ROOT`: all exit 0. Lifecycle mock: 26 dispatched
  requests, 21 context releases.
- Existing inactive identity-surface test using the candidate host dependencies:
  **6 checks PASS**, including rejected GET and unchanged POST framing/token
  requirements.
- `git diff --check`: clean; listener/auth source diffs remain empty.

The original linked firmware checkout, proposed candidate owner source,
manufacturer roots, credentials, BLE, ADR-077/079, Matter, Dataset and Gateway
Envelope code were not modified. No commit, push or hardware flash was performed.

## Activation remains forbidden

No GET handler exists; no GET route is registered or reachable. `POST /un/p4/auth`
was not enabled or changed. Development TLS-smoke still registers no HTTP
handlers; normal product startup still fails when original raw-header admission
cannot be enforced. Neither the cache profile nor the proposed addendum bypasses
these gates. Selecting the M3 candidate can make local proof preparation fail
closed on devices without a valid CIB; it does not manufacture migration evidence.

Remaining gates: explicit addendum approval; completed bounded HTTP/rate/admission
contract; production manufacturer issuance/CIB provisioning and protected storage;
Companion CIB/installation/TLS verification; root-policy lifecycle; clean tracked
SDK configuration; controlled endpoint integration and separately approved HIL.
