# ADR-091 M4W.2B M4.2B.2C — offline occupancy model

Submitted for independent review. No fixture is enrolled and no actual NVS
operation or hardware access occurred. Prior ABI/NimBLE extraction artifacts
remain subject to their own independent review; this model does not broaden
those acceptance claims.

`occupancy_model.py` emits deterministic JSON **descriptions only**: no data
bytes, NVS image, writer, device identity or credential is generated. Seed
`910202` is a reproducible label; no randomized contents or implied valid
installation are generated. Every included record has a unique physical
namespace/key, value type, size, quantity, condition and source revision.
Fixed arrays use full 32-element blob lengths irrespective of logical node
population. Profile output references the fixed and NimBLE evidence with
SDK/compiler/configuration provenance and has explicit feature flags.

The model accepts only v5.5.2 and v5.5.4 evidence with expected SHA-256 values,
provenance, three-bond/eight-CCCD configuration and tested disabled host
privacy/encrypted advertising. Unknown feature settings, schema version,
missing/tampered evidence, duplicate entries, invalid types, out-of-range sizes
and integer overflows fail. The v5.5.2 fixed ABI came from the locked harness
configuration while its NimBLE evidence came from a separate explicit offline
NimBLE configuration. Their ABI-relevant fixed structures were measured against
pinned sources; this is not a single fully integrated production sdkconfig.
The v5.5.4 configuration describes a historical build, not deployed FC61.

Each profile includes optional-record presence conditions. Normal is a declared
representative combination, not an observation or a claim that every optional
record exists in a live Gateway. The NimBLE profile explicitly chooses three
peer security and three local security records plus eight CCCDs; this is one
55-entry scenario, not a universal maximum. Additional CSFC, local IRK and RPA
records are included in the inventory but not silently assumed present.
`p_dev_rec` and `ead_sec` are excluded because the corresponding tested features
are disabled; enabling either is rejected pending evidence.

## Results for either tested SDK configuration

| Profile | Estimated settled live entries | Largest single-record replacement allocation | Modeled obsolete history |
|---|---:|---:|---:|
| EMPTY | 0 | 0 | 0 |
| MINIMAL_INSTALLATION | 23 | 17 | 0 |
| NORMAL_INSTALLATION | 229 | 122 | 0 |
| PROVISIONING_JOURNAL_PRESENT | 28 | 17 | 0 |
| PROVISIONING_JOURNAL_ABSENT | 23 | 17 | 0 |
| MAX_REGISTRY | 175 | 122 | 0 |
| NIMBLE_CONFIGURED_CAPACITY | 55 | 5 | 0 |
| GATEWAY_WIFI_AB | 13 | 6 | 0 |
| REPLACEMENT_PRESSURE | 229 | 122 | 272 |
| COMBINED_OCCUPANCY | 297 | 122 | 0 |
| HIGH_PRESSURE | 682 | 122 | 0 |
| EXHAUSTION | 938 | 122 | 0 |

HIGH_PRESSURE adds 12 hypothetical 960-byte filler descriptions; EXHAUSTION
adds 20. Filler is labelled model-only, not a product owner or valid schema.
EXHAUSTION exceeds the **756 theoretical entries** in six 126-entry pages; it
is not a simulated ESP-IDF failure. Real writable capacity is lower or otherwise
state-dependent: one or more pages and metadata are involved in GC, strings
need contiguous entries, blob chunks may add overhead across pages, and other
owners/old data can consume space. No number in this table is a measured
`nvs_get_stats` result or a production minimum-free threshold.

### Capacity boundary review

The initial model distinguished logical live demand, 756 theoretical physical
slots, obsolete history, and unknown GC/writable capacity, but did not expose
the requested 630-entry conservative planning reference or classify profiles
against it. The classification is now explicit. Neither boundary implies that
the ESP-IDF NVS API will accept any given allocation.

The selected ESP-IDF v5.5.2 implementation at commit
`30aaf64524299d3bde422ca9a2848090d1bc5d0f` defines 126 32-byte entries per
page in `components/nvs_flash/private_include/nvs_constants.h`; `nvs_page.cpp`
defines a 4096-byte sector. Six such pages comprise 756 physical entry slots.
`nvs_pagemanager.cpp` subtracts one page of entries from calculated
`available_entries`, and `requestNewPage` needs a reclaimable page when one
free page remains. Thus five pages / 630 entries is an analytical planning
reference; actual state and API admission are not established by subtraction.

| Analytical classification of settled logical live demand | Profiles |
|---|---|
| Within 630-entry planning reference | EMPTY, MINIMAL_INSTALLATION, NORMAL_INSTALLATION, PROVISIONING_JOURNAL_PRESENT, PROVISIONING_JOURNAL_ABSENT, MAX_REGISTRY, NIMBLE_CONFIGURED_CAPACITY, GATEWAY_WIFI_AB, REPLACEMENT_PRESSURE, COMBINED_OCCUPANCY |
| Above 630 planning reference but within 756 physical slots | HIGH_PRESSURE: 682 |
| Above 756 physical slots | EXHAUSTION: 938 |

Actual NVS admission is UNKNOWN for **every** profile. In particular, neither
the 682 estimate nor the 630 reference is a measured fill state. The 938
estimate exceeds the modeled physical slot count and cannot be represented as
those settled live entries on the specified six-page geometry; it is not a
measurement of a particular NVS error or an empirical capacity boundary.

REPLACEMENT_PRESSURE reports 229 settled live entries, 272 accumulated obsolete
entries in a no-reclamation history model, and 122 entries of additional demand
for its largest replacement. These are distinct quantities; their sum is not a
measurement of simultaneous physical occupancy. GC-reclaimed entries and page/
chunk placement are UNKNOWN. `nvs_storage.cpp` can split a blob over pages and
needs index/chunk metadata; the 3,840-byte, 122-entry single-chunk estimate is
not proof that a contiguous page or a viable multi-page allocation is available.
The model does not declare replacement success from aggregate arithmetic.

The selected SDK headers document one overhead entry plus `ceil(length/32)`
for a string including NUL, and two overhead entries plus `ceil(length/32)` for
a small single-chunk blob. Scalars cost one entry; each distinct created
namespace adds one. Replacement models new allocation separately from obsolete
entries and never assumes immediate reclaim. REPLACEMENT_PRESSURE is a simple
upper history counter without GC: four 132-byte automation table replacements
and two 3,840-byte registry array replacements produce 272 obsolete entries;
individual largest replacement needs 122 entries. It does not imply that those
entries can be allocated contiguously or that their sum remains physically live.
Deletes preserve namespace accounting and count obsolete historical entries.
Real GC headroom and reclaim timing are unknown.

All unmodeled SDK/application namespaces and allocation histories remain an
explicit unresolved capacity limitation. The output exposes `writable_capacity`
and `gc_headroom` as `UNKNOWN` and does not assert cross-version NVS-image
compatibility.

## Files and commands

Added `occupancy_model.py`, `test_occupancy_model.py`, fixed evidence copies
`fixed_abi_v552.json`, `fixed_abi_v554.json`, and this report, within the
unregistered qualification project. Existing `nimble_abi_evidence.json` is
read only; no accepted foundation, guard, allowlist, entry point, partition or
production registration changed.

From `examples/basic_thread_border_router`:

```sh
python3 components/gateway_wifi_foundation/test/idf_qualification/host/test_occupancy_model.py
python3 components/gateway_wifi_foundation/test/idf_qualification/host/occupancy_model.py --sdk v5.5.2 --profile COMBINED_OCCUPANCY
bash components/gateway_wifi_foundation/test/idf_qualification/host/run_fixture_guard_tests.sh
python3 components/gateway_wifi_foundation/test/idf_qualification/host/check_isolation.py /tmp/adr091-m4-2b1-build
git diff --check
git status --short
```

Ten offline test methods pass, covering all twelve profiles under both versions,
deterministic output, arithmetic, capacity boundaries, source-backed lengths,
complete arrays, journal inclusion, supported/unsupported NimBLE features,
missing/tampered evidence, schema version, duplicate keys, type/size rejection,
overflow, history and no NVS/hardware access. Existing fixture guard: 1,680
checks pass; default app stays locked. SDK build metadata/source scope isolation
passes. This phase did not build firmware, run HIL, access hardware, read/write
NVS, measure GC, or generate a production installation image.
