# M4.2B.3B offline qualification case plan

`case_catalog.json` is the deterministic version 1.0 machine-readable output of
`python3 case_catalog.py --catalog`. Regenerate it with that command. The
catalog defines future tests; it is not a fixture generator, NVS image, or
execution backend. `test_case_catalog.py` checks the checked-in JSON against
that output and rejects altered metadata. The selector has no execution switch:
it reports `CASE_EXECUTED: false` and `CASE_RESULT: NOT_RUN`, even when it
permits an offline host case. Offline tests in the accepted worker and fixture
runners execute independently and are not automatically attributed to catalog
cases. Selection needs the exact version, case ID, SDK and JSON evidence map
returned by `required_evidence(sdk)`; the function validates the pinned fixed and
NimBLE evidence through `occupancy_model.evidence`. Only the recorded ESP32-S3
configurations with three bonds, eight CCCDs, host privacy disabled and
advertising encryption disabled are supported. No runtime override enables a
future case. A caller-provided catalog must equal the canonical catalog.

## Case semantics and authority boundaries

`occupancy_profile` is an analytical capacity-demand reference, **not** an
NVS-image recipe or a requirement that every listed key exists in the case.
`nvs_state` is the case-specific future fixture-state contract: required
record presence, validity and generation. Thus `SINGLE_A` and `SINGLE_B`
each require only one valid committed slot even though both reference the
`GATEWAY_WIFI_AB` capacity model, which counts two slots. The catalog makes
these meanings explicit in its top-level `field_semantics` object. No fixture
state, NVS image or flash contents are constructed by M4.2B.3B.

`DUAL_EQUAL` means two different valid configurations with the same
generation: the accepted storage scan **fails without selecting an
authoritative record**. It does not emit a dedicated conflict diagnostic, so
none is required by this catalog case.

The historical case ID `NO_AUTH` tests **missing trusted fixture preflight
approval** only. The fixture guard is not BLE Local authentication or fresh,
one-use ADR-073 Admin authorization. Passing any fixture guard check never
grants Wi-Fi configuration authority. Gateway Wi-Fi Local/Admin admission and
same-connection proof remain separately gated; no M4W.3 behavior is exercised
or accepted by these case definitions.

## Source-backed contracts and empirical limits

* `wifi_store.c` validates records and slot generations; after an attempted
  write, failed write/commit/readback remains UNKNOWN until verified recovery
  or restart. `wifi_store_nvs.c` gates real writes behind the disabled synthetic
  build switch and latches restart-required errors. `wifi_recovery.c` preserves
  the coordinator's single owner, queued revocation and running-worker drain.
* ESP-IDF v5.5.2 `components/nvs_flash/src/nvs_api.cpp` implements
  `nvs_set_blob` through the handle and `nvs_commit` through `handle->commit()`;
  its source comments that commit is currently a no-op for intermediate cache.
  `nvs_storage.cpp` writes new blob chunks/index before erasing old versions,
  handles out-of-space and can return `ESP_ERR_NVS_REMOVE_FAILED`.
  `nvs_pagemanager.cpp` contains page activation and interrupted recovery logic.
  None of this demonstrates flash behavior in a particular fixture.
* The accepted occupancy profiles describe analytical entry demand, not NVS API
  admission, measured garbage-collection headroom, or simultaneous physical
  occupancy. High pressure and exhaustion remain blocked as fixture prerequisites.
  Actual writes, torn records, page transitions and interruption timing require
  separate empirical qualification. Cross-version NVS-image compatibility is
  not established. ESP-IDF v5.5.4 inert compilation remains unverified due to its
  documented `stdatomic.h` / `-pedantic -Werror` diagnostic.

## Prerequisites for a future independently authorized milestone

1. Explicit authorization of a dedicated disposable ESP32-S3 fixture. The
   operational FC61 Gateway is not an approved fixture. Record verified hardware
   identity and an externally approved application/image identity.
2. Verify the isolated test partition's device, label, type, offset, size,
   encryption/security configuration, partition table, and separation from all
   production data. Establish an independently reviewed, synthetic-only NVS
   initialization procedure, storage configuration and destructive authority.
3. Obtain actual `nvs_get_stats()` baseline and independent flash capacity and
   page-state observations; demonstrate profile setup feasibility. Aggregate
   counts alone do not prove placement, garbage collection or API admission.
4. Approve deterministic power-interruption equipment, fault points, restart
   and recovery procedure, and bounded worker completion and fencing behavior.
5. Define synthetic-only shared-NVS preservation comparisons before and after
   replacement, garbage collection and restart. Record results and anomalies.
6. Assess production storage-security limitations, including credential
   confidentiality, encryption, image identity and ownership, separately from
   this offline fixture design. Never use installation credentials.

No hardware access, NVS initialization, read, write, image generation, fixture
selection, or enrollment is authorized by this plan.
