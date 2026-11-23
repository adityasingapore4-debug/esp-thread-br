#!/usr/bin/env python3
"""Execute the twelve fixed offline bindings and generate a 40-case trace."""
import json
from pathlib import Path
import case_catalog as cat
import offline_case_runner as runner

HERE=Path(__file__).resolve().parent
sdk='v5.5.2'; evidence=cat.required_evidence(sdk); results=[]
for case in cat.catalog()['cases']:
 if case['classification']==cat.OFFLINE:
  result=runner.execute(case['id'],sdk,cat.VERSION,evidence)
  print(case['id'],result['CASE_RESULT'],result['ASSERTIONS_EXECUTED'],flush=True)
 else:
  result={'RESULT_SCHEMA_VERSION':runner.SCHEMA,'CASE_ID':case['id'],'CATALOG_VERSION':cat.VERSION,
   'CLASSIFICATION':case['classification'],'SDK_EVIDENCE_VERSION':sdk,'SOURCE_REVISION':cat.occupancy.PIN,
   'GATEWAY_COMMIT':None,'TEST_SOURCE':None,'TEST_ENTRY_POINT':None,'CASE_DEFINED':True,
   'CASE_SELECTED':False,'CASE_EXECUTION_PERMITTED':False,'CASE_EXECUTED':False,'CASE_RESULT':'NOT_RUN',
   'ASSERTIONS_EXECUTED':0,'ASSERTIONS_PASSED':0,'PROCESS_EXIT_CODE':None,'EXECUTION_EVIDENCE':None}
 results.append(result)
runner.validate_results(results)
if len(results)!=40 or sum(r['CASE_RESULT']=='PASS' for r in results)!=12: raise SystemExit('case qualification failed')
(HERE/'M42C_CASE_EVIDENCE.json').write_text(json.dumps({'schema_version':runner.SCHEMA,'sdk_evidence':sdk,'cases':results},indent=2,sort_keys=True)+'\n')
lines=['# M4.2C offline case execution trace','',
 'Case results below were obtained from individually dispatched host test functions. Selection alone remains NOT_RUN. Detailed per-case assertion counts, process status, host compiler, SDK evidence and source hashes are in `M42C_CASE_EVIDENCE.json`. SDK evidence is not a target compiler or hardware execution.','',
 '| Case | Classification | Host binding | Result | Assertions |','| --- | --- | --- | --- | ---: |']
for x in results:
 binding=(x['TEST_SOURCE']+':'+x['TEST_ENTRY_POINT']) if x['CASE_EXECUTED'] else '—'
 lines.append(f"| {x['CASE_ID']} | {x['CLASSIFICATION']} | {binding} | {x['CASE_RESULT']} | {x['ASSERTIONS_PASSED']} |")
lines+=['','Commands from this host directory: `python3 generate_m42c_report.py`, `python3 test_offline_case_runner.py`, `python3 test_case_catalog.py`, `python3 test_occupancy_model.py`, `bash run_fixture_guard_tests.sh`, `bash run_worker_tests.sh`, `SANITIZE=1 bash run_worker_tests.sh`, and `python3 check_isolation.py /tmp/m42c-locked-build`. Storage and recovery regressions were compiled separately with `cc -std=c11 -Wall -Wextra -Werror -pedantic -pthread`, the original codec, transaction, store and recovery sources, and the respective `test_storage.c` or `test_recovery.c`; recovery additionally used `-DWIFI_RECOVERY_HOST_TEST`. `git diff --check` was run from the repository. See the JSON for gateway revision and target-independent host compiler provenance.','',
 'The NO_AUTH case covers missing trusted fixture-preflight approval only; it does not test BLE or Admin authorization. Synthetic secret checks cover captured host-process diagnostics and tested transaction/store candidate retirement, not every memory or logging path. Hardware and real NVS are not exercised. Future fixture authorization, approved image and device identity, isolated partition verification, NVS statistics, page-state observation, fault equipment and restart procedures remain prerequisites described in QUALIFICATION_CASE_PLAN.md. The v5.5.2 locked-app compile-only build was recreated for isolation; the v5.5.2 and v5.5.4 inert adapter builds were not rerun in this milestone. ESP-IDF target builds are separate from this host execution.']
(HERE/'M42C_TRACEABILITY.md').write_text('\n'.join(lines)+'\n')
