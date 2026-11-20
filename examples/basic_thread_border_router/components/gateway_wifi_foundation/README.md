# ADR-091 Gateway Wi-Fi foundation

M4W.2B adds an isolated two-slot storage core and an ESP-IDF NVS adapter, with
production registration absent and NVS writes locked off by default. See
[STORAGE_REPORT.md](STORAGE_REPORT.md) for the ownership audit, encoding,
failure matrix, exact validation commands and remaining qualification gates.
Run `bash components/gateway_wifi_foundation/test/run_storage_tests.sh` from
the Gateway example; prefix with `SANITIZE=1` for ASan + UBSan. The historical
M4W.2A description below applies to the codec/transaction foundation.

Host-only development code: no ESP-IDF component registration, command dispatch, Wi-Fi driver, BLE, NVS, Thread, or security authority. It parses business payloads without implementing the Operation Result Envelope. Authorization decisions are supplied by the future owner; BLE connection-generation binding remains an M4W.3 gate. Do not use product credentials.

The asynchronous store is responsible for the durable-promotion linearization point. Before reporting COMMITTED, the state machine reloads the verified authoritative record and checks its generation, SSID, security mode, and credential against the candidate. A failed completion is fenced via cancel_commit before rollback; clear only wipes volatile data. restart must fence outstanding mock operations before reboot recovery and may be invoked with a reset clock. `WIFI_CANCEL_ABORTED` guarantees promotion was fenced; `WIFI_CANCEL_DURABLE` means promotion and readback verification finished; `WIFI_CANCEL_UNKNOWN` means neither outcome is asserted and the state machine remains pending. Completions carry a copied transaction ID, previous generation, and monotonic ticket. The owner serializes callbacks, ensures the state machine outlives them, and drains callbacks before reinitializing it. The store must synchronously copy staged data and never retain the pointer. The mock does not demonstrate real NVS crash consistency or confidentiality.

The eight most recent transaction IDs are retained in a bounded ring until carrier loss or reboot. Older entries are evicted; duplicate IDs still in the ring are rejected. This cache is not authorization or complete request replay protection. The caller must consume a fresh Admin grant for every admission, including busy outcomes. VALIDATED and COMMITTING project as PENDING / STABILITY_CHECK, the last exposed nonterminal state, until the verified durable result is known. The response codec and test coverage remain incomplete; in particular, complete cross-field grammar, all failure races, stale callback lifetime, and replay/authorization integration require further verification before claiming M4W.2A complete.

Run from the Gateway example directory:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation components/gateway_wifi_foundation/wifi_codec.c components/gateway_wifi_foundation/wifi_transaction.c components/gateway_wifi_foundation/test/test_wifi.c -o /tmp/m4w_test
/tmp/m4w_test
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation components/gateway_wifi_foundation/wifi_codec.c components/gateway_wifi_foundation/wifi_transaction.c components/gateway_wifi_foundation/test/test_closure.c -o /tmp/m4w_closure
/tmp/m4w_closure
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation components/gateway_wifi_foundation/wifi_codec.c components/gateway_wifi_foundation/wifi_transaction.c components/gateway_wifi_foundation/test/test_review.c -o /tmp/m4w_review
/tmp/m4w_review
```

## Command allocation scan

On the specified worktrees, `git ls-files -z` enumerated tracked files and each readable UTF-8 text file was searched for 0x42 (including C suffixes), decimal 66, and command/reservation terms. Inventory: firmware 1,142 files (1,090 text, 52 binary); Gateway 253 (235 text, 18 binary); Companion 420 (379 text, 41 binary). Binary/non-UTF-8 files were excluded; build output and untracked files were outside this tracked-file scan. The firmware Control Frame header defines 0x3E/0x3F/0x40, and ADR-082 reserves 0x41. ADR-091 proposes 0x42. Generic authentication tests use 0x42 as sample data; other numeric hits include image bytes, packet lengths, and ORE test codes. ADR-037A reserves notification types, a different namespace. No actual conflicting 0x42 command was found in inspected hits, but review of all symbolic aliases and indirect declarations is not complete; production allocation remains provisional.

A subsequent `git grep` of symbolic command definitions, command-ID reservations, and numeric 0x42/66 switch cases across all tracked text found no additional allocation. This is evidence within the searched source trees, not proof concerning binaries, external repositories, computed IDs, or untracked changes. Production registration remains gated.

## Approved ADR-091 Addendum 01 ordering

The sole transaction owner must serialize event admission in order. Stability completion before elapsed 45,000 ms moves to VALIDATED; `PROMOTION_ADMIT` is a separate irreversible gate, and only that event calls the storage stage callback. A cancel or carrier-loss event admitted first prevents the gate. After the gate, cancellation and loss are deferred until a verified durable or fenced nonpromotion outcome. The validation deadline does not apply to storage or delayed completion. The backend receives the candidate ID, target generation, and ticket and must fence or drain operations before instance reuse.

The gate is not durability proof. A successful callback or a DURABLE indication still requires the existing verified committed-record reload and byte comparison. A failed callback triggers an explicit backend fence: ABORTED means all late writes are fenced; DURABLE requires verified readback; UNKNOWN leaves the transaction single-writer blocked until a later definitive outcome or successful restart fence. A disconnected connection cannot project transaction status. This is host-only contract evidence, not real NVS crash-consistency evidence.

Candidate credentials remain in memory while an admitted storage operation is unresolved because verification may still require them; terminal outcomes and successful restart clear them. Such unresolved retention is not acceptable as an unbounded production lifecycle: bounded backend completion and recovery are M4W.2B gates. M4W.3 must prove event queue order, BLE lifetime and one-use Admin admission.

## Host qualification tests

From the Gateway example directory, run the six focused programs and the two
published hash-vector checks:

```sh
for test_name in test_wifi test_closure test_review test_codec_qualification test_transaction_qualification fuzz_request; do
  cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation \
    components/gateway_wifi_foundation/wifi_codec.c \
    components/gateway_wifi_foundation/wifi_transaction.c \
    "components/gateway_wifi_foundation/test/$test_name.c" -o "/tmp/m4w_$test_name" && "/tmp/m4w_$test_name" || exit
done
python3 components/gateway_wifi_foundation/test/test_vectors.py
```

For ASan and UBSan, add `-fsanitize=address,undefined -fno-omit-frame-pointer`
and run the binaries with `ASAN_OPTIONS=detect_leaks=0` when LeakSanitizer is
unavailable. The mutation test uses seed `0x09102a01` and 100,000 iterations;
it is deterministic mutation testing, not coverage-guided fuzzing. The tests
exercise a host mock, not actual NVS, BLE, Wi-Fi, or power-loss behavior.
