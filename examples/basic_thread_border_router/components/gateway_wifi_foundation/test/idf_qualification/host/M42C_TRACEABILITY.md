# M4.2C offline case execution trace

Case results below were obtained from individually dispatched host test functions. Selection alone remains NOT_RUN. Detailed per-case assertion counts, process status, host compiler, SDK evidence and source hashes are in `M42C_CASE_EVIDENCE.json`. SDK evidence is not a target compiler or hardware execution.

| Case | Classification | Host binding | Result | Assertions |
| --- | --- | --- | --- | ---: |
| EMPTY | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| SINGLE_A | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| SINGLE_B | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| DUAL_ORDER | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| DUAL_EQUAL | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| BAD_CRC | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| BAD_LENGTH | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| BAD_SCHEMA | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| TORN | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| INT_BEFORE_WRITE | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| INT_DURING_WRITE | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| INT_AFTER_WRITE | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| INT_BEFORE_COMMIT | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| INT_AFTER_COMMIT | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| UNKNOWN_WRITE | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| UNKNOWN_COMMIT | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| UNKNOWN_READBACK | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| RECOVERY_REQUIRED | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| CAP_NORMAL | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| CAP_COMBINED | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| CAP_REPLACE | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| CAP_HIGH | BLOCKED_BY_UNVERIFIED_PREREQUISITE | — | NOT_RUN | 0 |
| CAP_EXHAUST | BLOCKED_BY_UNVERIFIED_PREREQUISITE | — | NOT_RUN | 0 |
| COEXIST | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| GC_REPEAT | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| GC_TRANSITION | FUTURE_POWER_INTERRUPTION_REQUIRED | — | NOT_RUN | 0 |
| STALE_WORKER | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:worker_normal_and_identity | PASS | 57 |
| QUEUED_FENCE | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:worker_queued_restart | PASS | 28 |
| RUNNING_DRAIN | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:worker_running_restart | PASS | 34 |
| UNRESOLVED | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:worker_uncertainty_and_budget | PASS | 36 |
| RESTART_SCAN | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:worker_running_restart | PASS | 34 |
| FIXTURE_MISMATCH | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 11 |
| NO_ENROLLMENT | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 12 |
| WRONG_PARTITION | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 11 |
| WRONG_APP | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 11 |
| NO_AUTH | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 12 |
| PRODUCTION_DENY | OFFLINE_HOST_EXECUTABLE | fixture_case_driver.c:fixture_case | PASS | 16 |
| SYNTHETIC_SECRET | OFFLINE_HOST_EXECUTABLE | worker_case_driver.c:synthetic_secret_case | PASS | 33 |
| SDK_552 | FUTURE_ISOLATED_NVS_REQUIRED | — | NOT_RUN | 0 |
| SDK_554 | BLOCKED_BY_UNVERIFIED_PREREQUISITE | — | NOT_RUN | 0 |

Commands from this host directory: `python3 generate_m42c_report.py`, `python3 test_offline_case_runner.py`, `python3 test_case_catalog.py`, `python3 test_occupancy_model.py`, `bash run_fixture_guard_tests.sh`, `bash run_worker_tests.sh`, `SANITIZE=1 bash run_worker_tests.sh`, and `python3 check_isolation.py /tmp/m42c-locked-build`. Storage and recovery regressions were compiled separately with `cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread`, the original codec, transaction, store and recovery sources, and the respective `test_storage.c` or `test_recovery.c`; recovery additionally used `-DWIFI_RECOVERY_HOST_TEST`. `git diff --check` was run from the repository. See the JSON for gateway revision and target-independent host compiler provenance.

The NO_AUTH case covers missing trusted fixture-preflight approval only; it does not test BLE or Admin authorization. Synthetic secret checks cover captured host-process diagnostics and tested transaction/store candidate retirement, not every memory or logging path. Hardware and real NVS are not exercised. Future fixture authorization, approved image and device identity, isolated partition verification, NVS statistics, page-state observation, fault equipment and restart procedures remain prerequisites described in QUALIFICATION_CASE_PLAN.md. The v5.5.2 locked-app compile-only build was recreated for isolation; the v5.5.2 and v5.5.4 inert adapter builds were not rerun in this milestone. ESP-IDF target builds are separate from this host execution.
