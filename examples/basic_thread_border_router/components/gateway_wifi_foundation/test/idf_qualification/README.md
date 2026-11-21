# ADR-091 M4W.2B M4.2B.1 — locked qualification foundation

Standalone ESP32-S3 project, submitted for independent review. No device is
enrolled. Do not flash or run this image on hardware in this milestone.

The entry point prints a bounded preflight record and exits. Its compiled-in
trusted allowlist is empty, so the default path returns before any hardware
identity provider is invoked. NVS initialization, NVS mutation and destructive
cases are absent, even after a hypothetical positive identity preflight.

## Safety boundary

- Fixture description: `ADR091_S3_NVS_QUAL_01`; this is not hardware enrollment.
- The trusted allowlist is an immutable application input, currently `{NULL,0}`.
  No command, mutable device manifest, file or NVS record can populate it.
- A later externally approved entry must supply a silicon MAC, ESP32-S3 model,
  physical flash size, exact internal-flash partition identity, approved running
  application digest, external approval reference and PREFLIGHT-only capability.
- Missing/invalid entries and duplicate silicon identities fail closed. Only
  PREFLIGHT is supported; all other requested or allowlisted capabilities fail.
- Provider failures and every identity/geometry mismatch block preflight.
- Image identity uses `esp_partition_get_sha256` on the actual running factory
  application, after checking its label/address/size and internal flash device.
  ESP-IDF verifies an appended app digest when present. This is not an ELF hash
  or necessarily `sha256sum` of the complete `.bin` file. Expected identity must
  be externally approved under the same digest convention.
- Actual enrollment and its approval-record delivery are intentionally absent.
  A final application's own digest cannot simply be embedded into that same
  hashed application. A later independent review must approve how a trusted
  external allowlist reaches the fixed image without self-asserted enrollment;
  this milestone implements the guard interface and empty policy only.
- Successful host-only identity controls return PREFLIGHT_ONLY. They do not
  unlock storage. There is no storage permission token or API to enable NVS.
- The test app has no command interpreter, worker, occupancy fixture, radio,
  Gateway service, reset path, or enrollment bypass.

The proposed **test-only** table contains `nvs` at `0x9000`, size `0x6000`, and
factory application `qual_app` at `0x10000`, size `0x100000`. It is unrelated to
the operational Gateway partition table and remains subject to independent
layout review. A 2 MiB build header is a test-image configuration, not approval
of any physical device or an enrollment default.

## Files

- `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.qual.csv`: separate project
  and proposed test-image layout; only ESP32-S3 is supported.
- `main/CMakeLists.txt`: explicit five-source registration; no accepted storage
  sources or production directories are included.
- `main/qualification_main.c`: locked entry point, fixed preflight only.
- `main/fixture_allowlist.c`: deliberately empty immutable policy.
- `main/fixture_guard.[ch]`: portable, bounded, read-only verification interface.
- `main/fixture_platform.[ch]`: ESP-IDF read-only identity/geometry observations;
  compiled against real headers, not executed on hardware.
- `main/result_protocol.[ch]`: bounded fixed-field diagnostics. No arbitrary
  strings, MACs, hashes, raw records, credentials or private material are output.
- `host/test_fixture_guard.c`: mocked hardware/partition provider, including
  the unchanged application entry point linked against a host provider.
- `host/run_fixture_guard_tests.sh`: strict C11 host fixture tests only.
- `host/check_isolation.py`: read-only source, scope and optional SDK build
  component-isolation checks. No HIL imports or hardware access.

## Validation performed

Run from the Gateway example directory:

```bash
bash components/gateway_wifi_foundation/test/idf_qualification/host/run_fixture_guard_tests.sh
python3 components/gateway_wifi_foundation/test/idf_qualification/host/check_isolation.py
source /home/aditya/esp/esp-idf/export.sh >/tmp/adr091-m4-2b1-idf-export.log 2>&1 && idf.py -C components/gateway_wifi_foundation/test/idf_qualification -B /tmp/adr091-m4-2b1-build -D SDKCONFIG=/tmp/adr091-m4-2b1-sdkconfig -D IDF_TARGET=esp32s3 build >/tmp/adr091-m4-2b1-build.log 2>&1
python3 components/gateway_wifi_foundation/test/idf_qualification/host/check_isolation.py /tmp/adr091-m4-2b1-build
git diff --check
git status --short
```

Host fixture suite: **1,680 checks PASS**, using C11 `-Wall -Wextra -Werror
-pedantic`. Cases cover missing enrollment; self-asserted manifest; wrong
silicon/target/capacity; wrong partition label/type/subtype/offset/size/device;
wrong app identity; unauthorized capabilities; invalid/missing allowlist fields;
provider failures; output bounds; and default application startup. A positive
synthetic identity is also verified, with storage admission still DENIED.
All these host paths link no NVS implementation and no hardware provider.

ESP-IDF **v5.5.2 / ESP32-S3** compile, link, binary generation and partition-size
checks: PASS. Binary size `0x2c0a0`, in a `0x100000` factory slot. Generated
component metadata confirms no NVS, Wi-Fi, Bluetooth, OpenThread, Gateway or
accepted storage component is selected. No synthetic-write macro is enabled.
The initial formatter's SDK compilation rejected vendor stdio's `#include_next`
under pedantic diagnostics; the formatter now uses bounded literal assembly and
retains portable pedantic compilation. The IDF platform integration uses C11
with strict warnings against vendor headers.

Production isolation: all added files are confined to this new directory.
Existing Gateway source/configuration, accepted codecs/FSM/storage/recovery,
headers, inert compilation project and operational partition table are unchanged.

Not executed: existing storage/recovery suites, sanitizers, live NVS, hardware
identity reads, HIL, serial access, flashing, reset, occupancy or fault injection.
Only fixture tests, static checks and the isolated build were authorized here.

## Evidence and remaining gate

Gateway starting HEAD: `ecc083dfa7a01590ba2d2190b7bf5369d0022a04`, branch
`dev/g2-dev-1b-lan-bringup`, initially clean. Architecture documents were read
from `impl/adr090-cib-v1-core` at `8720847e1f62b2001a93a3ac436491671404bf77`
through Git without switching the unrelated dirty architecture checkout. M3
acceptance and the M4.2A design supplied in the conversation govern this scope.

All build artifacts, sdkconfig and logs are under `/tmp`. No device has been
authorized or accessed. No architecture, operational Gateway, security policy,
encryption, production registration or `0x42` registration changed. No commit
or push performed. Next gate: `M4_2B_1_INDEPENDENT_REVIEW`; no later milestone
or fixture enrollment is implemented or authorized by this result.
