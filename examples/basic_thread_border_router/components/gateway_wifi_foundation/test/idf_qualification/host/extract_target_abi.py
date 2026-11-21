#!/usr/bin/env python3
"""Compile original pinned translation units as non-executable ABI probes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

PIN = '8720847e1f62b2001a93a3ac436491671404bf77'
SRC = Path('/home/aditya/esp/projects/un-fw-adr090/firmware')
GATEWAY = Path(__file__).resolve().parents[5]
BUILD = GATEWAY / 'build/dev1b-dma'
ORIGINAL = '/home/aditya/esp/projects/universal-node-thread-fw/firmware'
GATEWAY_BASE = 'ecc083dfa7a01590ba2d2190b7bf5369d0022a04'
GATEWAY_REVIEW = '10367ab83b46ab98150252453f06fdc9b6f1da61'
def verify_gateway_source():
 root=GATEWAY/'components/gateway_wifi_foundation'
 protected=[str(root/name) for name in ('wifi_store.c','wifi_store.h','wifi_store_nvs.c','wifi_store_nvs.h','wifi_codec.c','wifi_codec.h','wifi_transaction.c','wifi_transaction.h','wifi_recovery.c','wifi_recovery.h')]
 assert run(['git','-C',str(GATEWAY),'rev-parse',GATEWAY_BASE])==GATEWAY_BASE
 subprocess.run(['git','-C',str(GATEWAY),'merge-base','--is-ancestor',GATEWAY_BASE,GATEWAY_REVIEW],check=True)
 subprocess.run(['git','-C',str(GATEWAY),'merge-base','--is-ancestor',GATEWAY_REVIEW,'HEAD'],check=True)
 assert not run(['git','-C',str(GATEWAY),'diff','--name-only',GATEWAY_BASE,'HEAD','--',*protected]), 'committed production source differs'
 assert not run(['git','-C',str(GATEWAY),'status','--porcelain','--',*protected]), 'working production source differs'
TYPES = {
 'provisioning_v2.c': {'provisioning_state':'sizeof(provisioning_v2_state_record_t)', 'provisioning_state_align':'_Alignof(provisioning_v2_state_record_t)', 'provisioning_journal':'sizeof(provisioning_v2_journal_t)', 'provisioning_journal_align':'_Alignof(provisioning_v2_journal_t)', 'provisioning_credential':'sizeof(provisioning_v2_credential_record_t)', 'provisioning_credential_align':'_Alignof(provisioning_v2_credential_record_t)', 'state_credentials_offset':'offsetof(provisioning_v2_state_record_t,credentials)+1'},
 'node_registry.c': {'registry_entry':'sizeof(node_registry_persistent_state_t)', 'registry_entry_align':'_Alignof(node_registry_persistent_state_t)', 'registry_entries_blob':'sizeof(s_registry_persistent_io)', 'registry_membership':'sizeof(node_registry_membership_record_t)', 'registry_membership_align':'_Alignof(node_registry_membership_record_t)', 'registry_membership_blob':'sizeof(s_membership)', 'registry_count':'NODE_REGISTRY_MAX_NODES'},
 'device_metadata.c': {'metadata_record':'sizeof(metadata_persisted_record_t)', 'metadata_align':'_Alignof(metadata_persisted_record_t)', 'metadata_payload_offset':'offsetof(metadata_persisted_record_t,metadata)+1'},
 'automation_storage.c': {'automation_table':'sizeof(automation_table_t)', 'automation_align':'_Alignof(automation_table_t)', 'automation_rules_offset':'offsetof(automation_table_t,rules)+1'},
 'security_root.c': {'security_root':'sizeof(s_root)', 'security_root_align':'_Alignof(uint8_t)'},
 'ble_key_manager.c': {'ble_key':'sizeof(s_key)', 'ble_key_align':'_Alignof(uint8_t)'},
}
def run(args):
 return subprocess.check_output(args, text=True).strip()
def main():
 parser=argparse.ArgumentParser(); parser.add_argument('--commands',type=Path,default=BUILD/'compile_commands.json'); parser.add_argument('--config',type=Path,default=BUILD/'config/sdkconfig.h'); parser.add_argument('--type-check'); args=parser.parse_args()
 assert run(['git','-C',str(SRC),'rev-parse','HEAD'])==PIN, 'source revision mismatch'
 assert not run(['git','-C',str(SRC),'status','--porcelain','--','components']), 'source tree modified'
 assert args.config.is_file() and args.commands.is_file(), 'required configuration missing'
 config=args.config.read_text(); assert '#define CONFIG_IDF_TARGET_ESP32S3 1' in config, 'wrong target'
 assert '#define CONFIG_UN_CLI 1' in config, 'required configuration missing'
 desc=json.loads((BUILD/'project_description.json').read_text()); assert desc['idf_path']=='/home/aditya/esp/esp-idf-5.5.4' and desc['target']=='esp32s3', 'SDK mismatch'
 assert args.config.resolve()==(BUILD/'config/sdkconfig.h').resolve(), 'configuration does not match compile commands'
 assert hashlib.sha256(args.config.read_bytes()).hexdigest()=='eea7f9bd546c82f0869cfdf6f576c43516f6c6e41844fa1e83386f663fd4e0c3', 'unsupported sdkconfig'
 commands=json.loads(args.commands.read_text()); assert args.commands.resolve()==(BUILD/'compile_commands.json').resolve(), 'unsupported compile commands'
 assert hashlib.sha256(args.commands.read_bytes()).hexdigest()=='b7c455d6692d3ae19ac2930b5e355454057522a638dbba6dfe522ea49dfd90ab', 'unsupported compiler configuration'
 assert run(['git','-C',desc['idf_path'],'describe','--tags','--always'])=='v5.5.4', 'unsupported SDK revision'
 if args.type_check: assert any(args.type_check in v for v in TYPES.values()), 'missing required type'
 results={}
 with tempfile.TemporaryDirectory(prefix='adr091-abi-',dir='/tmp') as tmp:
  for name,expressions in TYPES.items():
   hits=[c for c in commands if c['file'].endswith('/'+name)]
   assert len(hits)==1, 'missing or ambiguous translation unit: '+name
   item=hits[0]; source=Path(item['file'].replace(ORIGINAL,str(SRC)))
   assert source.is_file() and str(source).startswith(str(SRC)+'/components/'), 'missing pinned source'
   text=source.read_text()
   checks={'provisioning_v2.c':['write_blob(PROVISIONING_V2_NVS_STATE_KEY, &state, sizeof(state))','write_blob(PROVISIONING_V2_NVS_JOURNAL_KEY, &journal, sizeof(journal))'], 'node_registry.c':['records, sizeof(s_membership)', 's_registry_persistent_io,\n            sizeof(s_registry_persistent_io)'], 'device_metadata.c':['&record, sizeof(record)'], 'automation_storage.c':['sizeof(*table)'], 'security_root.c':['root, sizeof(s_root)'], 'ble_key_manager.c':['key, BLE_KEY_SIZE']}
   assert all(s in text for s in checks[name]), 'persisted call changed: '+name
   probe=Path(tmp)/(name+'.probe.c'); obj=Path(tmp)/(name+'.o')
   probe.write_text('#include <stddef.h>\n#include "'+str(source)+'"\n'+''.join('char abi_'+k+'['+v+'];\n' for k,v in expressions.items()))
   cmd=shlex.split(item['command']); compiler=cmd[0]
   assert run([compiler,'-dumpmachine'])=='xtensa-esp-elf', 'wrong compiler target'
   assert '/esp-idf-5.5.4/' in item['command'], 'SDK headers mismatch'
   cmd=[x.replace(ORIGINAL,str(SRC)) for x in cmd]
   cmd[cmd.index('-o')+1]=str(obj); cmd[cmd.index('-c')+1]=str(probe)
   subprocess.run(cmd,cwd=item['directory'],check=True,stdout=subprocess.DEVNULL)
   nm=compiler.replace('gcc','nm'); symbols=run([nm,'-S','--defined-only',str(obj)])
   found={}
   for line in symbols.splitlines():
    fields=line.split()
    if len(fields)==4 and fields[3].startswith('abi_'): found[fields[3][4:]]=int(fields[1],16)
   assert set(found)==set(expressions), ('missing types',name,found)
   for k in found:
    if k.endswith('_offset'): found[k]-=1
   results.update(found)
 assert results['registry_entries_blob']==results['registry_entry']*results['registry_count']
 assert results['registry_membership_blob']==results['registry_membership']*results['registry_count']
 wifi_source=GATEWAY/'components/gateway_wifi_foundation/wifi_store.c'
 wifi_adapter=GATEWAY/'components/gateway_wifi_foundation/wifi_store_nvs.c'
 verify_gateway_source()
 assert not run(['git','-C',str(GATEWAY),'status','--porcelain','--',str(wifi_source),str(wifi_adapter),str(wifi_source.with_suffix('.h'))]), 'gateway storage modified'
 assert 'size_t len=16u+c->ssid_len+c->secret_len' in wifi_source.read_text()
 assert 'nvs_set_blob(a->handle,keys[slot],p,n)' in wifi_adapter.read_text()
 with tempfile.TemporaryDirectory(prefix='adr091-wifi-abi-',dir='/tmp') as tmp:
  item=next(c for c in commands if c['file'].endswith('/automation_storage.c'))
  compiler=shlex.split(item['command'])[0]
  probe=Path(tmp)/'wifi.c'; obj=Path(tmp)/'wifi.o'
  probe.write_text('#include <stddef.h>\n#include "'+str(wifi_source)+'"\nchar abi_wifi_max[WIFI_STORE_RECORD_MAX];\nchar abi_wifi_config[sizeof(wifi_config_t)];\nchar abi_wifi_align[_Alignof(wifi_config_t)];\nchar abi_wifi_min[16+1+8];\n')
  cmd=[x.replace(ORIGINAL,str(SRC)) for x in shlex.split(item['command'])]
  cmd[cmd.index('-o')+1]=str(obj); cmd[cmd.index('-c')+1]=str(probe)
  subprocess.run(cmd,cwd=item['directory'],check=True,stdout=subprocess.DEVNULL)
  symbols=run([compiler.replace('gcc','nm'),'-S','--defined-only',str(obj)])
  found={f[3][4:]:int(f[1],16) for line in symbols.splitlines() if len(f:=line.split())==4 and f[3].startswith('abi_')}
  assert set(found)=={'wifi_max','wifi_min','wifi_config','wifi_align'}
  assert found['wifi_max']==111 and found['wifi_min']==25
  results.update(found)
 output={'source_revision':PIN,'idf_version':run(['git','-C',desc['idf_path'],'describe','--tags','--always']),'compiler':run([compiler,'--version']).splitlines()[0], 'target':run([compiler,'-dumpmachine']),'sdkconfig_sha256':hashlib.sha256(args.config.read_bytes()).hexdigest(), 'compile_commands_sha256':hashlib.sha256(args.commands.read_bytes()).hexdigest(), 'method':'original translation units included in compile-only sizeof/_Alignof probes; nm symbol sizes; offsets encoded as +1', 'sizes_bytes':results, 'nimble':'NOT IN SCOPE'}
 print(json.dumps(output,indent=2,sort_keys=True))
if __name__=='__main__': main()
