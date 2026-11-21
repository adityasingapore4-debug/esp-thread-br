#!/usr/bin/env python3
"""Compile pinned owner types using the independent v5.5.2 S3 SDK build."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('abi_original',HERE/'extract_target_abi.py')
original=importlib.util.module_from_spec(spec); spec.loader.exec_module(original)
base_path=Path('/tmp/adr091-m4-2b1-build')
base_config=base_path/'config/sdkconfig.h'
base_commands=base_path/'compile_commands.json'
original_commands=original.BUILD/'compile_commands.json'
def call(args): return subprocess.check_output(args,text=True).strip()
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
assert call(['git','-C',str(original.SRC),'rev-parse','HEAD'])==original.PIN
assert not call(['git','-C',str(original.SRC),'status','--porcelain','--','components'])
assert call(['git','-C','/home/aditya/esp/esp-idf','rev-parse','HEAD'])=='30aaf64524299d3bde422ca9a2848090d1bc5d0f'
assert digest(base_config)=='38435c6c35ef0756f880aa4d896c3218a1100eb811fb40b2c9c819681c400e95'
assert digest(base_commands)=='84a29f70e594cea5435aba2c105927f73d1a8e9d9549223386762399f0cdd760'
assert digest(original_commands)=='b7c455d6692d3ae19ac2930b5e355454057522a638dbba6dfe522ea49dfd90ab'
assert '#define CONFIG_IDF_TARGET_ESP32S3 1' in base_config.read_text()
commands=json.loads(base_commands.read_text()); template=next(c for c in commands if c['file'].endswith('/fixture_guard.c'))
base=shlex.split(template['command']); base=base[:base.index('-o')]
base=[x for x in base if x not in ('-pedantic','-std=c11')]
assert all('/esp-idf-5.5.4/' not in x and '/build/dev1b-dma/config' not in x for x in base)
compiler=base[0]; assert call([compiler,'-dumpmachine'])=='xtensa-esp-elf'
old=json.loads(original_commands.read_text()); results={}
with tempfile.TemporaryDirectory(prefix='adr091-552-abi-',dir='/tmp') as tmp:
 for name,expressions in original.TYPES.items():
  matches=[c for c in old if c['file'].endswith('/'+name)]; assert len(matches)==1
  item=matches[0]; source=Path(item['file'].replace(original.ORIGINAL,str(original.SRC))); assert source.is_file()
  includes=[]
  for flag in shlex.split(item['command']):
   if flag.startswith('-I') and '/build/dev1b-dma/' not in flag:
    flag=flag.replace('/home/aditya/esp/esp-idf-5.5.4/','/home/aditya/esp/esp-idf/').replace(original.ORIGINAL,str(original.SRC))
    if Path(flag[2:]).exists(): includes.append(flag)
  assert all('/esp-idf-5.5.4/' not in x and original.ORIGINAL not in x for x in includes)
  probe=Path(tmp)/(name+'.c'); obj=Path(tmp)/(name+'.o')
  probe.write_text('#include <stddef.h>\n#include "'+str(source)+'"\n'+''.join('char abi_'+k+'['+v+'];\n' for k,v in expressions.items()))
  subprocess.run(base+includes+['-c',str(probe),'-o',str(obj)],cwd=template['directory'],check=True,stdout=subprocess.DEVNULL)
  symbols=call([compiler.replace('gcc','nm'),'-S','--defined-only',str(obj)])
  found={f[3][4:]:int(f[1],16) for line in symbols.splitlines() if len(f:=line.split())==4 and f[3].startswith('abi_')}
  assert set(found)==set(expressions),name
  for key in found:
   if key.endswith('_offset'): found[key]-=1
  results.update(found)
 wifi=original.GATEWAY/'components/gateway_wifi_foundation/wifi_store.c'
 assert call(['git','-C',str(original.GATEWAY),'rev-parse','HEAD'])=='ecc083dfa7a01590ba2d2190b7bf5369d0022a04'
 assert not call(['git','-C',str(original.GATEWAY),'status','--porcelain','--',str(wifi),str(wifi.with_suffix('.h'))])
 assert 'size_t len=16u+c->ssid_len+c->secret_len' in wifi.read_text()
 probe=Path(tmp)/'wifi.c'; obj=Path(tmp)/'wifi.o'
 probe.write_text('#include <stddef.h>\n#include "'+str(wifi)+'"\nchar abi_wifi_max[WIFI_STORE_RECORD_MAX];\nchar abi_wifi_config[sizeof(wifi_config_t)];\nchar abi_wifi_align[_Alignof(wifi_config_t)];\nchar abi_wifi_min[16+1+8];\n')
 subprocess.run(base+['-c',str(probe),'-o',str(obj)],cwd=template['directory'],check=True,stdout=subprocess.DEVNULL)
 symbols=call([compiler.replace('gcc','nm'),'-S','--defined-only',str(obj)])
 found={f[3][4:]:int(f[1],16) for line in symbols.splitlines() if len(f:=line.split())==4 and f[3].startswith('abi_')}
 assert set(found)=={'wifi_max','wifi_config','wifi_align','wifi_min'}; results.update(found)
assert results['registry_entries_blob']==results['registry_entry']*results['registry_count']
assert results['registry_membership_blob']==results['registry_membership']*results['registry_count']
assert results['wifi_min']==25 and results['wifi_max']==111
print(json.dumps({'source_revision':original.PIN,'idf_version':'v5.5.2','idf_commit':'30aaf64524299d3bde422ca9a2848090d1bc5d0f','compiler':call([compiler,'--version']).splitlines()[0],'target':call([compiler,'-dumpmachine']),'sdkconfig_sha256':digest(base_config),'compile_commands_sha256':digest(base_commands),'owner_include_discovery_sha256':digest(original_commands),'method':'original translation units compiled with v5.5.2 build flags/config and SDK headers; owner include paths derived from v5.5.4 compile database; nm symbol sizes','sizes_bytes':results,'nimble':'NOT IN SCOPE'},indent=2,sort_keys=True))
