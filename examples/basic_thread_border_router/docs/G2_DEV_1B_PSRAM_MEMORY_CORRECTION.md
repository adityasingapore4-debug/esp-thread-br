# FC61 DEV-1B PSRAM memory correction candidate

Verdict: **PARTIAL**. Root cause **CONFIRMED from user-supplied FC61 hardware
evidence**; development correction implemented and software validation passed.
No new hardware operation was performed. Wi-Fi is not yet verified fixed and
DEV-1B is not complete. Date: 2026-11-17.

## Confirmed failure

Supplied measurements (internal 8-bit heap, bytes):

| Boundary | Free | Largest when supplied |
| --- | ---: | ---: |
| NVS | 150016 | — |
| Gateway | 123988 | — |
| OpenThread | 88752 | — |
| Thread integration attachment | 88428 | — |
| BLE | 3332 | 1664 |
| P4 preparation | 3332 | — |
| Before Wi-Fi | 3332 | 1664 |

The heap failure callback reports 1684 bytes / caps 0x804 / heap_caps_malloc.
That matches the compiled driver's 200 x 8-byte Wi-Fi queue plus 84-byte
FreeRTOS queue structure, requiring INTERNAL | 8BIT. Aggregate free heap is
already critically low, and its largest block is 20 bytes smaller than this
allocation. This is internal-memory exhaustion with insufficient contiguous
space, not evidence of a general-heap or PSRAM capacity failure. PSRAM was
unavailable (total/free zero). SDK wifi_create_queue returns a wrapper with a
NULL queue handle, then cleanup calls vQueueDelete(NULL). That assertion is
secondary. P4 preparation shows no additional heap decrease.

## Hardware-supported interface

The user confirmed FC61's marking **ESP32-S3-N8R2**. Espressif's N8R2 module
specification lists 8 MB flash and **2 MB Quad SPI PSRAM**:
[ESP32-S3-WROOM-1 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf).
The series [datasheet](https://documentation.espressif.com/esp32_s3_datasheet_en.pdf)
also identifies embedded 2 MB PSRAM as Quad SPI. Quad mode is based on those
specifications and the supplied marking, not the IDF default or an R8 board.
No live identification/PSRAM detection was performed here.

Select conservative **40 MHz Quad STR**, auto-detected PSRAM type, normal boot
initialization, memory protection and the SDK memory test. Local IDF 5.5.4
`docs/en/api-guides/flash_psram_config.rst` supports this with the existing
80 MHz Quad/DIO flash clock. No flash mode, flash frequency, voltage/eFuse,
PSRAM pins or hardware wiring is changed. Standard S3 CS/clock pins remain
26/30. An initialization failure aborts; ignore-not-found is disabled.

## Allocation plan and exact configuration

The opt-in overlay is `config/dev1b-psram.defaults`, applied after the existing
DEV-1B sdkconfig. `sdkconfig.defaults` and the original sdkconfig are unchanged.
The resolved configuration is preserved in
`build-dev1b-psram/sdkconfig.resolved`; complete before/after symbol differences
are `/tmp/gateway-dev1b-psram/config-diff.json`.

| Setting | Candidate |
| --- | --- |
| UN_GATEWAY_DEV_PSRAM | y; depends on development TLS smoke and SPIRAM |
| SPIRAM / MODE_QUAD / TYPE_AUTO | y / y / y |
| SPIRAM_SPEED_40M / SPEED | y / 40 |
| SPIRAM_BOOT_HW_INIT / BOOT_INIT / MEMTEST | y / y / y |
| SPIRAM_IGNORE_NOTFOUND | n |
| SPIRAM_USE_MALLOC | y |
| SPIRAM_MALLOC_ALWAYSINTERNAL | 1024 |
| SPIRAM_MALLOC_RESERVE_INTERNAL | 32768 |
| BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL | y; INTERNAL=n |
| MBEDTLS_DEFAULT_MEM_ALLOC | y; INTERNAL=n |
| SPIRAM_TRY_ALLOCATE_WIFI_LWIP | n |
| FREERTOS_TASK_CREATE_ALLOW_EXT_MEM | n |
| SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY / ALLOW_NOINIT_SEG_EXTERNAL_MEMORY | n / n |
| SPIRAM_FETCH_INSTRUCTIONS / RODATA | n / n |
| OPENTHREAD_PLATFORM_MALLOC_CAP_SPIRAM / PLATFORM_MSGPOOL_MANAGEMENT | n / n |
| FATFS_ALLOC_PREFER_EXTRAM | n |

The last three policy groups explicitly prevent newly visible PSRAM defaults
from silently changing unrelated allocation policies. SDK-selected PSRAM sleep
leakage and S32C1I atomic workarounds and normal memory-protection settings are
retained. Legacy renamed sdkconfig aliases reflect the same selections.

| Allocation | Placement / limitation |
| --- | --- |
| NimBLE host malloc/calloc, including eligible GATT/HCI/MSYS pools | Strict PSRAM 8BIT through the supported external option; no internal fallback for those calls |
| Ordinary malloc/calloc/realloc, including eligible Wi-Fi/lwIP data | Over 1024 bytes prefer PSRAM, smaller requests prefer internal; SDK fallback if preferred memory unavailable |
| Mbed TLS dynamic allocations | Supported default calloc uses the same policy; large TLS/crypto buffers become eligible for PSRAM |
| Ordinary dynamic FreeRTOS stacks, TCBs, queues, semaphores | Internal; ordinary task creation unchanged |
| Bluetooth controller and DMA-specific allocations | Internal/DMA-capable; host external mode does not override these |
| Wi-Fi static RX, hardware/DMA/internal-specific buffers, driver queue/task | Internal; cannot be solved by treating all Wi-Fi allocations as external |
| Canonical static data and OpenThread message pool | Existing internal/static policy retained |

32 KiB is the IDF supported default internal reserve, isolated from ordinary
malloc use for internal/DMA-specific allocations. It is **not** a guaranteed
32 KiB free floor or one contiguous block; tasks and driver objects consume it.
The 1 KiB threshold moves sizeable general data away before internal memory is
exhausted while keeping small allocations local. It trades some cache latency
for internal headroom. It does not override explicit capabilities.

Mbed TLS previously selected INTERNAL and therefore bypassed normal malloc
policy. The development-only default mode allows large handshake buffers to
use PSRAM; no ciphers, certificate checks, authentication, TLS limits, key-use
rules or routes are removed. External memory contains sensitive transient TLS
state and is not encrypted in this existing insecure development profile.
This allocator selection must not be promoted to a production security profile
without that profile's memory-protection review. No production key provider,
security-root ownership, eFuse or flash-encryption setting was changed.

## BLE audit

The measured BLE boundary decreases internal free heap by **85096 bytes**
(83.10 KiB). It covers controller initialization, host initialization, GATT and
security-store registration, the canonical deferred Join Existing worker and
queues, NimBLE host task and event subscriptions. It is not exclusively NimBLE
host memory. No per-allocation hardware trace was supplied, so exact controller
versus host totals and recovered internal bytes remain unmeasured.

- `ble_cmd_task_init` creates a **12288-byte** deferred worker stack plus two
  queues; `nimble_port_freertos_init` creates the **6144-byte** host stack.
  Together these explain 18432 bytes of stack requests, excluding TCBs and
  allocator overhead. Both remain internal and are unchanged.
- IDF `bt/porting/mem/bt_osi_mem.c` routes host malloc/calloc in external mode to
  SPIRAM | 8BIT. `esp_nimble_mem.h` maps NimBLE platform host calls to it.
  Explicit internal allocation functions still request INTERNAL | 8BIT | DMA.
- Configured MSYS capacity remains **12 x 256 + 24 x 320 = 10752 bytes** of
  block payload capacity, plus metadata/alignment. On this S3 build the
  non-controller MSYS path allocates the backing pools using NimBLE host
  calloc, making those pools eligible to move. This is capacity analysis,
  not a measured 10752-byte heap saving.
- ATT/GATT entries, HCI contexts and host protocol pools also allocate through
  host APIs; realloc uses the ordinary allocator. Fixed/static data remains
  fixed/static. Controller archives are not fully source-visible, so this
  task does not invent an exact controller byte count.
- Connections=3, bonds=3, CCCDs=8, MTU=256, prepare entries=64, GATT procedures=4,
  MSYS block counts/sizes, all enabled BLE roles and advertising capabilities
  are preserved. Bonding, MITM and Secure Connections remain canonical. No
  buffer or connection limit was reduced, and on-demand pool allocation was
  not enabled.

Expected relief is relocation of eligible host/general allocations, not an
asserted 85 KiB saving. BLE provisioning, bonding and authentication must still
be exercised physically while Wi-Fi and TLS run.

## Wi-Fi and coexistence audit

Keep static RX=10, dynamic RX=32, dynamic TX=32, RX BA window=6 and dynamic TX
mode. Static RX buffers cost about 1.6 KiB each (roughly 16 KiB total), allocated
at Wi-Fi init; dynamic RX/TX counts are limits, not proof all buffers exist at
startup. Internal management buffers, queue/semaphores, driver-selected task
stack and TCB, coexistence locks/state and PHY allocations require additional
headroom. Their aggregate bytes are not inferable from buffer counts alone.

IDF's `_wifi_malloc/calloc/realloc` fall through to ordinary allocation when
SPIRAM_TRY_ALLOCATE_WIFI_LWIP is off; lwIP's mem_clib allocation also uses the
ordinary allocator. Thus eligible >1 KiB packet data can prefer PSRAM without
changing the buffer model. Wi-Fi internal-specific wrappers and coexistence
wrappers explicitly require INTERNAL | DMA | 8BIT and stay internal. The
generic Wi-Fi OSI queue wrapper separately supports a PSRAM-backed static queue;
this does not change the failed Wi-Fi work queue, whose dedicated creation path
explicitly requires internal memory even with SPIRAM_USE_MALLOC. PHY,
coexistence, core affinity, aggregation and IRAM optimizations are unchanged.

The explicit SPIRAM_TRY_ALLOCATE_WIFI_LWIP option was evaluated and **not
selected**: Kconfig makes dynamic TX unavailable with that option and defaults
to 16 static TX buffers (about 25.6 KiB additional persistent internal RAM),
plus changes RX/BA defaults. This candidate addresses the measured shortage
with host relocation and general allocator integration while preserving the
existing Wi-Fi buffer contract. It does not claim dynamic DMA buffers relocate.

## Startup verification and diagnostics

Main conditionally depends on esp_psram and esp_https_server for the opt-in
profile. SDK startup initializes/tests/maps PSRAM and adds it to the heap;
application code does not initialize it twice. Before NVS/Gateway startup,
the existing development diagnostics verify `esp_psram_is_initialized()`,
`esp_psram_get_size()==2097152`, and a nonzero SPIRAM 8BIT heap total. A mismatch
fails before canonical application services start. SDK boot-init failures may
abort even earlier and must be read from the boot log.

All existing boundary/free/largest/minimum/general/PSRAM/HWM/failure diagnostics
are preserved. The profile additionally registers a public HTTPS event observer
after Wi-Fi connects, before the listener starts. It samples after successful
TLS handshake, disconnect and HTTPS error. These events are delivered later on
the event-loop task: their HWM belongs to that task, and their instantaneous
heap is not an exact handshake peak. The monotonic minimum-free heap includes
intervening lows. Client evidence is still required for a real handshake.
No payload, certificate/key, credential, NVS blob or Dataset is logged.
No HTTPS send/receive/open/close/transport callback or authentication route is
changed. The observer creates no task or trace buffer; event registration has
normal small SDK allocation overhead.

## Main stack qualification

Main remains **5120 bytes**; NimBLE 6144, OpenThread 8192 and HTTPS 16384 remain
unchanged. The task request reports a low main HWM but supplies no numerical
HWM samples, so there is no basis to select a replacement stack size.
A final-profile compile-only `-fstack-usage` audit was executed using actual
compile_commands entries. Examples: app_main=320, wifi_connect_common=464,
P2 init=656, P4 identity prepare=400, P4 proof build=720, TLS material build=656
bytes per frame. These are **not** full nested high-water bounds, nor can all
these frames simply be summed: code paths, inlining, SDK and crypto frames vary.
The inspected source has no new unbounded stack allocation.

A low lifetime HWM can reflect earlier NVS/provisioning/GATT/crypto calls rather
than current main usage. It does not reset when those calls return. Main runs
P4 identity/proof/certificate preparation later, so a successful Wi-Fi queue
allocation does not qualify main stack safety. Capture its boundary HWM through
listener setup and retain stack-overflow checking. Determine any stack increase
from that evidence and account for its added internal heap cost separately.

## Files changed in this follow-up

- config/dev1b-psram.defaults (new, development overlay)
- main/Kconfig.projbuild (default-off profile marker)
- main/CMakeLists.txt (conditional PSRAM/HTTPS component dependencies)
- main/dev_heap_diagnostics.c (initialization verification and HTTPS events)
- main/dev_heap_diagnostics.h (conditional observer declaration)
- main/esp_ot_br.c (register observer before existing listener startup)
- docs/G2_DEV_1B_PSRAM_MEMORY_CORRECTION.md (this report)

Earlier diagnostic changes/report and HTTPS host-stub correction were retained;
they were not reset. Universal Node firmware source, Companion, SDK, production
defaults and partition table remain unchanged. Architecture stays frozen;
OpenThread remains the only instance/Dataset owner. Both P2 journal and
NORMAL-before-OpenThread development safeguards remain in force. Saved Wi-Fi
still uses read-only wifi_config and RAM Wi-Fi storage. No ownership or P4
security-authority change and no architecture conflict was identified.

## Validation and artifacts

From the Gateway example:

```sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
mkdir -p /tmp/gateway-dev1b-psram
idf.py -B build-dev1b-psram \
  -D SDKCONFIG=/tmp/gateway-dev1b-psram/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig;config/dev1b-psram.defaults' build
idf.py -B build-dev1b-psram size
bash scripts/run_p4_auth_host_tests.sh
bash scripts/run_p4_https_lifecycle_tests.sh
bash scripts/run_p4_https_tests.sh
```

The output sdkconfig must initially be absent to apply both defaults; existing
output configs retain user values. The initial configure attempt failed because
the /tmp parent directory was absent; creating it resolved that setup error.
After explicitly pinning the newly visible SDK policies and Mbed TLS allocation,
the generated first-candidate config was preserved as sdkconfig.first-candidate
and a fresh final sdkconfig was generated. Only the final profile is reported.

From the firmware repository:

```sh
bash scripts/run_host_tests.sh provisioning_v2
bash scripts/run_host_tests.sh ble_auth_carrier
bash scripts/run_host_tests.sh ble_connection_context
```

Results:

- Final Gateway build and actual partition check: **PASS**.
- `idf.py size`: **PASS**; DIRAM **288715/341760 (84.48%)**, 53045 unassigned
  in linker accounting, BSS=168264, DIRAM text=98095, data=22356. Baseline DIRAM
  was 285287: **3428 bytes more static/internal use**, not less. Relocation
  benefits are runtime allocations and cannot be inferred from linker free
  space. This accounting does not measure live heap or contiguous headroom.
- Provisioning, BLE auth carrier and BLE connection-context host targets: **PASS**.
- P4 authentication host regression: **PASS**.
- HTTPS lifecycle: **PASS**, 26 modeled requests, 21 modeled releases.
- Synthetic loopback HTTPS/TLS regressions: **PASS**; authorized outside-sandbox
  loopback execution. This is OpenSSL host evidence, not ESP-IDF/FC61 TLS.
- Final-profile stack-usage compile audit: **PASS**, existing unrelated unused
  BLE symbol warnings observed. No full SDK call-graph stack bound established.
- Configuration audit: **PASS**, 88 selected BLE/Wi-Fi/coexistence/stack/profile
  settings identical, exact candidate allocation settings asserted.
- `cmp` partition tables: **PASS**, identical. Original diagnostic bin hash:
  **PASS**, unchanged. Linked symbols for PSRAM init/heap integration and the
  HTTPS event observer verified with offline nm.
- `git diff --check`: **PASS**.

Logs: /tmp/gateway-dev1b-psram-build.log,
/tmp/gateway-dev1b-psram-size.log, /tmp/gateway-psram-*-tests.log.
Stack records: /tmp/gateway-dev1b-stack-audit/*.su.

Final binary: **build-dev1b-psram/esp_ot_br.bin**
Size: **1690880 bytes**; 2 MiB slot headroom **406272 bytes (19.37%)**.
SHA-256: `0ea518fe606967e8c9eb7fea688c4c52202c86ff708d1f8372bb752f68c52418`.

Original diagnostic build/esp_ot_br.bin remains byte-identical:
`e24726c7287be02721ad1a07ac275afa9202482a8bab3743ba6e0c9d93507ecb`.
A separate copy of its bin/ELF/map/config/bootloader/partition table/source and
SHA manifest is `/tmp/gateway-dev1b-diagnostic-preserved-20261117T201005/`.
Existing recovery backup files were not touched.

## Remaining risks and separately authorized hardware plan

No predicted free-heap saving or runtime PASS is claimed. PSRAM has cache and
latency constraints, strict NimBLE external allocations fail if PSRAM exhausts,
and internal/DMA queues/stacks still consume the reserve. Static Wi-Fi RX,
controller use, HTTPS task creation and handshake/concurrency peaks can still
exhaust internal memory. The 1 KiB placement threshold and reserve require
measurement; they are an initial supported candidate, not qualified production
tuning. No additional hardware action is authorized by this report.

After separate FC61 approval and the previously reviewed app-only ota_1
procedure (preserving ota_0, boot metadata and commissioned persistent state):

1. Capture full PSRAM init/memory-test output and the application's initialized,
   2097152-byte physical-size/nonzero-heap confirmation. Failure stops the test.
2. Capture all existing boundary lines: after NVS/Gateway/OpenThread/attachment/
   BLE/P4, before and after Wi-Fi. Record free/largest/minimum internal 8BIT,
   PSRAM use and every allocation failure. Compare with supplied baseline.
3. Require successful saved Wi-Fi association/IP plus an active BLE connection
   and retained Thread attachment. Confirm bonding, provisioning-carrier and
   authentication behavior without reprovisioning or credential rotation.
4. Require HTTPS listener startup and a genuine FC61 TLS client handshake on
   the existing TLS-only port. Record after-listener and after-handshake heap,
   minimum/free/largest values, disconnect recovery and client-side success.
   Repeat connections and exercise the existing four-socket limit with BLE
   active; check for downward trends, heap failure, fragmentation and stack
   faults. Synthetic host TLS does not satisfy this criterion.
5. Measure main HWM through identity/proof/certificate/listener setup separately;
   HTTPS events report event-task HWM. Qualify HTTP task HWM using approved
   supported runtime observation if needed. Do not infer stack safety from
   linker output or the per-function stack audit.
6. Acceptance requires stable, sustainable internal headroom for Wi-Fi tasks,
   RX/TX bursts and concurrent TLS, not merely >1684 bytes before Wi-Fi. No
   numeric sustainable margin can be certified before those runtime peaks are
   measured. Persistent state, recovery image and both commissioning safeguards
   must remain unchanged. Wi-Fi is fixed only after physical initialization
   succeeds; DEV-1B is complete only after HTTPS and genuine TLS succeed.
