import copy
import json
from pathlib import Path
import subprocess
import unittest
from unittest import mock
import case_catalog as c
import offline_case_runner as r

class NegativeTests(unittest.TestCase):
 def setUp(self): self.e=c.required_evidence('v5.5.2')
 def run_case(self,case='STALE_WORKER',version='1.0',sdk='v5.5.2',evidence=None,**kw):
  return r.execute(case,sdk,version,self.e if evidence is None else evidence,**kw)
 def test_unknown_version_sdk_and_missing_evidence(self):
  for args in [('MISSING','1.0','v5.5.2',self.e),('STALE_WORKER','2.0','v5.5.2',self.e),('STALE_WORKER','1.0','v0',self.e),('STALE_WORKER','1.0','v5.5.2',{})]:
   out=self.run_case(*args); self.assertEqual(out['CASE_RESULT'],'BLOCKED'); self.assertFalse(out['CASE_EXECUTED'])
 def test_altered_evidence(self):
  e=copy.deepcopy(self.e); e['fixed_abi_sha256']='0'*64
  self.assertEqual(self.run_case(evidence=e)['CASE_RESULT'],'BLOCKED')
 def test_tampered_classification_and_future(self):
  altered=c.catalog(); case=next(x for x in altered['cases'] if x['id']=='EMPTY'); case['classification']=c.OFFLINE; case['requires_real_nvs']=False
  self.assertEqual(self.run_case('EMPTY',catalog_data=altered)['CASE_RESULT'],'BLOCKED')
  for ident in ('EMPTY','INT_DURING_WRITE','CAP_HIGH'):
   out=self.run_case(ident); self.assertEqual(out['CASE_RESULT'],'BLOCKED'); self.assertFalse(out['CASE_EXECUTED'])
 def test_missing_binding(self):
  with mock.patch.dict(r.BINDINGS,{},clear=True): self.assertEqual(self.run_case()['CASE_RESULT'],'FAIL')
 def test_failed_assertion_or_process(self):
  for code in (-6,9):
   proc=subprocess.CompletedProcess([],code,b'',b'assertion failed')
   with mock.patch.object(r,'build',return_value=[]),mock.patch.object(r,'invoke',return_value=proc):
    out=self.run_case(); self.assertEqual(out['CASE_RESULT'],'FAIL'); self.assertTrue(out['CASE_EXECUTED']); self.assertEqual(out['PROCESS_EXIT_CODE'],code)
 def test_missing_malformed_duplicate_wrong_or_zero_markers(self):
  for output in (b'',b'CASE_ASSERTIONS:STALE_WORKER:no\n',b'CASE_ASSERTIONS:OTHER:3\n',b'CASE_ASSERTIONS:STALE_WORKER:0\n',b'CASE_ASSERTIONS:STALE_WORKER:3\nCASE_ASSERTIONS:STALE_WORKER:3\n',b'j'*8+b'\nCASE_ASSERTIONS:STALE_WORKER:3\n'):
   with self.assertRaises(ValueError): r.parse_evidence('STALE_WORKER',subprocess.CompletedProcess([],0,output,b''))
 def test_duplicate_inconsistent_and_unexecuted_pass(self):
  sample={'CASE_ID':'STALE_WORKER','CLASSIFICATION':c.OFFLINE,'CASE_RESULT':'NOT_RUN','CASE_EXECUTION_PERMITTED':False,'CASE_EXECUTED':False,'PROCESS_EXIT_CODE':None,'ASSERTIONS_EXECUTED':0,'ASSERTIONS_PASSED':0,'EXECUTION_EVIDENCE':None,'TEST_SOURCE':None,'TEST_ENTRY_POINT':None}
  with self.assertRaises(ValueError): r.validate_results([sample,sample])
  bad=copy.deepcopy(sample); bad['CLASSIFICATION']=c.NVS
  with self.assertRaises(ValueError): r.validate_results([bad])
  bad=copy.deepcopy(sample); bad['CASE_RESULT']='PASS'; bad['TEST_SOURCE'],bad['TEST_ENTRY_POINT']=r.BINDINGS['STALE_WORKER']
  with self.assertRaises(ValueError): r.validate_results([bad])
  bad.update(CASE_DEFINED=True,CASE_SELECTED=True,CASE_EXECUTION_PERMITTED=True,CASE_EXECUTED=True,PROCESS_EXIT_CODE=0,ASSERTIONS_EXECUTED=1,ASSERTIONS_PASSED=1,EXECUTION_EVIDENCE={'invalid':True})
  with self.assertRaises(ValueError): r.validate_results([bad])
  bad['TEST_ENTRY_POINT']='wrong_binding'
  with self.assertRaises(ValueError): r.validate_results([bad])
 def test_selector_never_executes(self):
  out=c.select_case('1.0','STALE_WORKER','v5.5.2',self.e)
  self.assertFalse(out['CASE_EXECUTED']); self.assertEqual(out['CASE_RESULT'],'NOT_RUN')
 def test_cli_invalid_has_no_fallback(self):
  p=subprocess.run(['python3',str(Path(r.__file__)),'--case','NONE','--version','1.0','--sdk','v5.5.2','--evidence','/nonexistent'],capture_output=True,text=True)
  self.assertNotEqual(p.returncode,0); self.assertFalse(json.loads(p.stdout)['CASE_EXECUTED'])
if __name__=='__main__': unittest.main()
