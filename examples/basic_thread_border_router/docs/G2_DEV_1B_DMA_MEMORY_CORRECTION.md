# DEV-1B second Wi-Fi failure: internal DMA-memory correction

## Later DEV-1B closure evidence (2026-11-18)

The user reports FC61 hardware validation of binary SHA-256
`e718f74c057504a3df90544a78a94ea512d9272642fb58c916149848a890a82e`
(1,693,728 bytes), with an 8192-byte main stack. PSRAM, Wi-Fi, BLE,
OpenThread attachment, P4 identity preparation and the TLS-only listener
passed; five sequential TLS 1.2 handshakes and memory recovery passed.
After the fifth disconnect: internal free 18,567; DMA free 10,607;
DMA largest 7,936; PSRAM free 2,018,872 bytes; 19 tasks. This evidence
is user-supplied, not a new hardware run in this review.

DEVELOPMENT_TLS_SMOKE: VALIDATED for that exact binary and sequential test.
PRODUCTION_GATEWAY_HTTPS: NOT_YET_VALIDATED. The self-signed certificate
does not establish production trust. Concurrency, indefinite stability and
Registry Sync convergence were not validated by this evidence.

Merge readiness remains blocked: the P4 principal binding accepts both native
EUI-64 and expanded EUI-48 derivations for an ambiguous FF:FE principal without
an authenticated platform discriminator. ADR-084 and ADR-086 require the
canonical principal/Node-ID binding; its resolution needs security/architecture
review, not a silent change to identity ownership. The development overlay now
records the 8192-byte stack, but the build's base resolved sdkconfig is an
ignored build artifact and must be made reproducible from reviewed tracked inputs.
The historical investigation below describes earlier candidates and results.

**Verdict: PARTIAL.** The second allocation failure is confirmed by the supplied
FC61 hardware evidence. A separately built, minimally scoped development memory
candidate passes software checks. No hardware operation was performed here.
Wi-Fi is not declared fixed; DEV-1B is incomplete pending actual HTTPS, TLS and
concurrency qualification. Date: 2026-11-17.

## Failure assessment

The N8R2's 2 MiB Quad PSRAM was detected at 40 MHz and passed its memory test in
the user-supplied run. Before Wi-Fi: internal free=30531, largest=19456;
PSRAM free after BLE=2063876 / total=2097152. The former 1684-byte queue failure
was passed and the driver task started. New callback: **1604 bytes, caps 0x80c**
(INTERNAL | DMA | 8BIT), followed by static RX count **4 of expected 10**.

This is exhaustion of the specific internal/DMA heap during later driver
initialization. Plenty of PSRAM does not satisfy that capability request.
The 19456-byte largest block is a pre-init sample: preceding driver/task/queue
allocations have consumed memory by the fifth RX allocation. It does not prove
that a 1604-byte DMA block remained available at the actual failure. Prior
instrumentation did not separately measure DMA free/largest values. No claim
about the precise DMA heap state at failure is made beyond the failed request.
Six further RX payload allocations alone need **9624 bytes**, excluding allocator
overhead. Fixing only that shortfall would leave HTTPS unbudgeted.
Wi-Fi returns ESP_ERR_NO_MEM; the example helper's ESP_ERROR_CHECK then aborts.
Unlike the original failure, this is not the earlier vQueueDelete cleanup path.

## Selected correction and exact configuration delta

New overlay: `config/dev1b-dma.defaults`. Apply it after the **preserved previous
PSRAM candidate's resolved sdkconfig**, not against release defaults.

| Option | Previous | New |
| --- | ---: | ---: |
| SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY | n | y |
| SPIRAM_MALLOC_ALWAYSINTERNAL | 1024 | 0 |
| OPENTHREAD_PLATFORM_MSGPOOL_MANAGEMENT | n | y |
| SPIRAM_MALLOC_RESERVE_INTERNAL | 32768 | 32768 |

ESP_ALLOW_BSS_SEG_EXTERNAL_MEMORY is the derived selected gate. Every other
resolved configuration value was checked unchanged (unavailable false booleans
normalized to false). IGNORE_NOTFOUND becomes unavailable with external BSS;
it remains disabled and PSRAM failure remains fatal.

Static RX=10, dynamic RX=32, dynamic TX=32, dynamic TX mode, RX BA=6, management
buffers, Wi-Fi IRAM optimizations, WPA3, all coexistence settings, BLE capacity,
HTTPS/TLS settings, main/OT/BLE/HTTPS stack sizes and security gates remain
unchanged. General OT allocation caps remain unchanged:
OPENTHREAD_PLATFORM_MALLOC_CAP_SPIRAM=n. No production default is modified.

The BSS-only comparison recovered only **13176 net internal bytes**. That did
not provide convincing Wi-Fi plus HTTPS stack margin. The final candidate also
uses IDF's existing OpenThread platform message pool in PSRAM, preserving all
**65 x 128-byte** packet buffers. Its pointer array uses **65 x 4=260** external
bytes in addition to the **8320-byte** buffer allocation and heap overhead.
No task, second instance, Dataset mechanism, commissioner or ownership change
is introduced. SDK message.cpp calls otPlatMessagePoolInit with the existing
buffer count; SDK esp_openthread_messagepool.c owns New/Free and lifecycle.
Only volatile packet storage moves. The canonical instance remains internal.

## Supported external BSS and map evidence

IDF 5.5.4 supports this exact S3/Quad/boot-init/malloc configuration. Its
Kconfig.spiram.common and docs/en/api-guides/external-ram.rst describe the
supported library BSS relocation; sections.ld.in places it in `.ext_ram.bss`
NOLOAD and SDK startup initializes/zeros it before app_main and removes that
region from the external heap. No application EXT_RAM_BSS_ATTR, custom linker
mapping, external stacks or external noinit section was added.

| Archive | Previous internal BSS | Final internal BSS | Final external BSS |
| --- | ---: | ---: | ---: |
| libnet80211.a | 7602 | 0 | 7602 |
| libpp.a | 1234 | 0 | 1234 |
| liblwip.a | 2826 | 0 | 2826 |
| libwpa_supplicant.a | 1330 | 0 | 1330 |
| libbt.a | 160 | 0 | 160 |
| libbtdm_app.a (controller) | 684 | 684 | 0 |
| libopenthread.a | 40439 | 32119 | 0 |

The selected library payload totals **13152 bytes**; external section footprint
is **13248 bytes** with alignment. Examples: g_cnxMgr 3880, s_wifi_nvs 1308,
gWpaSm 828, g_ic 688, lwIP loop_netif 560 and destination_cache 480 bytes.
The linked controller's own fragments explicitly retain BSS/COMMON/data in
internal memory. The Bluetooth host library, not the controller, is relocated.
Although documentation mentions Bluedroid, the actual linked libbt.a contributes
160 bytes for this NimBLE build; no Bluedroid host was enabled.

| Static accounting | Previous PSRAM candidate | Final DMA candidate | Difference |
| --- | ---: | ---: | ---: |
| DIRAM BSS | 168264 | 146704 | -21560 |
| DIRAM initialized data | 22356 | 22436 | +80 |
| DIRAM text | 98095 | 98095 | 0 |
| DIRAM total | 288715 | 267235 | **-21480** |
| Linker unassigned DIRAM | 53045 | 74525 | +21480 |
| `.ext_ram.bss` | 0 | 13248 | +13248 |

_heap_low_start moves **0x3fcce8a0 -> 0x3fcc94b8**, exactly **21480 bytes** lower.
S3 heap/port/esp32s3/memory_layout.c marks the reclaimed DRAM/DIRAM regions as
INTERNAL | DMA | 8BIT capable. This is a real static placement recovery, not a
prediction from PSRAM capacity. Runtime free/largest values still depend on
allocator bookkeeping, reservation and changed runtime allocation choices.
Do not equate 74525 linker-unassigned bytes with live DMA heap.

The OT instance shrinks from 33200 to 24872 internal bytes when its embedded
pool is removed; small new platform pool bookkeeping remains internal. Overall
OT internal BSS falls by 8320, not the size of a second instance. Final map
checks show unchanged internal BSS sizes and zero external BSS for provisioning
(45), security_root (125), P4 identity (1722), Gateway (7002) and controller
(684). Canonical persistence/security ownership has not been relocated.

Evidence artifacts in the new build directory:
`static-placement-comparison.json`, `external-bss-symbols.json`, both ELF/map
files and `sdkconfig.resolved`. Per-archive JSON and size diff are also stored
under `/tmp/gateway-dma-*.json`. The BSS-only comparison image/map/config remain
at `/tmp/gateway-dev1b-dma/bss-only-comparison/`.

## Allocation policy and cache/DMA safety

heap_caps.c implements the threshold as a **preference**, not a hard boundary:
nonzero ordinary allocations at or below 1024 formerly preferred internal
DEFAULT memory. With zero, all nonzero ordinary malloc/calloc/realloc requests
prefer SPIRAM | DEFAULT; fallback to DEFAULT remains available on failure.
Explicit heap_caps INTERNAL, DMA, FreeRTOS object and peripheral requests do not
lose their capabilities. General heap_caps calls with explicit DEFAULT flags
are not equivalent to the threshold-aware libc allocator and are not forced
external by this setting.

The 32768-byte reserve removes DEFAULT from a pool allocated out of existing
internal DMA memory and re-adds it for explicit INTERNAL/DMA/8BIT allocation.
It neither increases RAM nor remains permanently free; tasks/driver objects
consume it. Increasing it can starve ordinary allocations or split regions and
is not selected. Keeping it while moving ordinary small allocations addresses
unnecessary competition without changing the required controller, DMA,
peripheral, stack or interrupt-critical allocations.

PSRAM is inaccessible when flash cache is disabled. IDF flash/cache guards and
supported library placements must remain intact. Application ISRs or callbacks
must not start accessing these external tables during cache-disabled operation.
DMA descriptors cannot be in PSRAM; Wi-Fi's explicit INTERNAL | DMA allocation
wrappers are unchanged. No DMA capability flags, controller linker mappings,
ISR code/data annotations, external stacks or cache-control behavior were
changed. SDK-selected library BSS is eligible under its supplied linker policy;
that does not prove every private SDK timing/cache interaction on FC61. Binary
Wi-Fi/controller archives are not fully source-visible. Hardware qualification
must cover concurrency; any flash-operation stress needs its own authorization
and must not mutate FC61 commissioning data merely to test memory placement.

PSRAM physical size should still be 2097152. Heap-capability PSRAM total will
now be smaller by the reserved external BSS footprint; a non-2MiB heap total is
therefore expected and is not a failed chip detection. The current startup
check correctly checks physical size separately and only requires a nonzero
external heap. OpenThread pool allocation and more small libc allocations
consume additional external heap after startup. Actual internal gains from
threshold zero remain unmeasured and are not added to the static recovery.

## Internal Wi-Fi + HTTPS budget

These costs are a known-component subtotal, not a complete driver budget.

| Consumer | Evidence-supported budget |
| --- | --- |
| Wi-Fi task stack | 6144 bytes for this build: private getter disassembly selects 6 KiB with WPA3 enabled and nano printf enabled; confirm against next driver task startup log |
| Wi-Fi TCB | sizeof(TCB_t)=340; total stack+TCB 6484 before allocation/alignment overhead |
| Driver work queue | 200 x 8 + 84 control =1684, plus 8-byte wrapper =1692 before heap overhead |
| Task-create semaphore | sizeof(StaticQueue_t)=84 via xSemaphoreCreateCounting, before overhead |
| Static RX | 10 x observed 1604 =16040 payload-allocation bytes, plus overhead; each block must satisfy 0x80c |
| RX management | 5 buffers; SDK documents approximately 500 bytes each (~2500); exact layout/overhead is private |
| Management short buffers | 32 retained; exact per-buffer bytes not exposed by Kconfig and not inferred |
| DMA descriptors / PHY / driver state | Explicit internal/DMA paths retained; descriptor counts and aggregate allocation size not established by supplied logs/private archive |
| Coexistence | Internal mutex/semaphore/critical state retained; some sizes are observable but live object count is not supplied |
| Dynamic RX/TX | 32/32 limits retained, demand-driven; frame sizes, in-flight copies, fragmentation and DMA-specific TX allocations determine peaks |
| HTTPS task | 16384 stack + 340 TCB =16724 internal bytes before overhead, once per server, not once per socket |
| TLS and sockets | Up to four sockets retained; ordinary Mbed TLS allocations follow the existing default allocator (now prefer PSRAM). TCP/HTTP objects and explicit internal queues/semaphores still add runtime costs |

Known-component subtotal is approximately **43524 bytes** (42.50 KiB), including
the approximate management cost but excluding all unknown rows and allocator
overhead. It explains why the design must consider more than the next 1604-byte
buffer. The final **21480-byte** static recovery and zero-threshold policy are
intended to cover initialization plus subsequent HTTPS/task/traffic pressure.
They do **not** certify a sustainable runtime margin. Simply adding static
recovery to the supplied pre-Wi-Fi total would yield 52011 bytes if all other
runtime use were identical, which it is not; this arithmetic is not a measured
DMA headroom result. No invented exact descriptor/TLS concurrency total is used.

Alternatives evaluated: BSS-only recovery is too small for a convincing complete
budget; a larger internal reserve creates no RAM; explicit Wi-Fi/lwIP PSRAM
preference still forces static TX and was not enabled; IRAM optimization removal
would trade throughput for RAM and was not selected. RX reduction remains only
a fallback: each removed static RX buffer would save roughly 1604 payload bytes
plus overhead but reduce burst reception, and must respect aggregation/BA-window
requirements. It is not implemented. External task stacks are not selected.

## Separate safety finding: driver NVS initialization is not read-only

`wifi:config NVS flash: enabled` accurately reflects
WIFI_INIT_CONFIG_DEFAULT.nvs_enable=1 from ESP_WIFI_NVS_ENABLED=y. It is separate
from the application's wifi_config namespace and the later storage selection.

The real order is:

`example_wifi_start -> esp_wifi_init -> wifi_nvs_init/load -> later
esp_wifi_set_storage(WIFI_STORAGE_RAM)`.

Offline disassembly of installed `libnet80211.a(ieee80211_nvs.o)` establishes:

- wifi_nvs_init opens **nvs.net80211** using OSI _nvs_open with mode **1**,
  NVS_READWRITE. OSI field offset 0x124 resolves to the real nvs_open.
- NVS Storage::createOrOpenNamespace writes the namespace record if it is
  absent, even without an application-level nvs_set or nvs_commit.
- wifi_nvs_load includes validators, calls wifi_nvs_commit before return, and
  validation repair paths can reach wifi_nvs_set, whose blob path reaches the
  real _nvs_set_blob (0x130). Commit resolves to real _nvs_commit (0x12c).
- wifi_nvs_deinit closes the handle and frees driver state; it does not roll
  back prior writes. Initialization precedes later RX allocation failure, so
  a failed Wi-Fi initialization is not proof that NVS was untouched.

Thus **Wi-Fi initialization can modify driver-owned persistent configuration
before RAM storage is selected**. The enabled log alone does not prove that this
particular FC61 boot actually changed any record; existing namespace, values,
validation and NVS no-op behavior affect whether flash is written. No FC61 NVS
readback/diff was performed. It does not establish a write to the application's
saved wifi_config, Dataset, manufacturing identity or provisioning namespace.
The existing helper's NVS_READONLY reads and save_to_nvs=false branch still
preserve the application credential-storage contract, but they are insufficient
to establish a completely non-writing SDK initialization path.

**No NVS behavior was changed in this task**, per the explicit instruction to
report this separately and obtain approval for a narrowly scoped correction.
Recommended separate correction for review: pass nvs_enable=0 to the real
esp_wifi_init in the development-only path (or disable only its development
Wi-Fi NVS option), while retaining the application's read-only saved-credential
reads and RAM storage. Do not change global SDK callbacks or production storage.
This recommendation needs focused regression/compile validation and approval;
it is not silently included in the new image. A no-NVS-write FC61 test must
resolve this gate before booting the candidate. Existing PHY calibration or
other SDK persistence also needs review if the approved test requires a global
no-flash-write guarantee.

## Main-stack review

The reported ~500-byte HWM on the 5120-byte main stack means only ~9.8% lifetime
margin in that capture. HWM is the historical minimum, not current remaining
space after deep startup calls return. No task stack was reduced or enlarged.
Target -fstack-usage compilation shows app_main remains 320 bytes per frame;
expanded dev_heap_snapshot is 112 versus 96 previously (**+16 bytes**), failure
callback remains 32 and the added OT pool API functions use 32-byte frames.
This does not bound the full SDK/crypto call chain. Future P4 identity/certificate
preparation on main may set a new low, and the HTTPS task has its own stack.
Keep canary/overflow checks; capture main HWM after Wi-Fi and after listener
preparation. A stack increase, if needed, must be separately budgeted in internal
RAM. Do not approve this image based solely on nominal configured stack sizes.

## Files and architecture

Changed in this follow-up:

- main/dev_heap_diagnostics.c: adds INTERNAL | DMA | 8BIT free/largest sampling
  to every existing boundary; preserves internal min/general/PSRAM/HWM/task
  counts and failure callback. No trace buffer, task or heap allocation added.
- config/dev1b-dma.defaults: three selected memory settings and unchanged
  explicit reserve; separate from the existing tested PSRAM profile.
- docs/G2_DEV_1B_DMA_MEMORY_CORRECTION.md: this implementation/audit report.

The existing diagnostics, PSRAM profile and image, source ownership, startup
sequence, TLS routes/configuration, Wi-Fi credentials, frozen ADRs and production
defaults are preserved. No SDK, Universal Node firmware or Companion source was
modified. No Dataset, NVS, identity, eFuse, OTA metadata or hardware operation was
performed. No architectural ownership conflict was identified; the SDK NVS
read-only safety assumption is explicitly corrected above. No completion commit
or PROJECT_STATE completion update is claimed while hardware qualification and
review remain outstanding.

Read instructions: applicable AGENTS/temporary override, PROJECT_WORKFLOW,
PROJECT_STATE and relevant ADR-066/ADR-070 ownership constraints. Frozen workflow,
architecture and roadmap documents were not edited.

## Executed validation and reproducibility

From the Gateway example, using a fresh output sdkconfig:

```sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
mkdir -p /tmp/gateway-dev1b-dma
idf.py -B build/dev1b-dma \
  -D SDKCONFIG=/tmp/gateway-dev1b-dma/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=build-dev1b-psram/sdkconfig.resolved;config/dev1b-dma.defaults' build
idf.py -B build/dev1b-dma size
bash scripts/run_p4_auth_host_tests.sh
bash scripts/run_p4_https_lifecycle_tests.sh
bash scripts/run_p4_https_tests.sh
```

Firmware directory:

```sh
bash scripts/run_host_tests.sh provisioning_v2
bash scripts/run_host_tests.sh ble_auth_carrier
bash scripts/run_host_tests.sh ble_connection_context
```

- Final Gateway build: **PASS**, new DMA diagnostics and platform pool compiled.
- Build's actual partition-size check: **PASS**, identical partition tables.
- Provisioning, BLE auth carrier, BLE context, P4 authentication: **PASS**.
- HTTPS lifecycle: **PASS**, 26 modeled requests, 21 releases.
- Synthetic OpenSSL HTTPS/TLS: **PASS**, loopback execution outside sandbox under
  tool approval; no FC61 access. Does not verify ESP-IDF runtime placement.
- Before/after map, archive JSON, nm and offline GDB type/OSI-offset review:
  **PASS**. The BSS-only comparison and final pool-enabled build were both run.
- Exact config comparison: **PASS**, only the three selected values and derived
  external-BSS gate differ. All buffer, stack, TLS, security and coexistence
  settings otherwise unchanged.
- Protected component/controller BSS location review: **PASS** (static evidence).
- Target compile-only stack audit: **PASS**, no hardware debugger attached.
- `git diff --check` and new-file whitespace checks: **PASS**.
- Hardware, actual Wi-Fi, actual HTTPS/TLS/concurrency/cache behavior: **NOT RUN**.

Logs: `/tmp/gateway-dev1b-dma-build.log`,
`/tmp/gateway-dev1b-dma-size.log`, `/tmp/gateway-dma-*-tests.log`.
Allocator/SDK audit: `/tmp/gateway-dma-sdk-audit/`.
Stack audit: `/tmp/gateway-dma-stack-audit/`.

Final binary: **build/dev1b-dma/esp_ot_br.bin**
Size: **1691328 bytes**, slot headroom **405824 bytes (19.35%)**.
SHA-256: `d8ef7091633c289943b56a010f61b22f631f21403e7cb2e61d543a91759d6b7c`.
Resolved configuration: **build/dev1b-dma/sdkconfig.resolved**.

Previous build-dev1b-psram image remains unchanged:
`0ea518fe606967e8c9eb7fea688c4c52202c86ff708d1f8372bb752f68c52418`.
Its ELF/map/bin/resolved configuration, bootloader/partition table and relevant
source/config files were separately copied before editing to
`/tmp/gateway-dev1b-psram-preserved-20261117T210648/`, with SHA manifest.
Use that preserved source/config snapshot to reproduce its prior diagnostics;
rebuilding it from the newly edited working tree would include the new DMA
log fields. Existing recovery backups and the original diagnostic build were
not altered.

## Proposed controlled FC61 qualification — separate authorization required

1. Resolve the pre-RAM NVS write gate above before a test that promises unchanged
   NVS. Approve and validate a narrowly scoped development correction first;
   the present candidate deliberately retains current storage behavior.
2. Obtain separate FC61 maintenance authorization and use the already reviewed
   app-only ota_1 procedure: verify live map/boot state, preserve and hash ota_0,
   previous candidate and recovery backups; no blanket idf.py flash, partition
   table, otadata, Dataset, NVS, manufacturing or eFuse operation. The memory
   task itself authorizes none of those hardware actions.
3. Capture PSRAM memory-test/physical-size confirmation and the complete new
   internal and internal-DMA free/largest snapshots after NVS/Gateway/OT/
   attachment/BLE/P4, before Wi-Fi and on both return paths. Expect reduced
   PSRAM heap total for external BSS and the existing 65-buffer pool success
   log (8320 bytes), not an additional OpenThread instance.
4. Require **all 10 RX buffers**, driver initialization return ESP_OK, saved-Wi-Fi
   association/IP and no failed required internal/DMA allocation. Preserve BLE
   connection, authentication and Thread attachment. A first successful buffer
   or Wi-Fi task is insufficient.
5. Require HTTPS startup and a client-observed genuine TLS handshake, then
   repeated connections and existing four-socket concurrency with BLE + Thread
   traffic. Capture DMA/internal min/free/largest, PSRAM use, event snapshots,
   main HWM and supported HTTPS/task HWM observation. Compare idle recovery
   after disconnect and look for descending baselines or fragmentation.
6. Establish sustained margin from measured bursts/concurrency and allocator
   peaks rather than certifying a single free-byte threshold. Explicit internal
   consumers, unknown driver descriptor costs, external-memory exhaustion,
   cache-disabled interactions and ~500-byte prior main HWM remain risks.
   If insufficient, measure which allocation consumes the remaining pool before
   considering IRAM-performance or RX-capacity tradeoffs. No such fallback was
   applied automatically.
7. Confirm unchanged commissioned state and recovery image using approved
   read-only verification. Wi-Fi is fixed only after actual successful init;
   DEV-1B is complete only after genuine TLS and concurrency tests succeed.
