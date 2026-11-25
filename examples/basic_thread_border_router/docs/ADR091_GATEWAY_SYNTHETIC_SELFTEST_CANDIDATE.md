# Gateway ADR-091 synthetic self-test candidate

2026-11-25. Candidate only; no hardware access, flash, reset or deployment was performed.

## Scope

`CONFIG_UN_GATEWAY_DEV_WIFI_SYNTHETIC_SELFTEST` is default-off and depends on the existing non-product Gateway profile. When enabled, the new optional `gateway_wifi_synthetic_selftest` component compiles the fixed-fixture owner and existing Wi-Fi codec, transaction, storage and recovery foundations. The existing Gateway application starts the test once after normal Gateway startup; failure to create the task is logged without stopping the Gateway. The RAM-only synthetic test has no ingress, authorization interface, real Wi-Fi or NVS calls. It wipes and frees its temporary owner allocation and deletes its task on completion. The existing 0x42 fail-closed guards, partition source, bootloader source and persistent service code were not changed. The selected existing development configuration retains its TLS smoke behavior and saved-Wi-Fi connection path; this change does not itself reconfigure Wi-Fi, but cannot certify all existing startup effects on an unobserved device.

## Build and tests

ESP-IDF v5.5.4, Gateway source base `79fab9bf603818c1988356dc60b8994dd3a3b8a7` plus this uncommitted candidate; firmware base `1dab303f0218df5f4bbe9e797c06300f6c410174`. Two Gateway builds using the qualified `/tmp/adr091-m4w3bc-sdkconfig` and a copied config with only the self-test option enabled passed. Default-off ELF has no `adr091_synthetic_owner_start` symbol. The enabled image is `/tmp/adr091-gateway-wifi-on-build/esp_ot_br.bin`, SHA-256 `2a0f22f354f009b7fb74c8464135dde07462417bcb0502b7053197f7431f0df1`.

| Measurement | Flag off | Flag on | Delta |
|---|---:|---:|---:|
| Application binary | 1,686,400 | 1,696,208 | +9,808 bytes |
| App slot free | 410,752 | 400,944 | -9,808 bytes |
| Linker DIRAM used | 285,375 | 285,375 | 0 bytes |
| Linker flash code | 1,275,408 | 1,284,676 | +9,268 bytes |
| Linker flash data | 277,376 | 277,920 | +544 bytes |

The enabled test temporarily allocates a 1,088-byte target owner and requests a 6,144-byte task stack, plus FreeRTOS task bookkeeping and allocation overhead. Live free heap, timing and stack high-water remain unmeasured on hardware. Both generated partition-table binaries are byte-identical (SHA-256 `a6ecf1d26ae3c0f44bbc681999d1f66917eebd43bff4ec24fd537c21250cb91b`); the generated bootloader binaries differ and neither should be written for app-only deployment. This build does not establish the live device partition table or boot state.

Host-shim self-test passed normally, with ASan and UBSan. Seven existing standalone cases and the existing foundation storage regression passed. A host fake high-water number is not a target measurement. No real Wi-Fi behavior, NVS durability or hardware self-test is qualified by these results.

## Device output

After normal Gateway startup and about 10.2 seconds of stability waits, expect:

- `ADR091_RUNTIME_SELFTEST: status scan queue PASS`
- `ADR091_RUNTIME_SELFTEST: validation commit PASS`
- `ADR091_RUNTIME_SELFTEST: cancellation rollback PASS`
- `ADR091_RUNTIME_SELFTEST: uncertain outcome restart recovery PASS`
- `ADR091_RUNTIME_SELFTEST: PASS owner_bytes=1088 queue_capacity=8 stack_config_bytes=6144 stack_high_water_bytes=<measured>`

Failure to create the task, allocate the owner, or complete the test prints a `FAIL` marker. Absence of the final marker, a reset, failed normal startup, or insufficient Gateway memory is not a pass. Existing DEV TLS readiness and BLE/Thread continuity must be checked separately.

## Future deployment and restoration prerequisites

This document authorizes no hardware action. Before a future operation, obtain explicit maintenance authorization and use the supported HIL lifecycle. Confirm device identity, actual partition map, current running/boot slot and OTA metadata, secure-boot/encryption state, and app image compatibility. Back up and verify the currently running app, intended target slot and `otadata`; retain independent recovery copies and hashes. Preserve commissioned NVS, manufacturer/P4 identity, Thread Dataset, saved Wi-Fi and eFuse data. Do not display secrets. Verify the candidate and backup hashes before any write.

The compiled table has two 2 MiB app slots (`ota_0` at `0x20000`, `ota_1` at `0x220000`), but these offsets are not proof of the live map. The candidate fits either compiled slot. App-only inactive-slot deployment is conditional on live verification and the supported HIL tool offering safe app-only write/verification and controlled boot selection. The existing documented proposal in `G2_DEV_1B_WIFI_HEAP_INVESTIGATION.md` specifically discusses an `ota_1` replacement under the ROM loader with existing `otadata` unchanged when it already selects `ota_1`; it is not evidence that a general inactive-slot switch is available. If an inactive slot must be selected through OTA metadata, explicitly plan and authorize that metadata update and rollback; otherwise stop. Never assume the running slot or overwrite it while executing.

Use only the reviewed app image and supported app-only process, verify the written image before reboot, and preserve the currently bootable image. Do not use `idf.py flash`/`flash_args`, erase-all, generated bootloader, generated partition table, `ota_data_initial.bin`, NVS, SPIFFS/RCP, or identity/eFuse operations. Confirm restoration of the original app and boot selection is supported and tested before deployment; retain the exact original slot and metadata backups and a recovery path. If safe backup, write, boot selection or restoration cannot be demonstrated, do not deploy. No claim of compatibility with the live device or availability of HIL capabilities is made by this offline build.
