# ADR-091 M4W.3C standalone host composition

This directory contains an **offline qualification fixture**, not a firmware component or ingress. The compilation guard requires `ADR091_STANDALONE_HOST_TEST`. No production or development communication path references it. `service.c` composes the existing codec, transaction, storage and recovery owners; `test_service.c` supplies a synthetic in-memory two-slot backend, synthetic credentials, scan results, clock and explicit radio/storage events. It never calls a Wi-Fi driver or NVS. The synthetic candidate helper passes the foundation's `authorized` test input solely to exercise the host FSM. It is not an authorization interface, and must not be wired to any transport. No authentication or carrier-lifetime behavior is implied.

A single caller must serialize all service operations, including promotion admission, cancellation and worker/poll progression. The harness deliberately does not create a queue, tasks or callbacks: call order defines fixture event order. Explicit promotion is separate from validation. Rollback completions carry a copied transaction ID, prior generation, ticket and recovery epoch; stale and repeated completions are rejected. The caller must deliver timeout events, storage jobs and rollback completions; no real association is attempted. Storage rollback validates retention of the prior committed record, not reconnection to an access point. Status reports committed configuration as disconnected because no radio is present. Scan results are bounded to eight, with a 10-second pending deadline and a 30-second cache lifetime; the synthetic caller explicitly starts and completes a scan.

The recovery coordinator blocks replacement on an unknown outcome. Restart must fence and drain the worker before its verified slot scan; transaction result state is discarded. The service is not an implementation of the five authorized ingress operations or their ORE responses. Same-generation authorization, ordered carrier-loss delivery, serialized ownership, secret lifetime and asynchronous worker scheduling across the real boundaries remain unimplemented integration requirements. This fixture does not establish production storage durability, Wi-Fi or BLE behavior.

From `examples/basic_thread_border_router`:

```sh
bash components/gateway_wifi_foundation/test/standalone/run.sh
SANITIZER=address bash components/gateway_wifi_foundation/test/standalone/run.sh
SANITIZER=undefined bash components/gateway_wifi_foundation/test/standalone/run.sh
bash components/gateway_wifi_foundation/test/run_storage_tests.sh
```

Seven service cases cover status and scan; commit and restart; connection loss, IP and connection timeout, cancellation and rollback; cancellation before and after promotion; unknown outcome and restart; validation deadline and delayed storage; and restart fencing and stale work. Both sanitizers run the new service and all four composed foundation source files. These results are offline host evidence only.
