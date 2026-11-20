# M4W.2B storage foundation implementation report

M1/M2 historical report. The later [M3 recovery report](STORAGE_RECOVERY_REPORT.md)
documents the coordinator, runtime fencing seam and stronger process-lifetime
NVS uncertainty latch. M3 leaves the encoding below unchanged. Fresh adapter
construction cannot clear NVS uncertainty in the same process.

Date: 2026-11-20. Submitted for independent review; this is not M4W.2B acceptance.

```text
M4W_2B_STORAGE_FOUNDATION:
VERDICT: PARTIAL
STORAGE_OWNERSHIP_AUDIT: PASS
NVS_PARTITION_COMPATIBILITY: UNVERIFIED
TWO_SLOT_RECORD_FORMAT: PASS
INDEPENDENT_SLOT_VALIDATION: PASS
GENERATION_SELECTION: PASS
ATOMIC_PROMOTION_CONTRACT: PARTIAL
CANCELLATION_FENCING: PASS
UNKNOWN_OUTCOME_RECOVERY: PARTIAL
CREDENTIAL_ZEROIZATION: PASS
FAULT_INJECTION: PASS
HOST_TESTS: PASS
ASAN: PASS
UBSAN: PASS
REAL_NVS_BACKEND: IMPLEMENTED
ESP_IDF_STORAGE_BUILD: PASS
PRODUCTION_REGISTRATION: DISABLED
PROTECTED_STORAGE_QUALIFICATION: NOT_APPROVED
HARDWARE_OPERATIONS: NONE
```

PASS for record/fencing/failure behavior denotes the host boundary contract;
it is not live-flash qualification. Static partition layout is compatible;
actual device occupancy and production protection remain unverified. Real NVS
API implementation and compilation are complete; measured completion bounds,
shared-partition recovery orchestration and hardware qualification remain open.

## Authority and scope

Gateway branch `dev/g2-dev-1b-lan-bringup`, clean starting HEAD
`902fc76850324ab83b77b93a8296c60b44907fd5`. Architecture checkout HEAD was
`362934baf571e60a2aa92c2586f5d737868ef627`; the required acceptance review and
later state commits were unavailable. The user explicitly authorized proceeding
with local approved ADR-091 / Addendum 01 and confirmed M4W.2A acceptance,
architecture acceptance commit `256b07e6c7f3085706f4eeed4922ebee0821bb66` and
state commit `ccefcfe41241e778e68e0be75e84fcb5657fdcb0`. Those later commits
were not independently inspected locally. No architecture documents changed.
No unresolved architecture conflict identified under this authorization.

All changes are inside `examples/basic_thread_border_router/components/`
`gateway_wifi_foundation/`. No production CMake registration was added. No
startup, legacy CLI, partition table, installation/Thread/manufacturer/P4
records, BLE, HTTP, HTTPS or command registration changed. No hardware tool,
flash/reset, customer credential write, commit, push or M4W.3 work occurred.

## M1 storage ownership audit

Inspected the requested `partitions.csv`, `main/esp_ot_br.c`,
`config/dev1b-dma.defaults`, `esp_ot_wifi_cmd.c`, `esp_br_wifi_config.c`,
the existing foundation, firmware source-selection CMake, and the directly
relevant provisioning/security/manufacturer/P4 storage owners.

| Partition | Size | Existing ownership / disposition |
|---|---:|---|
| `nvs` | `0x6000` (24 KiB, six 4 KiB pages) | Shared application NVS; legacy `wifi_config`, installation `prov_v2` / `sec_root` and other application users. New Wi-Fi records are confined to `gw_wifi`. |
| `mfg_identity` | `0xE000` | Manufacturing Identity; separate partition initialization and namespace. Never opened by new backend. |
| `p4_identity` | `0x6000` | P4 identity owner; never opened by new backend. |

`app_main()` calls `ESP_ERROR_CHECK(nvs_flash_init())` once before canonical
Gateway initialization. The new adapter requires this existing lifecycle; it
never initializes, erases or deinitializes any partition. It uses explicit
`nvs_open_from_partition("nvs", "gw_wifi", ...)` with keys `slot_a` / `slot_b`.
Namespace/key strings are storage details, not wire protocol.

Legacy CLI opens `wifi_config` read/write, writes `ssid` and `password`
separately with separate commits, and exposes legacy print/clear paths. Existing
development startup reads the namespace read-only and uses the no-persist
connection path. The web config handler retains credentials in RAM for its
caller; it does not itself write NVS. These are existing paths, not new storage
authority or migration sources. No inspected component owns ADR-091 `gw_wifi`
records already. The backend never imports `wifi_config` or erases a namespace.

`dev1b-dma.defaults` disables driver NVS. The no-persist Wi-Fi helper explicitly
selects `WIFI_STORAGE_RAM`; the legacy saved-credential helper retains its own
NVS writes. Other profiles may retain driver persistence. Future activation
must qualify a single Wi-Fi settings owner and RAM-backed driver behavior;
this isolated component performs no Wi-Fi association or driver calls.

The inspected example `sdkconfig` disables `CONFIG_NVS_ENCRYPTION` and
`CONFIG_SECURE_FLASH_ENC_ENABLED`. This does not establish any device eFuse or
key state, nor qualify a production profile. CRC provides accidental corruption
detection, no confidentiality, authentication or anti-rollback protection.
No encryption setting or key changed. Default NVS adapter writes are locked
off; a synthetic-test-only compilation define enables the API write path in
an isolated test. That define is absent from the isolated IDF build and all
production configuration. Stub tests exercise it without flash access.

Maximum record length is 111 bytes. v5.5.2 documents blob allocation as two
overhead entries plus one per 32 bytes, so each record needs six entries. Two
records plus a namespace need approximately 13 live entries; replacement also
needs transient allocation and NVS garbage-collection headroom. Six pages can
support this design without a new partition, but no live free-space inventory
was requested or performed. The adapter checks `available_entries` before a
write (seven entries for a maximum record, conservatively including namespace
creation). That check is not a reservation against other shared users. Capacity
or flash errors fail closed without deliberately touching the previous slot.
The layout passes the M1 design gate; live capacity remains a qualification gate.

## Record encoding and boot selection

All fields are bytes unless specified. No raw C structure is persisted.

| Offset | Field |
|---:|---|
| 0..1 | ASCII `GW` magic |
| 2 | schema `1` |
| 3 | reserved, must be zero |
| 4..7 | nonzero generation, uint32 big-endian |
| 8 | auth mode `1` WPA2 personal or `2` WPA3 personal |
| 9 | SSID length, 1..32 |
| 10 | credential length, 8..63 |
| 11 | reserved, must be zero |
| 12.. | exact SSID bytes, then exact credential bytes |
| last 4 | CRC-32/ISO-HDLC, big-endian, covering every preceding byte |

Total length is exactly `16 + ssid_length + credential_length`, 25..111 bytes.
SSID bytes cannot contain NUL; credentials are printable ASCII `0x20..0x7e`,
matching the accepted V1 codec. All limits, schema, reserved fields, full length,
generation and CRC are validated independently in each slot. Oversized/wrong-
type blobs are invalid; I/O errors are unavailable, not empty.

Boot selects the highest valid generation. A single valid slot wins over an
empty or invalid peer. Equal generations with identical payloads are equivalent;
equal generations with differing payloads fail closed. Both absent produce a
successful unconfigured load with generation zero and cleared configuration.
Neither valid with any invalid slot produces a load/init error, preventing new
mutations until explicit recovery. Generation `UINT32_MAX` loads but cannot
mutate; target must be exactly current + 1 with no wrap. Restart recovers only
configuration authority, never pending transactions, authorization or sessions.

## Promotion, fencing and recovery

The existing `wifi_store_t` ABI and transaction owner remain unchanged.
`wifi_storage_interface()` supplies its callbacks. Only owner-issued promotion
admission may call `stage`. Stage copies the candidate, nonzero transaction ID,
target generation and monotonically increasing ticket, without I/O. Caller
input pointers are never retained. Queued stage and UNKNOWN exclude all new
candidates. The worker entry `wifi_storage_execute` checks the immutable tuple;
stale ID, generation or ticket has no side effects. It writes only the inactive
slot, commits, checks exact readback bytes, then scans both records to confirm
that the candidate is authoritative. A callback alone never establishes success.

All entry points require exclusive owner serialization, including execution
and recovery. The future owner must schedule blocking storage work away from
BLE and deliver immutable completion identity to `wifi_tx_complete`. There are
no backend-created tasks, asynchronous vendor callbacks or retained I/O pointers.
Do not call cancel/restart concurrently with an active NVS function. The
worker/state machine must outlive operations, and fresh initialization requires
all old work to be drained. Tickets are preserved by soft restart and cannot
be reused within a backend lifetime.

Cancel of queued work atomically fences it in this serialized model and returns
ABORTED, clearing its candidate. A later worker with its old tuple cannot write.
After write begins, any write/commit/readback/selection failure returns UNKNOWN;
clear is never a fence and retains the minimal verification candidate. DURABLE
requires successful commit and exact authoritative readback. Candidate and
temporary encoded, decoded, scan and readback buffers are explicitly wiped
when safe. Committed configurations intentionally returned to the private
transaction owner remain secret-bearing; no public status contains credentials.

`restore` is a read-only previous-record verification; it neither writes a
rollback record nor attempts association. A future runtime owner must perform
the actual Wi-Fi restoration and deliver its rollback completion separately.

Recovery performs at most one boundary reopen and one scan of two bounded
records, without retry loops. If the accepted candidate is authoritative,
return DURABLE. If the previous generation/slot remains authoritative after a
successful writer fence, return ABORTED. Mismatching higher records or failed
recovery remain UNKNOWN. Restart quarantines mutations before reopening; a
failed reopen or scan cannot be reported as successful recovery. Successful
restart wipes old candidate identity and reports only current boot authority.

**Remaining real-NVS dependency:** v5.5.2 `ESP_ERR_NVS_REMOVE_FAILED` may require
NVS reinitialization. A handle close/open is not that recovery. The adapter
latches uncertainty on write/commit errors and refuses reopen/recovery and
further writes until an externally controlled actual restart reinitializes
NVS. It never deinitializes shared storage itself. Production needs an owner-
orchestrated, bounded controlled restart policy and measured worst-case vendor
flash completion/recovery time. The bounded host call count is not a wall-clock
guarantee. UNKNOWN must not become an indefinite production lifecycle.

## Authoritative local ESP-IDF assumptions

Reviewed installed `/home/aditya/esp/esp-idf`, `git describe` = `v5.5.2`:

- `components/nvs_flash/include/nvs.h`: namespace isolation, maximum names,
  bounded `nvs_get_blob` size query, blob overhead, error codes, explicit commit
  requirement and handle lifecycle.
- `components/nvs_flash/src/nvs_api.cpp` and `nvs_handle_simple.cpp`:
  current `nvs_commit` delegates to a no-op successful handle commit. Writes
  may already be persistent before commit; commit failure cannot imply abort.
- `components/nvs_flash/src/nvs_storage.cpp` and `nvs_page.hpp`: blob index,
  chunk replacement and page constraints.
- `docs/en/api-reference/storage/nvs_flash.rst`: 4096-byte pages, 32-byte
  entries, 126 entries/page, namespace isolation, power interruption recovery
  expectations and encryption limitations.

Set failures may have written data, especially REMOVE_FAILED; mocks explicitly
model write-before-commit persistence and post-persistence failure. A valid
higher-generation slot after actual reboot may be recovered even without a
previous success callback. No lost transaction result or event ordering is
invented. NVS API source review is not physical power-cut evidence.

## Deterministic fault matrix and results

`test_storage` passes **4,431 checks**. Its 84 failure combinations cover seven
faults × six prior generations × immediate/deferred persistence models. Later
generations prepopulate both slots so replacement of an existing inactive blob
is exercised. Every combination compares the full previous slot against its
pre-fault bytes and boots a fresh backend directly from simulated persistent
bytes before resolving the old transaction.

| Scenario | Observed result |
|---|---|
| Both slots empty | Unconfigured, generation zero |
| First and second successful commit | Generations 1 then 2 recovered |
| Repeated A/B updates | 24 generations, verified restart and previous-slot fallback |
| Either slot invalid | Other slot independently recovered |
| Both invalid / invalid plus absent | Init fails safely |
| Write fails before persistence | UNKNOWN initially; recovery proves previous record / ABORTED |
| Torn inactive update | Previous slot retained through recovery and fresh boot |
| Write returns error after persistence | UNKNOWN initially; recovery verifies candidate / DURABLE |
| Commit fails before deferred persistence | UNKNOWN; recovery proves previous record |
| Commit fails after immediate persistence | UNKNOWN; recovery verifies new record |
| Readback I/O failure or differing bytes | Never false success; recovery required |
| Valid higher record differs from candidate | Remains UNKNOWN; explicit restart reports authority only |
| Loss before queued write | Restart fences delayed tuple; previous record retained |
| Loss during/after update | Fresh boot selects only independently valid persistent records |
| Verified promotion restart | New generation recovered |
| UINT32_MAX | Mutation refused without writes |
| Bad schema/length/reserved/mode/credential | Rejected, including recomputed-valid-CRC malformed records |
| Truncation / mutation / trailing bytes / oversized blob | Rejected with cleared decode outputs |
| Cancel before execution | ABORTED; delayed completion cannot write |
| Wrong ID / generation / stale ticket | No writes |
| UNKNOWN plus another candidate | New stage refused; clear does not discard verification data |
| Backend reopen fails | No false recovery, mutations remain blocked |
| Accepted transaction integration | Promotion gate wins later cancel; verified late completion commits |
| Default adapter build | No NVS writes/commits; read-only handles |
| Synthetic adapter stub build | Capacity check, write/commit API path, uncertainty latch and handle cleanup pass |

The NVS stub header is only on the host adapter test include path. It is never
used in the real ESP-IDF compilation. Synthetic interrupted persistence is
**HOST_MOCK_VALIDATION**, not **HARDWARE_POWER_LOSS_VALIDATION**.

## Executed validation commands

Working directory for the following commands:
`/home/aditya/esp/projects/esp-otbr/esp-thread-br/examples/basic_thread_border_router`.

```bash
bash components/gateway_wifi_foundation/test/run_storage_tests.sh
SANITIZE=1 bash components/gateway_wifi_foundation/test/run_storage_tests.sh
```

Both pass. The script builds/runs all six existing M4W.2A programs plus storage
tests with `cc -std=c11 -Wall -Wextra -Werror -pedantic`, then the default-lock
and synthetic NVS adapter tests, then `test_vectors.py`. Sanitized mode adds
`-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie` and
`ASAN_OPTIONS=detect_leaks=0`. Existing codec qualification: 351 checks;
transaction qualification: 60 checks; deterministic mutation seed `0x09102a01`,
100,000 iterations; two published SHA-256 vectors pass. LeakSanitizer is not
claimed. Final normal artifacts: `/tmp/m4w2b-tests.9NSh4S`; final sanitized
artifacts: `/tmp/m4w2b-tests.VS1Gr5`.

```bash
source /home/aditya/esp/esp-idf/export.sh >/tmp/m4w2b-idf-export.log 2>&1 && idf.py -C components/gateway_wifi_foundation/test/idf_compile -B /tmp/m4w2b-idf-build -D SDKCONFIG=/tmp/m4w2b-idf-sdkconfig -D IDF_TARGET=esp32s3 build >/tmp/m4w2b-idf-build.log 2>&1
git diff --check
```

**REAL_ESP_IDF_NVS_COMPILATION:** ESP-IDF v5.5.2 / ESP32-S3 compile, archive,
link, binary generation and isolated app-size check pass. All storage sources
compile against the real NVS headers/libraries. Initial SDK compilation rejected
`-pedantic` because vendor newlib uses `#include_next`; adapter C11 warnings stay
strict while the portable sources retain `-pedantic`. No source/header stubs
were substituted. The inert `app_main` performs no initialization or operations;
unused APIs may be linker garbage-collected. This establishes compilation,
not NVS execution. All generated build/config artifacts are in `/tmp`.

**HARDWARE_POWER_LOSS_VALIDATION:** NOT_RUN; prohibited by task scope.

## Exact file changes

All paths relative to `components/gateway_wifi_foundation/`:

- Modified `README.md`: link isolated storage documentation and test entry.
- Added `wifi_store.h`, `wifi_store.c`: portable record/storage implementation.
- Added `wifi_store_nvs.h`, `wifi_store_nvs.c`: real ESP-IDF NVS boundary.
- Added `test/test_storage.c`: record, crash, fencing and integration tests.
- Added `test/test_nvs_adapter.c`, `test/nvs_stub/nvs.h`: adapter mock tests.
- Added `test/run_storage_tests.sh`: strict C11 and sanitizer validation runner.
- Added `test/idf_compile/CMakeLists.txt`, `test/idf_compile/main/CMakeLists.txt`,
  `test/idf_compile/main/compile_only.c`: isolated inert SDK compilation project.
- Added `STORAGE_REPORT.md`: this implementation/audit report.

## Remaining gates

Independent M4W.2B review and acceptance remain required. Before production:
qualify protected storage and security/rollback policy, inventory actual shared
NVS capacity, measure flash timing and resource use, implement controlled
restart recovery for uncertain vendor errors, verify hardware power-cut behavior
under separate authorization, and supply the serialized asynchronous runtime
owner/association integration. No new production registration is authorized.
M4W.3 is not started.
