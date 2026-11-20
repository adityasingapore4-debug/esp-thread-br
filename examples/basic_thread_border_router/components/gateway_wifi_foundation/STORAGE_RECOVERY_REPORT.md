# M4W.2B M3 — Storage recovery coordinator and runtime fencing

2026-11-20. Implementation evidence submitted for independent review. This does
not self-approve M3, complete M4W.2B, or authorize M4W.3.

```text
M4W_2B_M3_RECOVERY:
VERDICT: PARTIAL
RECOVERY_STATE_MACHINE: PASS
SINGLE_WRITER_FENCING: PASS
STALE_WORK_REJECTION: PASS
UNKNOWN_OUTCOME_HANDLING: PASS
BOUNDED_RECOVERY_POLICY: PASS
CONTROLLED_RESTART_CONTRACT: PASS
NVS_UNCERTAINTY_LATCH: PASS
CREDENTIAL_ZEROIZATION: PASS
HOST_FAULT_INJECTION: PASS
HOST_TESTS: PASS
ASAN: PASS
UBSAN: PASS
ESP_IDF_COMPILATION: PASS
REAL_NVS_EXECUTION: NOT_RUN
HARDWARE_POWER_LOSS: NOT_RUN
PRODUCTION_REGISTRATION: DISABLED
M4W_2B_MILESTONE_COMPLETE: NO
```

PASS denotes the isolated contract, deterministic host tests and compilation,
not production runtime qualification. PARTIAL reflects the outstanding actual
worker/owner scheduler, controlled restart orchestration, measured timing,
protected storage and hardware evidence. No architecture conflict identified.

## Baseline and boundaries

Verified clean Gateway branch `dev/g2-dev-1b-lan-bringup` at reviewed baseline
`502f8dece0d723f43f89e7e7a1f4556aabebc250` before edits. Read local ADR-091,
approved Addendum 01, M4W.2A independent acceptance review, project state and
workflow. Architecture checkout now includes M4W.2A acceptance. User's M3 work
order authorizes this isolated implementation; historical activation gates
remain intact. Architecture/protected repositories were not changed.

No transaction FSM, codec, slot encoding, namespace/key, or generation-selection
algorithm was redesigned. `wifi_store_t` is unchanged. Coordinator code wraps
that interface and feeds the existing correlated FSM completion API. No
production task/component/startup, real Wi-Fi/credential ingress, BLE,
authentication, partition, identity, Thread, HTTPS or PSRAM/DMA change occurred.
No reboot, flash/reset, partition erase/deinit, shared-NVS reinitialization,
customer credential write, commit or push occurred.

## M3-A — Recovery contract audit

Audited the specified transaction/store/NVS files and tests, and the existing
M1/M2 report. These are the findings and implemented dispositions:

| Condition | In-process recovery / disposition |
|---|---|
| Queued, not executing | Atomically invalidate dispatch before worker claim; backend cancellation proves ABORTED. No NVS operation was issued. |
| Synchronous write executing | Owner can request recovery/restart without waiting, but cannot claim a successful fence, wipe backend buffers, reopen handles or reinitialize storage. Wait for return and acquire/drain DONE. |
| Write failed before persistence | API failure alone does not prove this. A qualified mock boundary can fence/reopen and prove previous authority; real NVS set errors latch uncertainty and require actual controlled restart. |
| Write failed after persistence | Initially UNKNOWN. A qualified boundary may prove the new authoritative candidate. Real NVS set errors remain restart-required even when candidate bytes might already exist. |
| Commit failed | UNKNOWN; real adapter latches restart-required. No inference of nonpersistence. |
| REMOVE_FAILED | NVS subsystem recovery may be needed. A handle reopen or fresh adapter object is insufficient; quiesce then request controlled restart. |
| Readback failed after successful write/commit | UNKNOWN. If no uncertainty latch, bounded worker reopen + independent slot validation can recover DURABLE or ABORTED. |
| Boundary reopen failed | Never successful recovery. Attempts count toward policy; exhausted attempts/deadline require restart. |
| UNKNOWN with candidate in RAM | Keep only the backend verification candidate plus the FSM's accepted candidate; coordinator jobs/status contain no credential copies. Block mutations and run bounded policy. |
| Restart requested with queued work | CAS invalidates queued capability before cancelling its backend RAM operation. Delayed scheduler copies cannot execute. |
| Restart requested during synchronous work | Report nonquiescent; do not cancel flash. When call returns, drain its result, invalidate delivery and scan or escalate. |
| Stale execution/completion | Reject full epoch, dispatch stamp, transaction ID, generation and ticket mismatch; consumed completions cannot deliver twice. |

This audit does not repeat the previous partition/ownership survey. The default
`gw_wifi` A/B format and production write lock remain as reviewed in M1/M2.

## Lifecycle and ownership

```text
Gateway Wi-Fi Owner (sole serialized event/admission authority)
    | fresh external authorization; coordinator availability check
    v
Accepted Transaction FSM -- PROMOTION_ADMIT --> wifi_store_t wrapper
                                                   |
                                            Recovery Coordinator
                                              /             \
                           copied metadata job               policy/status
                                    |                             |
                           Storage Worker seam           RESTART_REQUIRED
                                    |                      + quiescence
                            M1/M2 storage core                    |
                                    |                    Future Gateway
                               NVS adapter               restart orchestrator
                                    |
                               Slot A / B
```

The coordinator is policy and scheduling glue. It does not authorize APPLY,
encode a parallel record, call NVS APIs directly, create a task, or restart the
system. `wifi_recovery_start_result` checks storage availability before calling
the unchanged FSM, including when that FSM is idle during recovery. The future
owner must still consume a fresh authorization grant for every admission,
including BUSY/rejected outcomes.

States are internal; no business/wire state was added:

```text
IDLE -> QUEUED_NOT_EXECUTING -> EXECUTING
                                  |
              +-------------------+-------------------+
              v                   v                   v
       DURABLE_VERIFIED   NOT_PROMOTED_FENCED    OUTCOME_UNKNOWN
                                                       |
                                               RECOVERY_REQUIRED
                                                       |
                              fence / drain -> qualified worker recovery
                                                       |
                       DURABLE / ABORTED / bounded retry / RESTART_REQUIRED
```

Definitive outcomes are delivered with copied original operation identity via
`wifi_recovery_completion` and `wifi_recovery_deliver`. The latter checks epoch,
dispatch identity, FSM phase, generation, ID and ticket before using the accepted
FSM callback; DURABLE still undergoes its independent authoritative reload.
UNKNOWN has no definitive callback. New candidates are blocked until definitive
delivery or completed boot recovery. Read/restore callbacks may read NVS and
must execute on the future owner task, never the BLE host task. Polling,
restart requests and admission checks perform no NVS calls.

## M3-B/C — Budget and exact fencing guarantees

The caller must supply nonzero `max_attempts` and `max_elapsed_ms` and an injected
clock. There are no defaults or production timeout values. Host tests use
three attempts / 100 synthetic milliseconds to exercise boundaries, not to
recommend production settings. The operation's budget starts at staging;
idle/definitive explicit restart requests start their own budget. Repeated
restart requests never reset an unresolved budget. Reopen/recovery or restart
scan dispatch consumes one attempt; each dispatch runs at most the existing
one-reopen/two-slot-scan operation, without retries or writes of its own.

The owner calls `wifi_recovery_poll` on scheduling/clock/completion events.
Exhaustion, backward clock, clock wrap or dispatch-stamp exhaustion fail closed.
Late recovery/scan results do not reopen admission after the deadline. A primary
write already verified durable may be accepted on return if no prior observed
deadline/restart fence invalidated its delivery; the validation wire deadline
was not extended or repurposed. Once RESTART_REQUIRED is latched, repeated polls
perform no recovery I/O. No potentially blocking flash call can be forcibly
stopped by this policy: a timed-out running worker remains nonquiescent until
it actually returns. Runtime watchdog/escalation and measured vendor timing
remain required; this is not a production wall-clock guarantee.

The injectable scheduler boundary is `wifi_recovery_job` (owner copies a job),
`wifi_recovery_worker_run` (worker claims/runs it), then owner poll/drain. Queues
must store job values rather than pointers to mutable job storage. One acquire/
release atomic mailbox holds IDLE, QUEUED, RUNNING or DONE plus a nonreused
dispatch stamp. A stale worker cannot claim a later dispatch. A matching stamp
also requires the full copied identity, including owner epoch, before backend
access. A concurrent queue fence wins the same CAS against worker claim; only
the winning side may touch the backend. A running operation cannot be falsely
fenced by a mutex, cancellation request or timeout.

DONE publication is release; owner acquisition drains that sole completion
mailbox before reuse. Pending old deliveries are invalidated by restart policy,
and original operation IDs are cleared after successful boot scan. The storage
object admits only one coordinator attachment; live coordinator initialization
is rejected. Coordinator/backends must outlive their work; raw backend APIs
must not be used after attachment, and raw `wifi_storage_init` is still a fresh-
object/fully-drained-lifetime operation. The future scheduler must honor these
ownership preconditions. No firmware worker registration is present.

## M3-D — Controlled restart and boot contract

The real adapter now has a sticky process/NVS-lifetime uncertainty latch in
addition to the instance flag. Any failed actual `nvs_set_blob` or `nvs_commit`
sets it. Reads, writes, commits and reopen refuse an uncertain lifecycle, and
`wifi_nvs_restart_required` exposes only the boolean requirement. Close does
not reset the latch. New adapter construction inherits it, including for
adapters that existed before the error. There is no runtime clear API. The
query and adapter operations share the single storage-worker serialization.

Real write/commit errors, including REMOVE_FAILED, therefore lead directly to
RESTART_REQUIRED after draining the worker. Unqualified repeated reopen errors,
invalid/unverifiable authority, budget/deadline exhaustion, epoch/stamp
exhaustion, or a failed definitive FSM reload also lead to restart-required.
The explicit status includes `quiescent` and `candidate_retired`: an orchestrator
must not confuse RESTART_REQUIRED while a worker is active with a safe completed
restart fence. This milestone only returns status; it never performs a reboot.

A qualified boundary can instead complete an in-process configuration-only
restart scan after its workers are fenced. Successful scan advances owner
epoch, discards operation metadata, and exposes `boot_ready`. Admission stays
blocked until the owner passes the prepared restart through the existing FSM
REBOOT event, which clears candidate/replay/transaction state and reloads
configuration. No session/Admin capability is reconstructed. This path cannot
clear an uncertain real NVS lifecycle and does not substitute for actual reset.

Actual future boot must have the existing shared-NVS owner initialize NVS,
then create fresh adapter/storage/coordinator/FSM objects with a fresh nonzero
owner epoch supplied externally. Only persistent A/B records survive. No pending
candidate, transaction result, authorization or BLE state is loaded. A fresh
epoch is mandatory to reject an old job even if its ticket, ID and dispatch
number happen to repeat. Host tests explicitly apply that collision pressure.
Actual boot/session/restart integration remains outside this milestone.

## M3-E — Credential lifetime

The coordinator's work, operation and completion records contain metadata only;
status contains state, epoch, budget and lifecycle booleans. No credential is
logged or returned in diagnostics. Existing encoding/readback/scan buffers keep
their explicit zeroization. Definitive completion wipes backend candidate RAM;
the accepted FSM wipes its candidate on delivered success/failure. Successful
configuration restart wipes backend candidate and the owner subsequently applies
the existing FSM reboot cleanup.

If restart is required, the coordinator first fences/drains, then calls the new
`wifi_storage_retire_for_restart`: wipe operation RAM, leave storage unavailable
and UNKNOWN, preserve committed slots and the NVS latch. This is not ABORTED
or DURABLE. The serialized owner calls `wifi_recovery_retire_transaction` once
status reports the completed fence; it wipes the FSM candidate, keeps its
unresolved exclusion, and disables transaction projection. This also works for
a candidate that had not yet reached promotion. The helper cannot run while
the worker still owns its buffers. Original accepted identity remains in
coordinator metadata until completed boot recovery; no secret copy is needed
after escalation.

The actual runtime must service poll/retirement/restart and measure the longest
retention interval. A stuck synchronous call cannot be safely wiped underneath
its worker. Neither ordinary RAM clearing on reboot nor these host checks prove
production credential-lifetime compliance; activation remains blocked.

## M3-F — Deterministic fault matrix

Host recovery tests include 40 failures across eight alternating prior
generations and six actual pthread interleavings stopped deterministically
inside the synchronous write boundary. No sleeps or real flash operations are
used. Assertions compare the full previous authoritative slot with pre-fault
bytes. The existing M1/M2 84-case fault matrix is retained unchanged.

| Scenario | Observed host result |
|---|---|
| Queued cancellation | ABORTED, queued job invalidated, delayed dispatch rejected, both candidates wiped on delivery |
| Running write success | Exact verified DURABLE; normal delivery commits through unchanged FSM |
| Running write ambiguous failure | UNKNOWN, candidate retained only for bounded verification |
| UNKNOWN + new candidate | Coordinator rejects; FSM reports BUSY; no second write |
| Repeated failed recovery | Exactly configured three attempts, then restart-required; 100 repeated polls/request calls perform no additional I/O |
| Reopen failure | No false recovery or boot-ready signal |
| REMOVE_FAILED equivalent | Immediate restart-required after worker drain, no retry bypass |
| Restart requested with queued work | Old job fenced before any write; worker restart scan restores authority |
| Restart during active write | Nonquiescent until actual return; no early buffer wipe or restart success |
| Deadline during active write | Restart requirement latched but fence withheld until return |
| Recovery result arrives at deadline | Remains restart-required, no reopened admission |
| Stale worker after recovery | Old stamp/ID/generation/ticket rejected without writes |
| Stale completion after epoch advance | Rejected, even with repeated ID/ticket/stamp |
| Error reported after persistence | Qualified recovery verifies new authoritative record |
| Definitely nonpromoted / torn inactive slot | Qualified recovery proves ABORTED and retains previous slot |
| Valid higher / torn higher / both invalid | Highest valid / previous valid / boot fails safely |
| Unknown mismatching higher record | No invented success; exhaust budget and retire verification data |
| Terminal/fenced candidate lifetime | Backend and FSM candidate buffers zero after safe completion/retirement |
| Idle FSM during recovery | Availability adapter returns BUSY before candidate admission |
| Fresh adapter in same uncertain process | Remains latched; old and new instances cannot read/reopen/write |
| Simulated actual boot | Persistent configuration only; idle transaction, empty replay/ID/candidate, no authorization resurrection |
| Second coordinator / live reinit | Rejected |
| Backward/wrapped clock | Fail closed without flash mutation |

The adapter's former test that allowed fresh object construction to resume
after a write error was strengthened to require continued rejection. Write-
error and commit-error tests now run in separate host processes so neither
needs to clear the process latch. All former record/codec/FSM assertions remain
unchanged, including 4,431 storage checks and the original six host programs.

## Validation evidence

All commands run from:
`/home/aditya/esp/projects/esp-otbr/esp-thread-br/examples/basic_thread_border_router`.

```bash
bash components/gateway_wifi_foundation/test/run_storage_tests.sh
SANITIZE=1 bash components/gateway_wifi_foundation/test/run_storage_tests.sh
source /home/aditya/esp/esp-idf/export.sh >/tmp/m4w2b-m3-idf-export.log 2>&1 && idf.py -C components/gateway_wifi_foundation/test/idf_compile -B /tmp/m4w2b-m3-idf-build -D SDKCONFIG=/tmp/m4w2b-m3-idf-sdkconfig -D IDF_TARGET=esp32s3 build >/tmp/m4w2b-m3-idf-build.log 2>&1
git diff --check
git status --short
```

- **HOST_RECOVERY_SIMULATION:** PASS. 1,557 recovery checks, 4,431 existing storage
  checks, all six M4W.2A programs, locked/synthetic NVS stub tests and both hash
  vectors pass. Portable sources compile with C11 `-Wall -Wextra -Werror
  -pedantic`; pthread links only host tests. Sanitized run adds ASan/UBSan and
  disables LeakSanitizer; no ThreadSanitizer claim is made.
- **ESP_IDF_COMPILATION:** PASS. Isolated ESP32-S3 / ESP-IDF v5.5.2 compiles the
  coordinator, core and NVS adapter against real SDK headers, then produces
  the inert ELF/bin and passes app size checks. The image is `0x277d0` bytes.
  NVS writes remain disabled. Unused functions may be linker garbage-collected;
  this is compile evidence, not execution. Portable core/coordinator retain
  `-pedantic`; vendor newlib's `#include_next` requires the existing adapter
  diagnostic exception. Host NVS stubs are absent from the SDK include path.
- **REAL_NVS_EXECUTION:** NOT_RUN.
- **HARDWARE_POWER_LOSS:** NOT_RUN. No hardware operations authorized/performed.

All generated build, sdkconfig, logs and test binaries are in `/tmp`. The
isolated compile project remains unregistered in production.
Final normal host artifacts: `/tmp/m4w2b-tests.m7BpvP`; final ASan/UBSan
artifacts: `/tmp/m4w2b-tests.eisnAc`. `git diff --check` and separate whitespace
checks for the four new files pass; final inventory is ten modified and four
added files, all in the authorized foundation scope.

## Exact files changed

Paths relative to `components/gateway_wifi_foundation/`:

| File | Change |
|---|---|
| `wifi_recovery.h` | New coordinator, copied jobs/completions, policy/status and owner/worker APIs |
| `wifi_recovery.c` | New bounded policy, atomic claim/fence/mailbox, epoch checks, FSM delivery and retirement glue |
| `wifi_store.h` | Single coordinator attachment flag and fenced retirement declaration |
| `wifi_store.c` | RAM-only, fail-closed retirement after completed worker fence |
| `wifi_store_nvs.h` | Restart-required query declaration and lifecycle contract |
| `wifi_store_nvs.c` | Sticky process uncertainty, replacement protection, refuse uncertain reads as well as mutation/reopen |
| `test/test_recovery.c` | New deterministic recovery/failure/host-thread/boot suite |
| `test/test_nvs_adapter.c` | Strengthened same-process latch assertions and independent write/commit failure processes |
| `test/nvs_stub/nvs.h` | Synthetic REMOVE_FAILED constant |
| `test/run_storage_tests.sh` | Add recovery suite, coordinator compilation, pthread linkage and separate commit-failure adapter run |
| `test/idf_compile/main/CMakeLists.txt` | Compile coordinator against real SDK with strict portable diagnostics |
| `README.md` | Link recovery lifecycle/report |
| `STORAGE_REPORT.md` | Historical M1/M2 pointer to stronger M3 lifecycle contract |
| `STORAGE_RECOVERY_REPORT.md` | This audit, implementation and validation report |

## Remaining qualification gates

Independent M3 review remains required. Production needs the concrete serialized
event owner and worker scheduler, fresh boot-epoch allocation, measured policy
values and operation timing, regular poll/retirement servicing, a controlled
Gateway restart orchestrator that respects quiescence, live capacity and
protected-storage qualification, real ESP-IDF NVS fault execution and hardware
power-cut evidence. An unreturning vendor flash call still requires an approved
system-level reliability policy. M4W.2B is not complete. Production registration
is disabled; M4W.3 was not begun.
