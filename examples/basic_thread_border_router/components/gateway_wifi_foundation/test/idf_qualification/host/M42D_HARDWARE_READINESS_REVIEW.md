# ADR-091 M4W.2B — M4.2D Hardware-Readiness Review

Status: **READINESS_PREPARATION_ACCEPTED / HARDWARE_EXECUTION_NOT_AUTHORIZED**

Reviewed Git commit: `f69afaea6e962eabf0f729930f9a64eb6bdd8213`.
Frozen architecture: ADR-091, source revision
`8720847e1f62b2001a93a3ac436491671404bf77`.
Review method: connected GitHub source inspection and committed M4.2C evidence inspection.
No hardware access, flashing, reset, serial operation, NVS initialization, NVS
mutation, NVS image generation, or fixture enrollment was performed.

## Existing accepted offline qualification

- Catalog v1.0 defines 40 cases: 12 offline, 18 future isolated real-NVS,
  7 future controlled power interruption, and 3 blocked by prerequisites.
- M4.2C evidence records 12 individually dispatched host cases PASS and
  28 future/blocked cases NOT_RUN. This is offline host evidence, not hardware
  execution. Prior independent review matched 16/16 referenced source SHA-256s
  to the reviewed Git commit; the test-run Git HEAD in the evidence is the
  predecessor commit, with individual source hashes matching the published files.
- Operator reported successful compile-only ESP-IDF v5.5.2 and v5.5.4 builds
  following the isolated CMake correction. Build logs were not independently
  inspected. Neither proves real NVS, power-loss, or cross-version NVS-image
  compatibility.

## Readiness findings

| ID | Source observation | Readiness verdict | Required evidence |
| --- | --- | --- | --- |
| F1 | `main/fixture_allowlist.c` is immutable `{NULL,0}`; no physical fixture approved | BLOCKED | Independent external approval of one dedicated disposable ESP32-S3 fixture, with recorded device identity and permitted operations |
| F2 | `partitions.qual.csv` defines test NVS `nvs` at `0x9000/0x6000` and factory `qual_app` at `0x10000/0x100000`; `sdkconfig.defaults` selects a 2 MB test-flash profile | BLOCKED | Approved board-specific flash capacity, image and partition-table compatibility, and proof no operational/customer data is at risk |
| F3 | `fixture_platform.c` uses `esp_partition_get_sha256` for the running `qual_app` image | BLOCKED | Externally approved running-image SHA-256 under this exact digest convention; not ELF/version/name |
| F4 | `main/qualification_main.c` runs locked preflight only; `main/CMakeLists.txt` does not link NVS/storage worker; production NVS adapter has its own disabled synthetic-write switch | PASS: safe default | Preserve locked app, empty allowlist, separate storage/write authorization and owner serialization |
| F5 | No independently approved recovery, bounded drain, synthetic data restoration or controlled power-loss procedure | BLOCKED | Reviewed failure/recovery checklist, instrumentation, expected results and stop conditions |
| F6 | Previous compatibility plan described a v5.5.4 compilation failure | DOC_CORRECTION | Distinguish reported successful isolated builds from unqualified real-NVS and cross-version image compatibility |

The current `QUAL_CAP_PREFLIGHT` capability is **identity preflight only**;
even a matching hypothetical allowlist entry must leave
`STORAGE_ADMISSION: DENIED`, `NVS_INITIALIZATION: DISABLED` and
`STORAGE_MUTATION: NO`. An approval reference string alone is not
authorization. No product installation credentials are permitted.

## Required gate separation

**M4.2D.0 — Documentation-only readiness:** This review. Do not change firmware,
allowlists, partition table, production security, build macros or NVS access.
Any fixture fields not independently obtained remain UNKNOWN/NOT_APPROVED.

**M4.2D.1 — Physical identity inspection:** Requires a separate explicit user
authorization naming the dedicated, disposable fixture and the non-destructive
inspection surface. Before authorization, do not probe hardware, capture device
identifiers, flash, open serial, read eFuses or assume flash geometry. Record
external approvals of physical identity, flash size, partition plan, the exact
running-app SHA-256 convention, and whether storage encryption/security
configuration is appropriate for a synthetic-only experiment. Do not touch the
operational FC61 Gateway.

**M4.2D.2 — Real-NVS isolated qualification:** Requires a *new* independently
reviewed image, explicit authorization of NVS initialization/mutation and
destructive risk, a dedicated partition, approved synthetic-only records,
backups/restore procedure, independently checked `nvs_get_stats()` baseline,
page state, readback, cross-owner preservation and bounded recovery. Approval
to inspect hardware alone never authorizes real-NVS operations. The existing
qualification application has no NVS execution path.

**M4.2D.3 — Power interruption:** Requires additional explicit authorization,
controlled equipment, safe failure/recovery procedures and previously qualified
isolated real-NVS operation. Seven power-interruption cases stay NOT_RUN.

## Stop conditions

Stop and return BLOCKED if fixture identity, image hash, actual flash geometry,
partition identity/device, synthetic-only provenance, data-disposability,
external authorization, recovery procedure, or approved operation scope is
missing or ambiguous. Never permit preflight success to authorize NVS writes.
No `--force`, command-line bypass, mutable on-device approval, or implicit
production registration. ADR-091 and its BLE Local/Admin, confidential credential,
reset, Wi-Fi and Thread boundaries remain frozen.

## Closure statement

`M4_2D_READINESS_REVIEW: COMPLETE`.

`M4_2D_HARDWARE_EXECUTION: NOT_AUTHORIZED`.

`FIXTURE_ENROLLED: NO`.
`REAL_NVS_INITIALIZATION: DISABLED`.
`REAL_NVS_MUTATION: DISABLED`.
`POWER_INTERRUPTION_CASES_EXECUTED: 0`.
`PRODUCTION_REGISTRATION: DISABLED`.

Next gate: explicit identification and external approval of a **disposable**
hardware fixture, followed by a separately scoped, non-destructive inspection
authorization. This document is not that authorization.
