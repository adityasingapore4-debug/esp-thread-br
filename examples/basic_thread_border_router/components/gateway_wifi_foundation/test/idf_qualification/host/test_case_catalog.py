#!/usr/bin/env python3
import copy
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest import mock
import case_catalog as catalog
import occupancy_model as occupancy

HERE=Path(__file__).resolve().parent

class CatalogTests(unittest.TestCase):
 def test_determinism_and_schema(self):
  one=subprocess.check_output([sys.executable,str(HERE/'case_catalog.py'),'--catalog'])
  two=subprocess.check_output([sys.executable,str(HERE/'case_catalog.py'),'--catalog'])
  self.assertEqual(one,two)
  self.assertEqual(one,(HERE/'case_catalog.json').read_bytes())
  data=json.loads(one); self.assertEqual(data['catalog_version'],catalog.VERSION)
  cases=data['cases']; ids=[x['id'] for x in cases]
  self.assertEqual(len(ids),len(set(ids)))
  self.assertTrue({'EMPTY','DUAL_ORDER','BAD_CRC','BAD_LENGTH','BAD_SCHEMA','TORN','INT_BEFORE_WRITE','INT_DURING_WRITE','INT_AFTER_WRITE','INT_BEFORE_COMMIT','INT_AFTER_COMMIT','UNKNOWN_WRITE','UNKNOWN_COMMIT','UNKNOWN_READBACK','RECOVERY_REQUIRED','CAP_NORMAL','CAP_COMBINED','CAP_REPLACE','CAP_HIGH','CAP_EXHAUST','COEXIST','GC_REPEAT','GC_TRANSITION','STALE_WORKER','QUEUED_FENCE','RUNNING_DRAIN','UNRESOLVED','RESTART_SCAN','FIXTURE_MISMATCH','NO_ENROLLMENT','WRONG_PARTITION','WRONG_APP','NO_AUTH','PRODUCTION_DENY','SYNTHETIC_SECRET','SDK_552','SDK_554'} <= set(ids))
  for c in cases:
   for key in ('id','objective','sdk','occupancy_profile','nvs_state','sequence','invariant','observable','fault_point','classification','requires_real_nvs','requires_hardware','requires_controlled_restart','requires_power_interruption','executable_in_m4_2b_3b','prohibition_reason','record_size_evidence'): self.assertIn(key,c)
   self.assertIn(c['occupancy_profile'],occupancy.PROFILES)
   self.assertEqual(c['executable_in_m4_2b_3b'],c['classification']==catalog.OFFLINE)
   self.assertEqual(c['requires_hardware'],c['classification']!=catalog.OFFLINE)
   if c['classification']!=catalog.OFFLINE: self.assertTrue(c['prohibition_reason'])
 def test_case_contracts_and_authority_boundaries(self):
  data=catalog.catalog()
  self.assertEqual(data['field_semantics'],catalog.FIELD_SEMANTICS)
  self.assertIn('capacity-demand reference',data['field_semantics']['occupancy_profile'])
  self.assertIn('not an exact NVS fixture recipe',data['field_semantics']['occupancy_profile'])
  self.assertIn('governs setup',data['field_semantics']['nvs_state'])
  cases={case['id']:case for case in data['cases']}
  self.assertEqual(len(cases),40)
  for cls,expected in [(catalog.OFFLINE,12),(catalog.NVS,18),(catalog.POWER,7),(catalog.BLOCKED,3)]:
   self.assertEqual(sum(1 for case in cases.values() if case['classification']==cls),expected)
  equal=cases['DUAL_EQUAL']
  self.assertEqual(equal['observable'],'scan fails; no authoritative record selected')
  self.assertEqual(equal['classification'],catalog.NVS)
  no_auth=cases['NO_AUTH']
  self.assertIn('fixture preflight',no_auth['objective'])
  self.assertIn('fixture guard',no_auth['sequence'])
  self.assertIn('never grants BLE Local or ADR-073 Admin authority',no_auth['invariant'])
  self.assertIn('Wi-Fi Admin authorization not tested',no_auth['observable'])
  self.assertEqual(no_auth['classification'],catalog.OFFLINE)
  for single in ('SINGLE_A','SINGLE_B'):
   self.assertEqual(cases[single]['occupancy_profile'],'GATEWAY_WIFI_AB')
   self.assertIn('only',cases[single]['nvs_state'])
  self.assertFalse(any(case['executable_in_m4_2b_3b'] for case in cases.values() if case['classification']!=catalog.OFFLINE))

 def test_source_backed_sizes_and_configs(self):
  for sdk in ('v5.5.2','v5.5.4'):
   fixed,nim=occupancy.evidence(sdk,catalog.FEATURES)
   self.assertEqual((fixed['sizes_bytes']['wifi_min'],fixed['sizes_bytes']['wifi_max']),(25,111))
   self.assertEqual(nim['abi']['config_host_privacy'],0)
   self.assertEqual(nim['abi']['config_enc_adv'],0)
 def test_selector_all_cases(self):
  for case in catalog.catalog()['cases']:
   for sdk in case['sdk']:
    result=catalog.select_case(catalog.VERSION,case['id'],sdk,catalog.required_evidence(sdk))
    self.assertTrue(result['CASE_DEFINED']); self.assertTrue(result['CASE_SELECTED'])
    self.assertEqual(result['CASE_EXECUTION_PERMITTED'],case['classification']==catalog.OFFLINE)
    self.assertFalse(result['CASE_EXECUTED']); self.assertEqual(result['CASE_RESULT'],'NOT_RUN')
 def test_unknown_and_missing(self):
  good=catalog.required_evidence('v5.5.2')
  for version,ident,sdk,evidence in [('2','STALE_WORKER','v5.5.2',good),(catalog.VERSION,'missing','v5.5.2',good),(catalog.VERSION,'STALE_WORKER','v9',good),(catalog.VERSION,'STALE_WORKER','v5.5.2',None),(catalog.VERSION,'SDK_552','v5.5.4',catalog.required_evidence('v5.5.4'))]:
   self.assertFalse(catalog.select_case(version,ident,sdk,evidence)['CASE_EXECUTION_PERMITTED'])
 def test_disabled_features_and_missing_provenance(self):
  for key,value in [('host_privacy',True),('encrypted_advertising',True),('bonds',4),('cccds',9)]:
   e=catalog.required_evidence('v5.5.2'); e['features'][key]=value
   self.assertFalse(catalog.select_case(catalog.VERSION,'STALE_WORKER','v5.5.2',e)['CASE_EXECUTION_PERMITTED'])
  with mock.patch.object(occupancy,'evidence',side_effect=FileNotFoundError('missing')):
   self.assertFalse(catalog.select_case(catalog.VERSION,'STALE_WORKER','v5.5.2',{})['CASE_EXECUTION_PERMITTED'])
  for key in catalog.required_evidence('v5.5.2'):
   e=catalog.required_evidence('v5.5.2'); del e[key]
   self.assertFalse(catalog.select_case(catalog.VERSION,'STALE_WORKER','v5.5.2',e)['CASE_EXECUTION_PERMITTED'])
 def test_metadata_tampering_cannot_enable(self):
  good=catalog.required_evidence('v5.5.2')
  for original in catalog.catalog()['cases']:
   if original['classification']==catalog.OFFLINE: continue
   changed=copy.deepcopy(catalog.catalog())
   c=next(x for x in changed['cases'] if x['id']==original['id'])
   c.update(classification=catalog.OFFLINE,requires_real_nvs=False,requires_hardware=False,requires_power_interruption=False,executable_in_m4_2b_3b=True)
   self.assertFalse(catalog.select_case(catalog.VERSION,c['id'],'v5.5.2',good,changed)['CASE_EXECUTION_PERMITTED'])
 def test_no_backend_and_allowlist_unchanged(self):
  script=(HERE/'case_catalog.py').read_text()
  for forbidden in ('import serial','import subprocess','nvs_flash_init(','nvs_set_blob(','nvs_get_blob('): self.assertNotIn(forbidden,script)
  self.assertIn('{NULL,0}',(HERE.parent/'main/fixture_allowlist.c').read_text())
  self.assertNotIn('nvs_flash',(HERE.parent/'main/CMakeLists.txt').read_text())
 def test_cli_denies_without_evidence(self):
  p=subprocess.run([sys.executable,str(HERE/'case_catalog.py'),'--select','STALE_WORKER','--version',catalog.VERSION,'--sdk','v5.5.2'],capture_output=True,text=True)
  self.assertEqual(p.returncode,1); self.assertFalse(json.loads(p.stdout)['CASE_EXECUTION_PERMITTED'])
if __name__=='__main__': unittest.main()
