#!/usr/bin/env python3
"""Inspect pinned SDK NVS translation units; never link or execute target code."""
import hashlib,json,shlex,subprocess,tempfile
from pathlib import Path

CASES={
 'v5.5.2':('/tmp/adr091-nimble552-build','/home/aditya/esp/esp-idf','30aaf64524299d3bde422ca9a2848090d1bc5d0f','9551ac31af0348d7de6cbe7527de3e5ba205460d'),
 'v5.5.4':('/home/aditya/esp/projects/esp-otbr/esp-thread-br/examples/basic_thread_border_router/build/dev1b-dma','/home/aditya/esp/esp-idf-5.5.4','735507283d5b2f9fb363a1901172dbd9e847945d','5c1a43ca3d205c3d5183e57dedce335fb6a81155')}
def run(argv): return subprocess.check_output(argv,text=True).strip()
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def extract(version,case):
 build,sdk,commit,nimble=case; build=Path(build); sdk=Path(sdk)
 assert run(['git','-C',str(sdk),'rev-parse','HEAD'])==commit
 assert run(['git','-C',str(sdk/'components/bt/host/nimble/nimble'),'rev-parse','HEAD'])==nimble
 assert not run(['git','-C',str(sdk/'components/bt/host/nimble/nimble'),'status','--porcelain'])
 config=json.loads((build/'config/sdkconfig.json').read_text())
 required={'IDF_TARGET':'esp32s3','BT_NIMBLE_ENABLED':True,'BT_NIMBLE_NVS_PERSIST':True,'BT_NIMBLE_SECURITY_ENABLE':True,'BT_NIMBLE_MAX_BONDS':3,'BT_NIMBLE_MAX_CCCDS':8}
 assert all(config.get(k)==v for k,v in required.items()),'unsupported or missing config'
 commands=json.loads((build/'compile_commands.json').read_text()); found=[c for c in commands if c['file'].endswith('/ble_store_nvs.c')]; assert len(found)==1
 cmd=shlex.split(found[0]['command']); compiler=cmd[0]; assert run([compiler,'-dumpmachine'])=='xtensa-esp-elf'
 assert str(sdk)+'/components/' in found[0]['file']; assert '/config/sdkconfig.h' not in found[0]['command'] or str(build) in found[0]['command']
 source=Path(found[0]['file']); source_text=source.read_text(); assert 'nvs_set_blob(nimble_handle, key, value, required_size)' in source_text
 types=['ble_store_value_sec','ble_store_value_cccd','ble_store_value_csfc','ble_store_value_local_irk','ble_store_value_rpa_rec']
 fields={'ble_store_value_sec':['peer_addr','bond_count','key_size','ediv','rand_num','ltk','irk','csrk','sign_counter'], 'ble_store_value_cccd':['peer_addr','chr_val_handle','flags'],'ble_store_value_csfc':['peer_addr']}
 with tempfile.TemporaryDirectory(prefix='adr091-nimble-abi-',dir='/tmp') as tmp:
  probe=Path(tmp)/'probe.c'; obj=Path(tmp)/'probe.o'
  body='#include <stddef.h>\n#include "'+str(source)+'"\n'
  for t in types:
   body+='char abi_'+t+'_size[sizeof(struct '+t+')];\nchar abi_'+t+'_align[_Alignof(struct '+t+')];\n'
   for field in fields.get(t,[]): body+='char abi_'+t+'_offset_'+field+'[offsetof(struct '+t+','+field+')+1];\n'
  body+='\n#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)\nchar abi_config_host_privacy[2];\n#else\nchar abi_config_host_privacy[1];\n#endif\n#if MYNEWT_VAL(ENC_ADV_DATA)\nchar abi_config_enc_adv[2];\n#else\nchar abi_config_enc_adv[1];\n#endif\nchar abi_config_max_csfcs[MYNEWT_VAL(BLE_STORE_MAX_CSFCS)+1];\n'
  body+='\n#if MYNEWT_VAL(BLE_HOST_BASED_PRIVACY)\nchar abi_ble_hs_dev_records_size[sizeof(struct ble_hs_dev_records)];\nchar abi_ble_hs_dev_records_align[_Alignof(struct ble_hs_dev_records)];\n#endif\n'
  body+='\n#if MYNEWT_VAL(ENC_ADV_DATA)\nchar abi_ble_store_value_ead_size[sizeof(struct ble_store_value_ead)];\nchar abi_ble_store_value_ead_align[_Alignof(struct ble_store_value_ead)];\n#endif\n'
  probe.write_text(body); cmd[cmd.index('-o')+1]=str(obj); cmd[cmd.index('-c')+1]=str(probe)
  subprocess.run(cmd,cwd=found[0]['directory'],check=True,stdout=subprocess.DEVNULL)
  out=run([compiler.replace('gcc','nm'),'-S','--defined-only',str(obj)])
  sizes={f[3][4:]:int(f[1],16) for line in out.splitlines() if len(f:=line.split())==4 and f[3].startswith('abi_')}
  for t in types: assert t+'_size' in sizes and t+'_align' in sizes
  for key in sizes:
   if '_offset_' in key or key.startswith('config_'): sizes[key]-=1
 return {'sdk_version':version,'idf_commit':commit,'nimble_commit':nimble,'compiler':run([compiler,'--version']).splitlines()[0],'target':run([compiler,'-dumpmachine']),'sdkconfig_header_sha256':sha(build/'config/sdkconfig.h'),'sdkconfig_json_sha256':sha(build/'config/sdkconfig.json'),'compile_commands_sha256':sha(build/'compile_commands.json'),'source_sha256':sha(source),'configuration':{k:config.get(k) for k in required},'abi':sizes,'namespace':'nimble_bond','value_type':'NVS blob','method':'original SDK translation unit, compile only, nm symbols','qualification':'offline synthetic config' if version=='v5.5.2' else 'historical Gateway build configuration; deployed state unverified'}
print(json.dumps({v:extract(v,c) for v,c in CASES.items()},indent=2,sort_keys=True))
