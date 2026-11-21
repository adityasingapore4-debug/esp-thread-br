import copy
import json
import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch
import occupancy_model as m

class OccupancyTests(unittest.TestCase):
 def test_determinism_all_profiles(self):
  for v in ('v5.5.2','v5.5.4'):
   for p in m.PROFILES:
    a=m.generate(v,p);b=m.generate(v,p)
    self.assertEqual(json.dumps(a,sort_keys=True),json.dumps(b,sort_keys=True))
    self.assertEqual(a['fixture_kind'],'SYNTHETIC_METADATA_ONLY_NOT_AN_INSTALLATION');self.assertEqual(a['nvs_io'],'NONE')
    self.assertTrue(all(r['synthetic'] for r in a['records']))
    self.assertEqual(a['gc_headroom'],'UNKNOWN');self.assertEqual(a['writable_capacity'],'UNKNOWN')
 def test_entries(self):
  for typ,n,expected in [('u8',1,1),('u32',4,1),('str',1,2),('str',32,2),('str',33,3),('blob',1,3),('blob',32,3),('blob',33,4),('blob',88,5),('blob',111,6)]: self.assertEqual(m.entries(typ,n),expected)
  for typ,n in [('u8',2),('u32',1),('str',4001),('blob',508001),('blob',0),('z',3)]:
   with self.assertRaises(ValueError):m.entries(typ,n)
 def test_fixed_inventory(self):
  f,n=m.evidence('v5.5.2');i=m.inventory(f,n)
  for key,size in [(('prov_v2','state'),460),(('prov_v2','journal'),80),(('node_reg','entries'),3840),(('node_reg','membership'),1536),(('auto','table'),132),(('meta','node_meta'),80),(('ble','key'),16),(('sec_root','root'),32)]:self.assertEqual(m.record(i,*key)['length'],size)
  self.assertEqual(m.generate('v5.5.2','MAX_REGISTRY')['settled_live_entries'],175)
  with self.assertRaises(ValueError):m.record(i,'node_reg','entries',length=120)
  with self.assertRaises(ValueError):m.record(i,'node_reg','entries',kind='u32')
  with self.assertRaises(ValueError):m.record(i,'node_reg','nonexistent')
 def test_journal(self):
  a=m.generate('v5.5.2','PROVISIONING_JOURNAL_PRESENT');b=m.generate('v5.5.2','PROVISIONING_JOURNAL_ABSENT')
  self.assertEqual(a['settled_live_entries']-b['settled_live_entries'],5)
  self.assertNotIn('journal',[r['key'] for r in b['records']])
 def test_nimble(self):
  x=m.generate('v5.5.4','NIMBLE_CONFIGURED_CAPACITY');self.assertEqual(x['settled_live_entries'],55)
  self.assertEqual(len(x['records']),14); self.assertEqual(len({r['key'] for r in x['records']}),14)
  self.assertFalse(any(r['key'].startswith(('ead_','p_dev_rec_')) for r in x['records']))
  with self.assertRaises(ValueError):m.generate('v5.5.4','EMPTY',features={'host_privacy':True,'encrypted_advertising':False,'bonds':3,'cccds':8})
  with self.assertRaises(ValueError):m.generate('v5.5.4','EMPTY',features={'host_privacy':False,'encrypted_advertising':True,'bonds':3,'cccds':8})
 def test_missing_evidence(self):
  with patch.object(m,'HERE',m.HERE/'missing'):
   with self.assertRaises(FileNotFoundError):m.evidence('v5.5.2')
  with self.assertRaises(ValueError):m.evidence('v5.5.3')
  with self.assertRaises(ValueError):m.generate('v5.5.2','EMPTY',features={})
 def test_tampered_evidence_and_schema_version(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp)
   for name in ('fixed_abi_v552.json','nimble_abi_evidence.json'): (root/name).write_bytes((m.HERE/name).read_bytes())
   (root/'fixed_abi_v552.json').write_text('{}')
   with patch.object(m,'HERE',root):
    with self.assertRaises(ValueError): m.evidence('v5.5.2')
  result=subprocess.run(['python3',str(m.HERE/'occupancy_model.py'),'--sdk','v5.5.2','--profile','EMPTY','--schema-version','2'],capture_output=True,text=True)
  self.assertNotEqual(result.returncode,0);self.assertIn('unsupported schema',result.stderr)
 def test_duplicates_and_history(self):
  f,n=m.evidence('v5.5.2');i=m.inventory(f,n);r=m.record(i,'gw_wifi','slot_a')
  with self.assertRaises(ValueError):m.summarize([r,copy.deepcopy(r)])
  x=m.summarize([r],[('replace',('gw_wifi','slot_a'),6),('replace',('gw_wifi','slot_a'),6),('delete',('gw_wifi','slot_a'),None)])
  self.assertEqual(x['initial_live_entries'],7);self.assertEqual(x['settled_live_entries'],1);self.assertEqual(x['single_replacement_additional_entries'],6);self.assertEqual(x['obsolete_entries_before_reclamation_upper_model'],18)
  with self.assertRaises(ValueError):m.summarize([r],[('replace',('gw_wifi','slot_b'),6)])
 def test_capacity_overflow(self):
  self.assertEqual(m.add(m.MAX-1,1),m.MAX)
  with self.assertRaises(ValueError):m.add(m.MAX,1)
  with self.assertRaises(ValueError):m.summarize([],theoretical_entries=m.MAX+1)
  self.assertTrue(m.generate('v5.5.2','EXHAUSTION')['exceeds_theoretical_total'])
  self.assertFalse(m.generate('v5.5.2','HIGH_PRESSURE')['exceeds_theoretical_total'])
  with self.assertRaises(ValueError):m.generate('v5.5.2','EMPTY',seed=m.MAX+1)
 def test_geometry_and_feasibility(self):
  self.assertEqual(m.PARTITION_BYTES,0x6000);self.assertEqual(m.PAGE_BYTES,4096)
  self.assertEqual(m.PAGE_COUNT,6);self.assertEqual(m.ENTRIES_PER_PAGE,126)
  self.assertEqual(m.PHYSICAL_SLOTS,756);self.assertEqual(m.PLANNING_REFERENCE,630)
  for sdk in ('v5.5.2','v5.5.4'):
   for profile,count,classification in [('NORMAL_INSTALLATION',229,'WITHIN_ANALYTICAL_PLANNING_BOUNDS'),('COMBINED_OCCUPANCY',297,'WITHIN_ANALYTICAL_PLANNING_BOUNDS'),('HIGH_PRESSURE',682,'ABOVE_CONSERVATIVE_PLANNING_REFERENCE'),('EXHAUSTION',938,'ABOVE_PHYSICAL_SLOTS')]:
    result=m.generate(sdk,profile)
    self.assertEqual(result['settled_live_entries'],count)
    self.assertEqual(result['analytical_live_demand_classification'],classification)
    self.assertEqual(result['actual_nvs_api_admission'],'UNKNOWN')
    self.assertEqual(result['gc_headroom'],'UNKNOWN');self.assertEqual(result['reclaimed_entries'],'UNKNOWN')
  with self.assertRaises(ValueError):m.summarize([],theoretical_entries=630)
 def test_reference_boundaries_and_replacement(self):
  def synthetic(n):return {'namespace':'x','key':'y','entries':n}
  for live,classification in [(630,'WITHIN_ANALYTICAL_PLANNING_BOUNDS'),(631,'ABOVE_CONSERVATIVE_PLANNING_REFERENCE'),(756,'ABOVE_CONSERVATIVE_PLANNING_REFERENCE'),(757,'ABOVE_PHYSICAL_SLOTS')]:
   self.assertEqual(m.summarize([synthetic(live-1)])['analytical_live_demand_classification'],classification)
  result=m.generate('v5.5.2','REPLACEMENT_PRESSURE')
  self.assertEqual(result['settled_live_entries'],229)
  self.assertEqual(result['single_replacement_additional_entries'],122)
  self.assertEqual(result['obsolete_entries_before_reclamation_upper_model'],272)
  self.assertEqual(result['history_simultaneous_physical_occupancy'],'UNKNOWN')
  self.assertEqual(result['replacement_page_chunk_placement'],'UNKNOWN')
  self.assertEqual(result['actual_nvs_api_admission'],'UNKNOWN')
 def test_guard_untouched(self):
  self.assertIn('{NULL,0}',(m.HERE.parent/'main/fixture_allowlist.c').read_text())
  for p in ['occupancy_model.py']:
   content=(m.HERE/p).read_text();self.assertNotIn('import serial',content);self.assertNotIn('nvs_flash_init(',content)

if __name__=='__main__':unittest.main()
