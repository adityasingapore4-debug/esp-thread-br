# ADR-091 M4W.3B/C Gateway checkpoint

2026-11-25 — integration blocked; no Gateway source or configuration changes.

The M4W.2 codec, transaction, storage and recovery foundation remains unregistered. The requested synthetic service and BLE adapter have not been implemented. The existing firmware authorization seam does not supply the business path with its BLE connection context or establish an ordered carrier-loss handoff to a Wi-Fi transaction owner. The work is stopped at that boundary pending a qualified integration design, without adding an alternative authority or response path. Production registration and real Wi-Fi/NVS activation remain off.

Against Gateway baseline `dea252fe6a5f2018bcb8b2403a223e5a1d3dc919` and firmware source `10f02aa4b63ad2ac3f4eef2d23de86fc755d48e8` with documentation `b7bdc49ffb35487043fec1a72f04e8808a0fd274`, `bash components/gateway_wifi_foundation/test/run_storage_tests.sh` passed the existing host regression executables, NVS variants and SHA-256 vectors. An isolated ESP-IDF v5.5.4 ESP32-S3 baseline build passed with binary `0x19bb70` and app partition headroom `0x64490` (20%). The build does not compile an integrated Wi-Fi service and is not production or HIL evidence. M4W.2D hardware persistence, security, admission ordering, adapter integration and full lifecycle qualification remain outstanding.

## M4W.3C standalone host composition — 2026-11-25

An offline-only, compile-guarded service composition now lives under `components/gateway_wifi_foundation/test/standalone`. It composes the existing codec, transaction, storage and recovery foundations with synthetic in-memory storage, clock, scan and events. Seven new service cases passed normally, under ASan and under UBSan; the existing storage/foundation regression script also passed. This is host qualification, not registration, runtime firmware integration, or evidence of real association or persistent storage. The M4W.3B authorization and ordered carrier-loss handoff blocker remains open. See the standalone README for exact commands and boundaries.
