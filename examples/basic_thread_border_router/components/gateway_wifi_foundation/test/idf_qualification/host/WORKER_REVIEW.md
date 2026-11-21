# M4W.2B M4.2B.3A offline worker review candidate

Status: **PARTIAL**, submitted for independent source review, not acceptance.
The host scheduler copies jobs and runs the accepted recovery worker against the
existing in-memory `wifi_store_io_t` mock. It does not link the real NVS adapter.
The owner polls and delivers results. The locked qualification application is
separate and remains without a worker or NVS component.

Previously executed offline validation (not rerun for this publication):

| Check | Result |
| --- | --- |
| Worker integration | 171 checks passed |
| Recovery regression | 2,067 checks passed |
| Storage regression | 4,431 checks passed |
| Fixture guard | 1,680 checks passed |
| Occupancy model | 12 tests passed |
| AddressSanitizer and UndefinedBehaviorSanitizer | PASS, combined worker run |
| ESP-IDF v5.5.2 isolated compile project | PASS |
| ESP-IDF v5.5.4 isolated compile project | FAIL |
| Locked qualification application v5.5.2 build | PASS |
| Fixed ABI extraction versus recorded v5.5.2 and v5.5.4 evidence | Identical |

The v5.5.4 failure occurred compiling `gateway_wifi_foundation/wifi_recovery.c`
in the unchanged `test/idf_compile` project. SDK revision:
`735507283d5b2f9fb363a1901172dbd9e847945d` (`v5.5.4`); compiler:
`xtensa-esp32s3-elf-gcc` crosstool-NG `esp-14.2.0_20260121`, GCC 14.2.0.
The relevant compile invocation, with generated include paths and output paths
omitted, was:

```text
xtensa-esp32s3-elf-gcc @/tmp/m43a-idf554/toolchain/cflags ... -Werror=all ... -std=gnu17 ... -std=c11 -Werror -pedantic ... -c .../gateway_wifi_foundation/wifi_recovery.c
```

The first meaningful diagnostic was:

```text
components/newlib/platform_include/stdatomic.h:7:2: error: #include_next is a GCC extension [-Werror]
```

The SDK header's `#include_next` triggered the pedantic diagnostic, and
`-Werror` made it fatal. A previous retry attempted a warning-option adjustment
without changing project sources and failed with the same diagnostic. Neither
captured build log contained another compiler `error:` or `fatal error:` before
the build stopped; this does not establish that no later errors exist. No
v5.5.4 build compatibility or NVS-image compatibility is claimed.

The prior run used `host/run_worker_tests.sh` with and without `SANITIZE=1`,
`host/run_fixture_guard_tests.sh`, `host/test_occupancy_model.py`, the existing
host storage/recovery regression sources, the two fixed ABI extractors, and
`host/check_isolation.py` with the isolated qualification build directory.
`WIFI_NVS_ISOLATED_SYNTHETIC_TEST` was absent from both ESP-IDF compile-command
databases. No fixture was enrolled, no hardware was accessed, and no real NVS
was initialized or mutated.
