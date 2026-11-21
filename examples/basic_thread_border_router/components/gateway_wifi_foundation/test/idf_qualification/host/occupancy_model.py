#!/usr/bin/env python3
"""Offline analytical fixture descriptions. No data bytes or NVS access."""
import argparse
import hashlib
import json
from pathlib import Path

HERE=Path(__file__).resolve().parent
PIN='8720847e1f62b2001a93a3ac436491671404bf77'
MAX=(1<<64)-1
PAGE_BYTES=4096
PARTITION_BYTES=0x6000
PAGE_COUNT=PARTITION_BYTES//PAGE_BYTES
ENTRIES_PER_PAGE=126
PHYSICAL_SLOTS=PAGE_COUNT*ENTRIES_PER_PAGE
PLANNING_REFERENCE=(PAGE_COUNT-1)*ENTRIES_PER_PAGE
PROFILES=('EMPTY','MINIMAL_INSTALLATION','NORMAL_INSTALLATION','PROVISIONING_JOURNAL_PRESENT','PROVISIONING_JOURNAL_ABSENT','MAX_REGISTRY','NIMBLE_CONFIGURED_CAPACITY','GATEWAY_WIFI_AB','REPLACEMENT_PRESSURE','COMBINED_OCCUPANCY','HIGH_PRESSURE','EXHAUSTION')

def add(a,b):
 if type(a)!=int or type(b)!=int or a<0 or b<0 or a>MAX-b: raise ValueError('integer overflow')
 return a+b

def entries(kind,length):
 if type(length)!=int or length<1 or length>MAX: raise ValueError('invalid length')
 if kind in ('u8','u32'):
  if length!={'u8':1,'u32':4}[kind]: raise ValueError('scalar size')
  return 1
 if kind=='str':
  if length>4000: raise ValueError('string size')
  return add(1,add(length,31)//32)
 if kind=='blob':
  if length>508000: raise ValueError('blob size')
  return add(2,add(length,31)//32)
 raise ValueError('unsupported value type')

def evidence(version,features=None):
 if version not in ('v5.5.2','v5.5.4'): raise ValueError('unsupported sdk')
 if features is not None and features!={'host_privacy':False,'encrypted_advertising':False,'bonds':3,'cccds':8}: raise ValueError('unsupported features')
 fixed_path=HERE/('fixed_abi_v552.json' if version=='v5.5.2' else 'fixed_abi_v554.json')
 nim_path=HERE/'nimble_abi_evidence.json'
 expected_hashes={'v5.5.2':'f60348bdceb51b01cead7e23c8903aae3a185efaf7b881cc4ac1087e564806ab','v5.5.4':'2cd53eefd247352968b3dfd1d968983840ed5923b8705478ada0c573a4568a0e'}
 if hashlib.sha256(fixed_path.read_bytes()).hexdigest()!=expected_hashes[version] or hashlib.sha256(nim_path.read_bytes()).hexdigest()!='23811a4108ac345cef47d00f617eaea13b67b0f4f9c4851c02d6ba9fd479ab27': raise ValueError('evidence hash mismatch')
 fixed=json.loads(fixed_path.read_text()); nim=json.loads(nim_path.read_text())[version]
 if fixed.get('source_revision')!=PIN or fixed.get('idf_version')!=version or fixed.get('target')!='xtensa-esp-elf' or not all(fixed.get(k) for k in ('compiler','sdkconfig_sha256','compile_commands_sha256','method')): raise ValueError('fixed ABI provenance')
 if nim.get('sdk_version')!=version or nim.get('target')!='xtensa-esp-elf' or nim.get('namespace')!='nimble_bond' or nim.get('value_type')!='NVS blob' or not all(nim.get(k) for k in ('compiler','sdkconfig_header_sha256','source_sha256','idf_commit','nimble_commit','method')): raise ValueError('NimBLE provenance')
 cfg=nim['configuration']; expected={'IDF_TARGET':'esp32s3','BT_NIMBLE_ENABLED':True,'BT_NIMBLE_NVS_PERSIST':True,'BT_NIMBLE_SECURITY_ENABLE':True,'BT_NIMBLE_MAX_BONDS':3,'BT_NIMBLE_MAX_CCCDS':8}
 if any(cfg.get(k)!=v for k,v in expected.items()): raise ValueError('configuration mismatch')
 a=nim['abi']; f=fixed['sizes_bytes']
 needed={'ble_store_value_sec_size':88,'ble_store_value_cccd_size':16,'ble_store_value_csfc_size':8,'ble_store_value_local_irk_size':23,'ble_store_value_rpa_rec_size':14,'config_host_privacy':0,'config_enc_adv':0,'config_max_csfcs':3}
 if any(a.get(k)!=v for k,v in needed.items()): raise ValueError('unsupported NimBLE evidence')
 for key in ('provisioning_state','provisioning_journal','security_root','registry_entries_blob','registry_membership_blob','registry_entry','registry_membership','registry_count','automation_table','metadata_record','ble_key','wifi_min','wifi_max'):
  if key not in f or type(f[key])!=int or f[key]<=0: raise ValueError('missing fixed evidence: '+key)
 if f['registry_count']!=32 or f['registry_entries_blob']!=f['registry_entry']*32 or f['registry_membership_blob']!=f['registry_membership']*32 or (f['wifi_min'],f['wifi_max'])!=(25,111): raise ValueError('inconsistent schema')
 return fixed,nim

def inventory(fixed,nim):
 f=fixed['sizes_bytes']; a=nim['abi']; core='firmware/components/'
 # Bounds are serialized byte counts. String lengths INCLUDE the NUL terminator.
 data=[('prov_v2','state','blob',f['provisioning_state'],f['provisioning_state'],'provisioning_v2/provisioning_v2.c','normal'),('prov_v2','journal','blob',f['provisioning_journal'],f['provisioning_journal'],'provisioning_v2/provisioning_v2.c','journal'),('sec_root','root','blob',f['security_root'],f['security_root'],'security_root/security_root.c','root'),('sec_root','gen','u32',4,4,'security_root/security_root.c','root'),('node_reg','entries','blob',f['registry_entries_blob'],f['registry_entries_blob'],'node_registry/node_registry.c','registry'),('node_reg','membership','blob',f['registry_membership_blob'],f['registry_membership_blob'],'node_registry/node_registry.c','membership'),('node_reg','mbr_ready','u8',1,1,'node_registry/node_registry.c','membership_ready'),('node_reg','replicated_v1','u8',1,1,'node_registry/node_registry.c','migration'),('auto','table','blob',f['automation_table'],f['automation_table'],'automation_storage/automation_storage.c','automation'),('auto','revision','u32',4,4,'automation_storage/automation_storage.c','automation_revision'),('meta','node_meta','blob',f['metadata_record'],f['metadata_record'],'device_metadata/device_metadata.c','metadata'),('ble','key','blob',f['ble_key'],f['ble_key'],'ble_bridge/src/ble_key_manager.c','ble_key'),('ble','key_ver','u32',4,4,'ble_bridge/src/ble_key_manager.c','ble_key'),('ble_pair','user_key','u32',4,4,'ble_pairing_policy/ble_pairing_policy.c','pairing'),('adr068','epoch_hi','u32',4,4,'registry_revision_security/registry_revision_security.c','epoch'),('adr068','epoch_lo','u32',4,4,'registry_revision_security/registry_revision_security.c','epoch'),('wifi_config','ssid','str',2,32,'gateway/components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c','legacy_wifi'),('wifi_config','password','str',1,64,'gateway/components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c','legacy_wifi'),('gw_wifi','slot_a','blob',25,111,'gateway/components/gateway_wifi_foundation/wifi_store.c','wifi_slot_a'),('gw_wifi','slot_b','blob',25,111,'gateway/components/gateway_wifi_foundation/wifi_store.c','wifi_slot_b')]
 result={}
 for ns,key,kind,lo,hi,src,condition in data:
  ref=src if src.startswith('gateway/') else core+src
  if (ns,key) in result: raise ValueError('inventory collision')
  result[(ns,key)]={'namespace':ns,'key':key,'type':kind,'min':lo,'max':hi,'source':ref,'source_revision':'ecc083dfa7a01590ba2d2190b7bf5369d0022a04' if src.startswith('gateway/') else PIN,'condition':condition,'evidence':'source and target ABI' if kind=='blob' else 'source API'}
 for prefix,typekey,count in [('peer_sec','ble_store_value_sec_size',3),('our_sec','ble_store_value_sec_size',3),('cccd_sec','ble_store_value_cccd_size',8),('csfc_sec','ble_store_value_csfc_size',3),('local_irk','ble_store_value_local_irk_size',3),('rpa_rec','ble_store_value_rpa_rec_size',3)]:
  for index in range(1,count+1):
   key=prefix+'_'+str(index); size=a[typekey]
   if ('nimble_bond',key) in result: raise ValueError('inventory collision')
   result[('nimble_bond',key)]={'namespace':'nimble_bond','key':key,'type':'blob','min':size,'max':size,'source':'esp-idf/components/bt/host/nimble/nimble/nimble/host/store/config/src/ble_store_nvs.c','source_revision':nim['nimble_commit'],'condition':'explicit '+prefix+' index '+str(index),'evidence':'SDK source and target ABI'}
 return result

def record(inv,ns,key,kind=None,length=None):
 if (ns,key) not in inv: raise ValueError('missing schema')
 spec=inv[(ns,key)]; size=spec['max'] if length is None else length
 if kind is not None and kind!=spec['type']: raise ValueError('wrong type')
 if type(size)!=int or not spec['min']<=size<=spec['max']: raise ValueError('invalid record size')
 return {**spec,'length':size,'quantity':1,'entries':entries(spec['type'],size),'synthetic':True}

def summarize(records,history=(),theoretical_entries=PHYSICAL_SLOTS):
 if type(theoretical_entries)!=int or theoretical_entries!=PHYSICAL_SLOTS: raise ValueError('unsupported capacity geometry')
 seen=set();live=0
 for r in records:
  pair=(r['namespace'],r['key'])
  if pair in seen: raise ValueError('duplicate physical record')
  seen.add(pair); live=add(live,r['entries'])
 namespaces=sorted({x[0] for x in seen}); live=add(live,len(namespaces))
 current={(r['namespace'],r['key']):r['entries'] for r in records}; obsolete=0; peak_extra=0; operations=[]
 for op,pair,newsize in history:
  if pair not in current: raise ValueError('history missing key')
  old=current[pair]
  if op=='replace':
   if type(newsize)!=int or newsize<1: raise ValueError('history size')
   peak_extra=max(peak_extra,newsize); obsolete=add(obsolete,old); current[pair]=newsize
  elif op=='delete':
   if newsize is not None: raise ValueError('delete size')
   obsolete=add(obsolete,old); del current[pair]
  else: raise ValueError('history operation')
  operations.append({'operation':op,'namespace':pair[0],'key':pair[1],'old_entries':old,'new_entries':newsize})
 settled=len(namespaces)
 for value in current.values(): settled=add(settled,value)
 classification=('ABOVE_PHYSICAL_SLOTS' if settled>PHYSICAL_SLOTS else
                 'ABOVE_CONSERVATIVE_PLANNING_REFERENCE' if settled>PLANNING_REFERENCE else
                 'WITHIN_ANALYTICAL_PLANNING_BOUNDS')
 return {'namespaces':namespaces,'records':records,'initial_live_entries':live,'settled_live_entries':settled,'single_replacement_additional_entries':peak_extra,'maximum_single_existing_record_replacement_entries':max((r['entries'] for r in records),default=0),'obsolete_entries_before_reclamation_upper_model':obsolete,'history':operations,'theoretical_partition_entries':theoretical_entries,'exceeds_theoretical_total':settled>theoretical_entries,'partition_bytes':PARTITION_BYTES,'page_bytes':PAGE_BYTES,'page_count':PAGE_COUNT,'entry_slots_per_page':ENTRIES_PER_PAGE,'physical_entry_slots':PHYSICAL_SLOTS,'conservative_planning_reference_entries':PLANNING_REFERENCE,'analytical_live_demand_classification':classification,'actual_nvs_api_admission':'UNKNOWN','reclaimed_entries':'UNKNOWN','history_simultaneous_physical_occupancy':'UNKNOWN','replacement_page_chunk_placement':'UNKNOWN','writable_capacity':'UNKNOWN','gc_headroom':'UNKNOWN','unknown_owners':'UNKNOWN; not all SDK/application owners inventoried','evidence_quality':'analytical model; no real nvs_get_stats; no compatibility claim'}

def generate(version,name,seed=910202,features=None):
 if name not in PROFILES: raise ValueError('unsupported profile')
 if type(seed)!=int or seed<0 or seed>MAX: raise ValueError('invalid seed')
 fixed,nim=evidence(version,features); inv=inventory(fixed,nim); selected=[]
 def include(ns,*keys):
  for key in keys: selected.append(record(inv,ns,key))
 def minimal(): include('prov_v2','state'); include('sec_root','root','gen')
 def registry(): include('node_reg','entries','membership','mbr_ready','replicated_v1')
 def nimble():
  for p,n in [('peer_sec',3),('our_sec',3),('cccd_sec',8)]:
   for i in range(1,n+1): include('nimble_bond',p+'_'+str(i))
 def normal():
  minimal(); registry(); include('auto','table','revision'); include('meta','node_meta'); include('ble','key','key_ver'); include('ble_pair','user_key'); include('adr068','epoch_hi','epoch_lo'); include('wifi_config','ssid','password')
 if name in ('MINIMAL_INSTALLATION','PROVISIONING_JOURNAL_PRESENT','PROVISIONING_JOURNAL_ABSENT'): minimal()
 if name=='PROVISIONING_JOURNAL_PRESENT': include('prov_v2','journal')
 if name in ('NORMAL_INSTALLATION','REPLACEMENT_PRESSURE','COMBINED_OCCUPANCY','HIGH_PRESSURE','EXHAUSTION'): normal()
 if name=='MAX_REGISTRY': registry()
 if name in ('NIMBLE_CONFIGURED_CAPACITY','COMBINED_OCCUPANCY','HIGH_PRESSURE','EXHAUSTION'): nimble()
 if name in ('GATEWAY_WIFI_AB','COMBINED_OCCUPANCY','HIGH_PRESSURE','EXHAUSTION'): include('gw_wifi','slot_a','slot_b')
 if name in ('HIGH_PRESSURE','EXHAUSTION'):
  # Model-only filler uses separate synthetic names, never write commands or data.
  for i in range(12 if name=='HIGH_PRESSURE' else 20):
   selected.append({'namespace':'test_filler','key':'fill_%02d'%i,'type':'blob','min':960,'max':960,'length':960,'quantity':1,'entries':entries('blob',960),'synthetic':True,'source':'MODEL_ONLY_NO_PRODUCT_SCHEMA','source_revision':None,'condition':'pressure only','evidence':'hypothetical'})
 history=[]
 if name=='REPLACEMENT_PRESSURE': history=[('replace',('auto','table'),entries('blob',fixed['sizes_bytes']['automation_table']))]*4+[('replace',('node_reg','entries'),entries('blob',fixed['sizes_bytes']['registry_entries_blob']))]*2
 result=summarize(selected,history);result.update({'schema_version':1,'profile':name,'sdk_version':version,'seed':seed,'fixture_kind':'SYNTHETIC_METADATA_ONLY_NOT_AN_INSTALLATION','nvs_io':'NONE','features':{'host_privacy':False,'encrypted_advertising':False,'bonds':3,'cccds':8},'fixed_evidence':{'source_revision':PIN,'idf_version':version,'target':fixed['target'],'compiler':fixed['compiler'],'config_sha256':fixed['sdkconfig_sha256'],'method':fixed['method']},'nimble_evidence':{'source_revision':nim['nimble_commit'],'idf_version':version,'target':nim['target'],'compiler':nim['compiler'],'config_sha256':nim['sdkconfig_header_sha256'],'method':nim['method']}})
 return result

def main():
 p=argparse.ArgumentParser();p.add_argument('--sdk',required=True,choices=('v5.5.2','v5.5.4'));p.add_argument('--profile',required=True,choices=PROFILES);p.add_argument('--schema-version',type=int,default=1);a=p.parse_args()
 if a.schema_version!=1: raise ValueError('unsupported schema')
 print(json.dumps(generate(a.sdk,a.profile),sort_keys=True,indent=2))
if __name__=='__main__': main()
