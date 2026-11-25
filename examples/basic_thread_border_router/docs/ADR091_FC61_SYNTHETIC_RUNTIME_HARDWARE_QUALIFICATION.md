# ADR-091 M4W.3C — FC61 synthetic runtime hardware qualification

Date: 2026-11-25. **PASS — bounded synthetic runtime execution on commissioned development Gateway; not connected service or product acceptance.**

## Provenance and candidate

User supplied terminal and implementation reports; the documentation reviewer did not execute the hardware operations or retrieve the local binary/configuration artifacts. Gateway source `927d57304c4bbd52e97ee5806c0c8a35eef20ec0`, firmware source `1dab303f0218df5f4bbe9e797c06300f6c410174`, ESP-IDF v5.5.4 with previously reported local PSRAM message-pool support. Final candidate SHA-256 `5d644a66e7072d30219e1e02421a7f79d975f60ef85c4513968456fb9e38dff9`, size 1,704,704 bytes (0x1a0300), 392,448 bytes free in 2 MiB app slot. Configuration-only corrections after the integrated self-test source commit. User-local build evidence: `examples/basic_thread_border_router/build/adr091-fc61-final-candidate/` (resolved configuration, generated header, ELF, map, logs, manifest and compile assertion). No binary, raw credentials or backup content added to Git.

## Controlled development build reference

Preserve the following effective settings from the successful candidate; compare generated sdkconfig.h and the full resolved configuration before any future build/deployment. This is a recorded hardware-tested profile, not a claim that the complete resolved configuration or local ESP-IDF patch is versioned in this repository.

| Setting | Required reference value |
| --- | --- |
| CONFIG_UN_GATEWAY_DEV_PSRAM | y |
| CONFIG_SPIRAM | y |
| PSRAM mode/type/speed | Quad, auto, 40 MHz |
| PSRAM boot initialization and malloc integration | Enabled |
| CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL | y |
| External BSS / OpenThread platform message pool | Enabled |
| CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL | 1024 |
| CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL | 32768 |
| SDK Wi-Fi NVS (CONFIG_ESP_WIFI_NVS_ENABLED) | Disabled |
| External task stacks / FATFS external allocation / general OpenThread external allocation | Disabled |
| Mbed TLS allocation | Default |
| Main stack | 8192 bytes |
| Self-test task stack | 6144 bytes |
| CONFIG_UN_GATEWAY_DEV_WIFI_SYNTHETIC_SELFTEST | Default OFF; ON solely for qualification |

The reference is not identical to the previous DMA configuration (that profile used a zero-byte threshold). The failed first candidate disabled PSRAM and used NimBLE internal allocation: internal free after BLE was 16 bytes, largest block zero, BLE and task creation failed. The corrected build reduced linker DIRAM usage from 285,375 to 267,567 bytes and added 13,248 bytes external BSS. The final configuration also disables SDK Wi-Fi NVS, preserves the application READONLY saved-Wi-Fi path and RAM driver storage. Normal startup is NOT certified globally write-free: PHY calibration and other existing state-dependent writes remain possible.

## User-reported physical test

FC61 ESP32-S3, 8 MiB flash, 2 MiB embedded PSRAM, secure boot and flash encryption disabled. Live table does not include p4_identity; compiled table does. Source review identified no fatal missing-P4-partition dependency in the development startup path. Both application slots, live partition table and OTA metadata were backed up and verified before authorized app-only replacement of the running ota_1 (0x220000). No direct NVS/identity, bootloader, partition-table or OTA metadata write was authorized. The user subsequently confirmed baseline restoration; no separate restoration terminal transcript or post-restore health log was supplied.

| Evidence | Result |
| --- | --- |
| PSRAM physical / heap | 2,097,152 / 2,083,904 bytes; memory test OK |
| Internal free / largest after Thread runtime attach | 104,003 / 31,744 bytes |
| Internal free / largest after BLE | 48,327 / 31,744 bytes |
| Wi-Fi init and saved-Wi-Fi LAN connection | PASS |
| Internal free / largest before HTTPS | 11,559 / 7,680 bytes |
| DMA free / largest before HTTPS | 3,771 / 3,072 bytes |
| HTTPS development listener | FAIL: allocation 16,384 bytes; ESP_ERR_HTTPD_TASK |
| Internal free / largest after HTTPS attempt | 10,559 / 7,680 bytes |
| Synthetic status, scan, queue | PASS |
| Synthetic validation and commit | PASS |
| Synthetic cancellation and rollback | PASS |
| Synthetic uncertain outcome and restart recovery | PASS |
| Final self-test | PASS; owner 1,088 bytes, queue capacity 8 |
| Stack allocation / high-water / observed used | 6,144 / 4,688 / 1,456 bytes |
| Panic/watchdog | None in supplied filtered log |

These results close the isolated synthetic runtime hardware milestone, not the complete M4W.3C connected runtime. The test uses RAM fixtures, not actual Wi-Fi candidate association or physical storage durability. Thread runtime attachment was logged; actual Thread network attachment/mesh traffic, BLE app interoperability, post-task deletion heap recovery and full Gateway coexistence were not established. HTTPS resource failure remains an M4W.6 blocker; stack margin is only for this fixture. Do not infer production readiness or reduce stack allocation solely from this test.

## Remaining gates

M4W.3B authorized ingress / ordered carrier-loss handoff remains unresolved. Keep 0x42 fail-closed and production registration disabled. M4W.3C connected runtime, M4W.4B Companion live integration, M4W.2D physical persistence, M4W.5 connected HIL, and M4W.6 BLE/storage/coexistence and security qualification remain open. M4W.3A development acceptance limitations remain in force. No additional synthetic-only implementation or duplicate build is required absent changes. No new architecture decision or deployment authorization is recorded here.
