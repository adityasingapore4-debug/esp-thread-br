# ADR-091 Gateway Wi-Fi foundation

Host-only development code: no ESP-IDF component registration, command dispatch, Wi-Fi driver, BLE, NVS, Thread, or security authority. It parses business payloads without implementing the Operation Result Envelope. Authorization decisions are supplied by the future owner; BLE connection-generation binding remains an M4W.3 gate. Do not use product credentials.

The asynchronous store is responsible for the durable-promotion linearization point. `WIFI_CANCEL_ABORTED` guarantees promotion was fenced; `WIFI_CANCEL_DURABLE` means promotion and readback verification finished; `WIFI_CANCEL_UNKNOWN` means neither outcome is asserted and the state machine remains pending. Completions carry a copied transaction ID, previous generation, and monotonic ticket. The owner serializes callbacks, ensures the state machine outlives them, and drains callbacks before reinitializing it. The store must synchronously copy staged data and never retain the pointer. The mock does not demonstrate real NVS crash consistency or confidentiality.

Eight recent transaction IDs are retained until carrier loss or reboot; the ninth is rejected. This bounded policy and connection ownership require integration review. The response codec and test coverage remain incomplete; in particular, complete cross-field grammar, all failure races, stale callback lifetime, and replay/authorization integration require further verification before claiming M4W.2A complete.

Run from the Gateway example directory:

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation components/gateway_wifi_foundation/wifi_codec.c components/gateway_wifi_foundation/wifi_transaction.c components/gateway_wifi_foundation/test/test_wifi.c -o /tmp/m4w_test
/tmp/m4w_test
cc -std=c11 -Wall -Wextra -Werror -pedantic -Icomponents/gateway_wifi_foundation components/gateway_wifi_foundation/wifi_codec.c components/gateway_wifi_foundation/wifi_transaction.c components/gateway_wifi_foundation/test/test_closure.c -o /tmp/m4w_closure
/tmp/m4w_closure
```

## Command allocation scan

On the specified worktrees, `git ls-files -z` enumerated tracked files and each readable UTF-8 text file was searched for 0x42 (including C suffixes), decimal 66, and command/reservation terms. Inventory: firmware 1,142 files (1,090 text, 52 binary); Gateway 253 (235 text, 18 binary); Companion 420 (379 text, 41 binary). Binary/non-UTF-8 files were excluded; build output and untracked files were outside this tracked-file scan. The firmware Control Frame header defines 0x3E/0x3F/0x40, and ADR-082 reserves 0x41. ADR-091 proposes 0x42. Generic authentication tests use 0x42 as sample data; other numeric hits include image bytes, packet lengths, and ORE test codes. ADR-037A reserves notification types, a different namespace. No actual conflicting 0x42 command was found in inspected hits, but review of all symbolic aliases and indirect declarations is not complete; production allocation remains provisional.
