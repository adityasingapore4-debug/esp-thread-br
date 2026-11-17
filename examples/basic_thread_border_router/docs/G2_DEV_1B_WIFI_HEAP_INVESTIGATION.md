# G2-DEV-1B Wi-Fi initialization crash investigation

Verdict: **ROOT_CAUSE_PROBABLE**. The null-handle cleanup defect is established
from the installed SDK; exhaustion versus fragmentation of internal memory is
not established without a new FC61 runtime capture. DEV-1B remains incomplete.
No HIL, serial, discovery, flash, reset, partition-selection or persistent-state
operation was performed. Investigation date: 2026-11-17.

## Failing path and evidence

The supplied hardware log reports Wi-Fi queue creation failure before HTTPS:

`app_main -> dev_p4_connect_saved_wifi -> esp_ot_wifi_connect_dev_readonly ->
wifi_connect_common -> example_wifi_start -> esp_wifi_init ->
esp_wifi_init_internal -> Wi-Fi task setup (ic_create_wifi_task / pp_create_task)
-> wifi_create_queue -> xQueueGenericCreate -> pvPortMalloc -> heap_caps_malloc`.

The SDK's private binary initialization chain is only partly source-visible.
The relevant driver operation is independently identifiable in `libpp.a(pp.o)`:
`pp_create_task` requests `_wifi_create_queue(200, 8)`, stores its wrapper in
`s_wifi_queue`, then reads the first word (queue handle) into `xphyQueue`.
There are TWO identical queue-failure strings: one for a NULL wrapper and one
for a NULL inner handle. The supplied short backtrace does not distinguish
those messages directly. The second case matches the subsequent assertion.

Installed IDF 5.5.4 `components/esp_wifi/esp32s3/esp_adapter.c:112`:

- Allocate an 8-byte wrapper using INTERNAL | 8BIT.
- With SPIRAM_USE_MALLOC disabled, assign `xQueueCreate(queue_len,item_size)`
  to its handle and return the wrapper even if that handle is NULL.
- FreeRTOS queue.c allocates one contiguous `sizeof(Queue_t) + len * item_size`
  block through pvPortMalloc, which requires INTERNAL | 8BIT (caps 0x804).
- Offline ELF type inspection proves Queue_t/StaticQueue_t size 84 bytes and
  wrapper size 8 bytes. Thus this queue needs **1,684 contiguous internal bytes**
  in addition to its wrapper and allocator overhead.
- On inner-handle failure, pp_create_task logs the second queue failure and
  cleans up the non-NULL wrapper using `_wifi_delete_queue`.
- `wifi_delete_queue` checks only the wrapper, then calls
  `vQueueDelete(queue->handle)` unconditionally. queue.c:2355 asserts on NULL.

Thus the vQueueDelete assertion is a **secondary cleanup failure**, not the
first failure. The most precise supported first-failure candidate is the
1,684-byte internal queue allocation. An 8-byte wrapper failure instead returns
NULL cleanly from this routine. The queue precedes the Wi-Fi task/semaphore
creation in pp_create_task; the log does not prove that task-stack creation
failed. Other allocations elsewhere may already have failed or been retried.
Heap corruption remains an alternative absent runtime evidence; static analysis
does not establish it. No live free/largest/minimum heap values were supplied.

## Resource configuration and ownership

Observed sdkconfig (unchanged):

- CONFIG_SPIRAM is disabled. Reported physical 2 MiB PSRAM is unavailable to the
  allocator; board PSRAM capacity/wiring has not been verified by this task.
- NimBLE memory mode INTERNAL; host task stack 6,144 bytes.
- OpenThread SDK task stack 8,192 bytes; main stack 5,120 bytes.
- BLE's canonical deferred Join Existing worker requests a 12,288-byte stack
  and two queues. Its existing worker log reports its own stack high-water mark.
- Wi-Fi static RX count 10, dynamic RX/TX limits 32 each, dynamic TX selected,
  management small-buffer count 32, RX BA window 6, Wi-Fi IRAM/RX IRAM enabled.
  Limits are configuration, not proof that every dynamic buffer exists at init.

`idf.py size` reports DIRAM 285,287 / 341,760 bytes (83.48%): BSS 168,208,
IRAM-resident text 95,543 and data 21,536 bytes, with 56,473 unassigned in that
linker accounting region. This is **not live heap availability** and does not
account for all runtime allocations or imply 56,473 usable contiguous bytes.
The ELF's canonical OpenThread instance alone occupies 33,200 static bytes.
The P4 auth transaction table occupies 6,600 static bytes; its preparation
initializes statically allocated transaction/session/association owners, rather
than loading a TLS key or starting a listener. Its BSS cost exists before
app_main; moving preparation earlier cannot recover that static storage.

OpenThread is started once by esp_openthread_start. The firmware's
thread_runtime_attach_existing_instance validates that same instance and
attaches canonical integrations; it does not invoke the alternate 8 KiB
thread_runtime_start task. BLE retains the canonical stack/carrier. P4 TLS
identity generation and HTTPS allocation happen only after Wi-Fi connection,
so they cannot explain this observed pre-listener crash.

Authority reviewed: firmware AGENTS.md and temporary override, PROJECT_STATE,
TRANSPORT_ARCHITECTURE_FINAL, ADR-031, ADR-066 and ADR-070; implementation of
canonical startup, BLE task startup, P2 recovery safeguards and P4 runtime.
No architecture conflict identified in the change.

## Implemented diagnostic image

All diagnostic code is gated by existing development-only
CONFIG_UN_GATEWAY_DEV_P4_TLS_SMOKE; no new production setting is enabled.
The failure hook is registered before NVS initialization and emits ROM output
with unsuccessful allocation size, capabilities and allocator API name. It
neither allocates nor queries heap/task state, and inspects no payload. It logs
every failure, including potentially recoverable fallbacks; the API name is the
heap entry point, not a caller backtrace.

Snapshots use supported heap-capability APIs for internal 8-bit free/largest/
minimum heap, general malloc-capable free heap, and PSRAM total/free/largest.
They also report caller-task stack HWM in ESP-IDF bytes and task count. Samples
are independent and overlapping capability totals must not be added. HWM is
for the caller (normally main), not every BLE/OT/Wi-Fi task. No diagnostic task,
dynamic trace buffer or task enumeration is introduced.

Boundaries: after NVS, Gateway initialization, OpenThread startup, Thread runtime
attachment, BLE startup, P4 auth preparation; immediately before and after the
actual esp_wifi_init call; before HTTPS startup and after its attempt. A
conditional link wrapper delegates unchanged to the actual SDK function and
logs its return if reached. This keeps instrumentation inside the authorized
Gateway example directory; neither SDK nor shared CLI helper is edited.

The SDK assertion may still occur, and then no post-init line is expected.
This image is diagnostic, not a claimed Wi-Fi recovery fix. Do not interpret
a successful build as a hardware fix.

## Minimum correction decision

No startup reorder, service removal, stack reduction, Wi-Fi buffer reduction,
PSRAM enablement or SDK workaround is justified before runtime measurements.
The smallest justified implementation is the diagnostic image above.

After a separately approved capture, compare the failure size/caps and
before-init free/largest values and startup deltas. A matching 1684/0x804
failure supports this exact allocation path. A small largest block with larger
aggregate free heap suggests fragmentation; small aggregate free heap supports
exhaustion. Concurrent allocations mean the boundary snapshot alone is not
an atomic measurement at failure. If those disagree, investigate heap
integrity/callers rather than forcing a memory-setting workaround.

If PSRAM is subsequently verified and authorized for a development profile,
using it for eligible general allocations can relieve internal pressure, but
FreeRTOS queues and default task stacks still require internal RAM. Wi-Fi's
SPIRAM_USE_MALLOC queue branch also explicitly allocates INTERNAL | 8BIT.
NimBLE INTERNAL mode would remain internal unless separately changed. Reserve
internal RAM appropriately and validate DMA/cache behavior. Reducing RX buffers
trades burst/aggregation throughput; changing IRAM optimization trades execution
speed/cache behavior for internal capacity. None of these tradeoffs has been
silently applied. Fixing SDK queue failure handling would remove the assert
but would not create enough memory for Wi-Fi; SDK writes require separate scope.

## Changed files (relative to Gateway example)

- main/dev_heap_diagnostics.c (new)
- main/dev_heap_diagnostics.h (new)
- main/esp_ot_br.c
- main/CMakeLists.txt
- components/universal_node_gateway/test/runtime_stubs/esp_https_server.h
- docs/G2_DEV_1B_WIFI_HEAP_INVESTIGATION.md (this report)

The host stub gained the real SDK's uint16_t port_secure member because the
existing TLS-smoke code uses it and the lifecycle fixture failed compilation
without it. No runtime listener behavior was changed.
Firmware repository and Companion remain unchanged. Architecture/ADRs, sole
OpenThread/Dataset ownership, BLE, Thread, credentials, identity and P4 security
remain unchanged. Saved Wi-Fi still reads wifi_config via NVS_READONLY and uses
esp_ot_wifi_connect_dev_readonly -> wifi_connect_common(..., false), selecting
WIFI_STORAGE_RAM and bypassing credential commits. Both existing development
P2 fail-closed journal and NORMAL-before-OpenThread guards are preserved.

## Executed validation

From the Gateway example unless stated otherwise:

- `source /home/aditya/esp/esp-idf-5.5.4/export.sh; idf.py build`: PASS.
  Both new diagnostics and app_main compiled, linked and generated the image.
  Log: /tmp/gateway-dev1b-build.log.
- `idf.py size`: PASS; /tmp/gateway-dev1b-size.log.
- Build's actual check_sizes.py check: PASS. Binary **1,683,056 bytes**,
  each slot **2,097,152 bytes**, remaining **414,096 bytes (19.75%)**.
- `bash scripts/run_p4_auth_host_tests.sh`: PASS (exit 0).
- `bash scripts/run_p4_https_lifecycle_tests.sh`: initially failed for missing
  port_secure in the host stub; after the stub correction PASS, 26 dispatched
  requests / 21 modeled releases. These are mocks, not genuine IDF runtime.
- `bash scripts/run_p4_https_tests.sh`: sandbox loopback listener failed to
  start; authorized execution outside the sandbox PASS. Synthetic OpenSSL TLS,
  SCRAM isolation, replay, framing, token and rate-limit regressions passed.
  This is not FC61 or ESP-IDF TLS qualification.
- Firmware: `bash scripts/run_host_tests.sh provisioning_v2`: PASS (1 target).
  This exercises the normal host configuration, not the development macro's
  crash-recovery behavior. The development guard was source-reviewed/compiled.
- Non-development diagnostic TU: compiled using its actual compile_commands
  entry with a temporary sdkconfig.h disabling TLS_SMOKE; PASS. Offline nm
  confirms the object has no diagnostic symbols. This is not a full production
  configuration build. Temporary files: /tmp/gateway-dev1b-nondev/.
- Offline objdump of libpp.a and queue code, GDB ELF type queries (no target
  attached), nm static-size inspection, and gen_esp32part.py parsing performed.
  Queue evidence: /tmp/gateway-libpp-disasm.txt and
  /tmp/gateway-dev1b-offline-gdb.log.
- `git diff --check`: PASS.

Image: build/esp_ot_br.bin
SHA-256: e24726c7287be02721ad1a07ac275afa9202482a8bab3743ba6e0c9d93507ecb
Compiled table: ota_0 at 0x20000; ota_1 at 0x220000; each 0x200000 bytes.
A compiled table does not prove the live device table matches.

## Separate approval required: safe ota_1 replacement proposal

1. Obtain explicit FC61 maintenance approval covering bootloader/download-mode
   entry, read-only backup, app-only ota_1 programming/verification, and reboot.
   Confirm live identity, partition map and boot selection/state through the
   supported HIL interface. Save private backups of ota_1 and otadata; preserve
   commissioned NVS/manufacturing/P4/Dataset partitions without displaying
   their contents. Record ota_0 hash and preserve it byte-for-byte.
2. Halt application execution in the ROM download loader using the approved
   supported lifecycle. Do not overwrite an executing ota_1 via an in-app OTA
   operation. Avoid switching to ota_0 merely for convenience: the original
   recovery firmware must first be qualified as safe for current P2 journals.
3. Only if the live map matches, program the reviewed application binary alone
   into ota_1 at 0x220000; verify written bytes/hash. No full `idf.py flash`,
   flash_args, bootloader, partition table, ota_data_initial.bin, SPIFFS/RCP,
   NVS, erase-all, identity or eFuse operation. Automatic reset must be suppressed
   until verification. Use the supported HIL app-only operation; if unavailable,
   stop and agree an explicit supported procedure before any hardware action.
4. Leave otadata unchanged when its verified state already selects valid ota_1.
   If boot selection/rollback state differs, stop for review rather than making
   an implicit OTA metadata change. Reboot only under the maintenance approval.
5. Capture the new boundary/failure lines, establish the allocation diagnosis,
   and implement/rebuild the smallest supported memory correction. A diagnostic
   reboot may still crash; do not mark this image or DEV-1B qualified on that boot.
6. Acceptance after correction: successful actual Wi-Fi initialization, saved
   LAN connection and genuine FC61 TLS handshake on the TLS-only port, plus
   BLE/Thread continuity and unchanged commissioned persistent state. Observe
   internal/largest heap headroom during handshake and repeated connections.
   Preserve ota_0 as recovery throughout. Product authentication/routes remain
   disabled. Do not declare DEV-1B complete before this hardware evidence.
