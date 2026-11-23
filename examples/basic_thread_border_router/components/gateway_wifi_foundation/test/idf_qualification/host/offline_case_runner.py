#!/usr/bin/env python3
"""Execute explicitly bound offline cases only; never accesses hardware or NVS."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import case_catalog as cat

HERE=Path(__file__).resolve().parent
PROJECT=HERE.parent
FOUNDATION=PROJECT.parent.parent
REPO=FOUNDATION.parents[3]
SCHEMA='1.0'
WORKER={'STALE_WORKER':'worker_normal_and_identity','QUEUED_FENCE':'worker_queued_restart',
        'RUNNING_DRAIN':'worker_running_restart','UNRESOLVED':'worker_uncertainty_and_budget',
        'RESTART_SCAN':'worker_running_restart','SYNTHETIC_SECRET':'synthetic_secret_case'}
FIXTURE={'FIXTURE_MISMATCH':'fixture_case','NO_ENROLLMENT':'fixture_case','WRONG_PARTITION':'fixture_case',
         'WRONG_APP':'fixture_case','NO_AUTH':'fixture_case','PRODUCTION_DENY':'fixture_case'}
BINDINGS={**{k:('worker_case_driver.c',v) for k,v in WORKER.items()},
          **{k:('fixture_case_driver.c',v) for k,v in FIXTURE.items()}}

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def invoke(cmd): return subprocess.run(cmd,capture_output=True,timeout=60,check=False)

def parse_evidence(case_id, proc):
 if proc.returncode != 0: raise ValueError('host process failed')
 if any(x in proc.stdout or x in proc.stderr for x in (b'j'*8,b'k'*8)):
  raise ValueError('synthetic secret detected in diagnostics')
 markers=re.findall(rb'^CASE_ASSERTIONS:([^:\r\n]+):([0-9]+)$',proc.stdout,re.MULTILINE)
 if len(markers)!=1 or markers[0][0].decode('ascii')!=case_id: raise ValueError('missing, duplicate or inconsistent result evidence')
 count=int(markers[0][1]);
 if count<=0: raise ValueError('no assertions executed')
 return count

def validate_results(results):
 ids=set()
 for r in results:
  if r['CASE_ID'] in ids: raise ValueError('duplicate case result')
  ids.add(r['CASE_ID'])
  canonical={x['id']:x for x in cat.catalog()['cases']}
  if r['CASE_ID'] not in canonical or r['CLASSIFICATION']!=canonical[r['CASE_ID']]['classification']: raise ValueError('inconsistent case identity')
  if r['CASE_RESULT']=='PASS' and (r['CASE_ID'] not in BINDINGS or (r['TEST_SOURCE'],r['TEST_ENTRY_POINT'])!=BINDINGS[r['CASE_ID']]): raise ValueError('inconsistent test binding')
  if r['CASE_RESULT']=='PASS' and not (r['CASE_EXECUTION_PERMITTED'] and r['CASE_EXECUTED'] and r['PROCESS_EXIT_CODE']==0 and r['ASSERTIONS_EXECUTED']>0 and r['ASSERTIONS_EXECUTED']==r['ASSERTIONS_PASSED'] and r['EXECUTION_EVIDENCE']):
   raise ValueError('unsubstantiated PASS')
  if r['CASE_RESULT']=='PASS':
   e=r['EXECUTION_EVIDENCE']
   if not isinstance(e,dict) or not e.get('host_compiler') or not isinstance(e.get('source_sha256'),dict) or not e['source_sha256'] or any(not re.fullmatch('[0-9a-f]{64}',str(e.get(k,''))) for k in ('host_binary_sha256','captured_stdout_sha256','captured_stderr_sha256','sdk_evidence_sha256')): raise ValueError('malformed execution evidence')
   if not r.get('CASE_DEFINED') or not r.get('CASE_SELECTED') or r.get('SOURCE_REVISION')!=cat.occupancy.PIN or not re.fullmatch('[0-9a-f]{40}',str(r.get('GATEWAY_COMMIT',''))): raise ValueError('missing provenance')
  if r['CASE_RESULT'] not in ('PASS','FAIL','NOT_RUN','BLOCKED'): raise ValueError('invalid result')
 return True

def build(binding, output):
 driver=HERE/binding[0]
 flags=['cc','-std=c11','-Wall','-Wextra','-Werror','-pedantic']
 if driver.name=='worker_case_driver.c':
  flags+=['-pthread','-DWIFI_RECOVERY_HOST_TEST','-I'+str(FOUNDATION),'-I'+str(HERE)]
  sources=[FOUNDATION/x for x in ('wifi_codec.c','wifi_transaction.c','wifi_store.c','wifi_recovery.c')]+[HERE/'worker_scheduler.c',driver]
 else:
  flags+=['-I'+str(PROJECT/'main')]
  sources=[PROJECT/'main'/x for x in ('fixture_guard.c','fixture_allowlist.c','result_protocol.c','qualification_main.c')]+[driver]
 proc=invoke(flags+[str(x) for x in sources]+['-o',str(output)])
 if proc.returncode: raise ValueError('host compilation failed')
 return [str(x) for x in sources]

def execute(case_id,sdk,version,evidence, catalog_data=None):
 r={'RESULT_SCHEMA_VERSION':SCHEMA,'CASE_ID':case_id,'CATALOG_VERSION':version,'CLASSIFICATION':None,
    'SDK_EVIDENCE_VERSION':sdk,'SOURCE_REVISION':cat.occupancy.PIN,'GATEWAY_COMMIT':None,
    'TEST_SOURCE':None,'TEST_ENTRY_POINT':None,'CASE_DEFINED':False,'CASE_SELECTED':False,
    'CASE_EXECUTION_PERMITTED':False,'CASE_EXECUTED':False,'CASE_RESULT':'BLOCKED',
    'ASSERTIONS_EXECUTED':0,'ASSERTIONS_PASSED':0,'PROCESS_EXIT_CODE':None,'EXECUTION_EVIDENCE':None}
 try:
  data=json.loads((HERE/'case_catalog.json').read_text()) if catalog_data is None else catalog_data
  selected=cat.select_case(version,case_id,sdk,evidence,data)
  for key in ('CASE_DEFINED','CASE_SELECTED','CASE_EXECUTION_PERMITTED'): r[key]=selected[key]
  matching=[x for x in data['cases'] if x['id']==case_id]
  if len(matching)==1: r['CLASSIFICATION']=matching[0]['classification']
  if not selected['CASE_EXECUTION_PERMITTED']: raise ValueError(selected.get('REJECTION_REASON','selection denied'))
  if case_id not in BINDINGS: raise ValueError('missing test binding')
  binding=BINDINGS[case_id]; r['TEST_SOURCE'],r['TEST_ENTRY_POINT']=binding
  r['GATEWAY_COMMIT']=subprocess.check_output(['git','-C',str(HERE),'rev-parse','HEAD'],text=True).strip()
  compiler=subprocess.check_output(['cc','--version'],text=True).splitlines()[0]
  with tempfile.TemporaryDirectory(prefix='adr091-m42c-',dir='/tmp') as tmp:
   binary=Path(tmp)/'case'; sources=build(binding,binary)
   proc=invoke([str(binary),case_id]); r['CASE_EXECUTED']=True; r['PROCESS_EXIT_CODE']=proc.returncode
   count=parse_evidence(case_id,proc)
   r['ASSERTIONS_EXECUTED']=count; r['ASSERTIONS_PASSED']=count
   extra=([FOUNDATION/'test/test_recovery.c',HERE/'test_worker_integration.c',HERE/'worker_scheduler.h'] if case_id in WORKER else [HERE/'test_fixture_guard.c',PROJECT/'main/fixture_guard.h'])
   r['EXECUTION_EVIDENCE']={'host_compiler':compiler,'host_flags':'C11 -Wall -Wextra -Werror -pedantic'+(' -pthread -DWIFI_RECOVERY_HOST_TEST' if case_id in WORKER else ''),
    'target_compilation':'NOT_PERFORMED','hardware_qualification':'NOT_PERFORMED',
    'sdk_evidence_sha256':hashlib.sha256(json.dumps(evidence,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
    'source_sha256':{str(Path(x).relative_to(FOUNDATION)):sha(Path(x)) for x in sources+extra},'host_binary_sha256':sha(binary),
    'captured_stdout_sha256':hashlib.sha256(proc.stdout).hexdigest(),'captured_stderr_sha256':hashlib.sha256(proc.stderr).hexdigest(),
    'compiled_source_count':len(sources)}
   r['CASE_RESULT']='PASS'
 except (ValueError,KeyError,TypeError,OSError,subprocess.SubprocessError,UnicodeError) as exc:
  r['CASE_RESULT']='FAIL' if r['CASE_EXECUTION_PERMITTED'] else 'BLOCKED'
  r['ERROR']=str(exc)
 if r['CASE_RESULT']=='PASS': validate_results([r])
 return r

def main():
 p=argparse.ArgumentParser(description=__doc__); p.add_argument('--case',required=True); p.add_argument('--version',required=True); p.add_argument('--sdk',required=True); p.add_argument('--evidence',type=Path,required=True)
 a=p.parse_args()
 try: evidence=json.loads(a.evidence.read_text())
 except (OSError,ValueError): evidence=None
 r=execute(a.case,a.sdk,a.version,evidence); print(json.dumps(r,indent=2,sort_keys=True))
 raise SystemExit(0 if r['CASE_RESULT']=='PASS' else 1)
if __name__=='__main__': main()
