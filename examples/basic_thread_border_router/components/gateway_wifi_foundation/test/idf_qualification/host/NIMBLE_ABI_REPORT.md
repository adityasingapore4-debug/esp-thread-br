# ADR-091 M4W.2B M4.2B.2B — NimBLE offline storage evidence

Independent review pending. No runtime, NVS-image compatibility, deployed
occupancy, confidentiality or power-loss claim is made.

The companion `extract_nimble_abi.py` compiles each original `ble_store_nvs.c`
translation unit with its own SDK's ESP32-S3 compiler, SDK headers, generated
configuration and compiler command. It includes the original source; no private
SDK structure is reproduced. Target object files are temporary and never linked
or executed. `nimble_abi_evidence.json` contains output of two identical runs,
SDK/NimBLE commits, compiler versions, sdkconfig/command/source hashes, sizes,
alignments and non-bitfield offsets.

v5.5.2: IDF `30aaf64524299d3bde422ca9a2848090d1bc5d0f`, NimBLE
`9551ac31af0348d7de6cbe7527de3e5ba205460d`, GCC 14.2.0
`esp-14.2.0_20251107`. Configuration is an explicitly generated **offline test
profile**, not the existing M4.2B.1 locked sdkconfig (which excludes BT).
It enables NimBLE, security and NVS persistence, with three bonds/eight CCCDs.
The temporary test project `/tmp/adr091-nimble552` was configured using IDF
`reconfigure` only; it was never built into a firmware image. Its defaults are:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_NVS_PERSIST=y
CONFIG_BT_NIMBLE_MAX_BONDS=3
CONFIG_BT_NIMBLE_MAX_CCCDS=8
```

v5.5.4: IDF `735507283d5b2f9fb363a1901172dbd9e847945d`, NimBLE
`5c1a43ca3d205c3d5183e57dedce335fb6a81155`, GCC 14.2.0
`esp-14.2.0_20260121`; the historical Gateway `build/dev1b-dma` config enables
NimBLE security/NVS persistence and has three bonds and eight CCCDs. Current
FC61 deployment and occupancy remain unverified. For both target configurations,
preprocessor-derived `BLE_HOST_BASED_PRIVACY=0`, `ENC_ADV_DATA=0`, and
`BLE_STORE_MAX_CSFCS=3`; `bond_count` exists under the tested maximum-bond
configuration. Other options and SDK revisions require separate extraction.

## Actual storage path

Both inspected SDKs map indexed keys to NVS namespace `nimble_bond` and call
`nvs_set_blob(handle,key,value,required_size)` followed by `nvs_commit`. The
write dispatch passes the actual struct address and `sizeof` for each type:
this path persists target-memory representation, including ABI padding/bitfields,
not a stable wire encoding. Exact indexed names are `peer_sec_<index>`,
`our_sec_<index>`, `cccd_sec_<index>`, `csfc_sec_<index>`, `local_irk_<index>`,
`rpa_rec_<index>`; `ead_sec_<index>` is conditional. With host privacy enabled,
`p_dev_rec_<index>` is separately persisted under a conditional path and must be
measured using a privacy-enabled configuration. Tests here do not establish its
size, nor EAD size. Both are disabled for these two measured configurations.

The SDK searches indexed records for an available/matching index, writes new
records, and uses `nvs_erase_key` plus commit for selected deletion. Updates and
replacement depend on host store reconciliation and may involve multiple NVS
operations; do not assume a single atomic bond-level replacement. v5.5.4 adds
size checks at several read sites and changes key formatting from sprintf to
snprintf. Identical measured layouts do not prove identical behavior.

Universal Node `ble/key` (16-byte blob) and `ble/key_ver` (u32), and
`ble_pair/user_key` (u32) are separate owner namespaces, not NimBLE records.
Source: pinned Universal Node firmware `8720847e1f62b2001a93a3ac436491671404bf77`,
`components/ble_bridge/src/ble_key_manager.c` and
`components/ble_pairing_policy/ble_pairing_policy.c`.

## Observed compile-only layouts for both tested configurations

| Key family | Target type | Serialized bytes | Alignment | Estimated blob entries |
|---|---|---:|---:|---:|
| `peer_sec_i`, `our_sec_i` | `ble_store_value_sec` | 88 | 8 | 5 |
| `cccd_sec_i` | `ble_store_value_cccd` | 16 | 4 | 3 |
| `csfc_sec_i` | `ble_store_value_csfc` | 8 | 1 | 3 |
| `local_irk_i` | `ble_store_value_local_irk` | 23 | 1 | 3 |
| `rpa_rec_i` | `ble_store_value_rpa_rec` | 14 | 1 | 3 |

The exact measured selected member offsets are in the JSON. No deployed values
or bytes were inspected. The C type contains security material, but the evidence
contains only lengths and offsets. Both tested SDK configs produce identical
sizes, alignments and selected offsets; the sources and SDK revisions differ.
This is ABI evidence only, not a proof of padding bytes, semantic or NVS-image
compatibility. Formatting of NVS keys, raw representation and compile-time
configuration are relevant compatibility constraints.

## Offline entry budget model

For the new-format single-chunk blob case, an estimate is `2 + ceil(bytes/32)`
entries per blob plus one namespace entry when created. It is a mathematical
model, not `nvs_get_stats`. It excludes additional optional records, old formats,
page fragmentation, other users, metadata/namespace overhead beyond the one
entry, and physical garbage-collection reserve. `peer_sec` and `our_sec` are
independently optional; one bond does not necessarily create both.

- No bonds/CCCD/identity records: 0 entries if namespace absent, 1 if present.
- One security record: 1 namespace + 5 = 6; one peer and one local record: 11.
- Three peer and three local security records: 1 + 6*5 = 31 entries.
- Zero CCCDs adds 0; eight CCCDs add 8*3 = 24 entries.
- Maximum six security records and eight CCCDs together: 55 entries, excluding
  CSFC/identity/RPA/other records. Three CSFC records add up to 9 entries;
  each local IRK or RPA blob adds 3, subject to actual feature/use counts.
- Updating/replacing one blob may require new entries while old data still
  exists; account at least for one additional new blob allocation, and measure
  actual NVS behavior under fragmentation/GC. Deletion invalidates entries and
  does not prove immediate flash reclamation. Concurrent shared-NVS activity,
  temporary copies and a usable GC page make exact headroom state-dependent.
  No production free-entry threshold is set.

## Executed validation

From the Gateway example directory:

```
python3 components/gateway_wifi_foundation/test/idf_qualification/host/extract_nimble_abi.py > /tmp/adr091-nimble-abi-first.json
python3 components/gateway_wifi_foundation/test/idf_qualification/host/extract_nimble_abi.py > /tmp/adr091-nimble-abi-second.json
cmp /tmp/adr091-nimble-abi-first.json /tmp/adr091-nimble-abi-second.json
bash components/gateway_wifi_foundation/test/idf_qualification/host/run_fixture_guard_tests.sh
python3 components/gateway_wifi_foundation/test/idf_qualification/host/check_isolation.py /tmp/adr091-m4-2b1-build
git diff --check
git status --short
```

Two outputs were identical; fixture guard 1,680 checks passed, production source
isolation passed. No physical device, NVS read/write/init, HIL, flash or reset
was accessed. No fixture, production service or synthetic occupancy was enabled.
Reproduction needs the pinned SDKs/toolchains, pinned Gateway build database and
separately configured `/tmp/adr091-nimble552-build` with the above defaults.
