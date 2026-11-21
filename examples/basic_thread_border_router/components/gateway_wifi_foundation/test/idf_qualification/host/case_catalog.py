#!/usr/bin/env python3
"""Offline descriptions and fail-closed selection; no test execution backend."""
import argparse
import hashlib
import json
from pathlib import Path
import occupancy_model as occupancy

VERSION = '1.0'
FIELD_SEMANTICS = {
 'occupancy_profile': "Analytical capacity-demand reference only. It is not an exact NVS fixture recipe, a record-presence assertion, or proof that an NVS write is admissible.",
 'nvs_state': "Required record-presence, validity, generation and other state conditions for future fixture construction. This case contract, not occupancy_profile, governs setup; no NVS image is generated here.",
}
HERE = Path(__file__).resolve().parent
OFFLINE = 'OFFLINE_HOST_EXECUTABLE'
NVS = 'FUTURE_ISOLATED_NVS_REQUIRED'
POWER = 'FUTURE_POWER_INTERRUPTION_REQUIRED'
BLOCKED = 'BLOCKED_BY_UNVERIFIED_PREREQUISITE'
FEATURES = {'host_privacy': False, 'encrypted_advertising': False, 'bonds': 3, 'cccds': 8}

# id, objective, profile, state, operations, invariant, observable, class, fault
ROWS = [
 ('EMPTY', 'Empty isolated partition', 'EMPTY', 'no Gateway records', 'initialize isolated fixture; scan both slots', 'absence cannot invent credentials', 'empty boot result', NVS, None),
 ('SINGLE_A', 'Single valid A', 'GATEWAY_WIFI_AB', 'valid A only', 'scan A and B', 'only valid record may be selected', 'A generation selected', NVS, None),
 ('SINGLE_B', 'Single valid B', 'GATEWAY_WIFI_AB', 'valid B only', 'scan A and B', 'only valid record may be selected', 'B generation selected', NVS, None),
 ('DUAL_ORDER', 'Dual records generation ordering', 'GATEWAY_WIFI_AB', 'two valid unequal generations', 'scan both slots', 'highest valid generation wins', 'higher generation selected', NVS, None),
 ('DUAL_EQUAL', 'Conflicting equal generations', 'GATEWAY_WIFI_AB', 'equal generations with different payloads', 'scan both slots', 'conflict cannot invent authority', 'scan fails; no authoritative record selected', NVS, None),
 ('BAD_CRC', 'CRC corruption', 'GATEWAY_WIFI_AB', 'one bad checksum and one valid record', 'scan both slots', 'corrupt candidate cannot become authority', 'valid record selected or fail closed', NVS, None),
 ('BAD_LENGTH', 'Malformed record length', 'GATEWAY_WIFI_AB', 'one malformed length', 'scan both slots', 'invalid length is rejected', 'invalid record rejected', NVS, None),
 ('BAD_SCHEMA', 'Invalid record schema', 'GATEWAY_WIFI_AB', 'one unsupported schema', 'scan both slots', 'unsupported schema cannot become authority', 'invalid record rejected', NVS, None),
 ('TORN', 'Torn record', 'GATEWAY_WIFI_AB', 'partially written record', 'restart and scan', 'partial candidate is never authoritative', 'prior valid record or fail closed', POWER, 'torn-record'),
 ('INT_BEFORE_WRITE', 'Interrupt before write', 'NORMAL_INSTALLATION', 'previous valid generation', 'interrupt before write; restart; scan', 'previous generation preserved', 'previous generation or fail closed', POWER, 'before-write'),
 ('INT_DURING_WRITE', 'Interrupt during write', 'NORMAL_INSTALLATION', 'previous valid generation', 'interrupt during write; restart; scan', 'partial record never accepted', 'verified generation or fail closed', POWER, 'during-write'),
 ('INT_AFTER_WRITE', 'Interrupt after write', 'NORMAL_INSTALLATION', 'previous valid generation', 'interrupt after write; restart; scan', 'no unverified success callback', 'verified generation or fail closed', POWER, 'after-write'),
 ('INT_BEFORE_COMMIT', 'Interrupt before commit', 'NORMAL_INSTALLATION', 'previous valid generation', 'interrupt before commit; restart; scan', 'never assume rollback from interruption', 'verified generation or fail closed', POWER, 'before-commit'),
 ('INT_AFTER_COMMIT', 'Interrupt after commit', 'NORMAL_INSTALLATION', 'previous valid generation', 'interrupt after commit; restart; scan', 'no durability without verified readback', 'verified generation or fail closed', POWER, 'after-commit'),
 ('UNKNOWN_WRITE', 'Write ambiguity', 'NORMAL_INSTALLATION', 'write returns error after attempted mutation', 'write; report error; recover', 'unknown excludes replacement', 'unknown until verified or restart required', NVS, 'write-error'),
 ('UNKNOWN_COMMIT', 'Commit ambiguity', 'NORMAL_INSTALLATION', 'commit result uncertain', 'write; commit; recover', 'unknown excludes replacement', 'unknown until verified or restart required', NVS, 'commit-error'),
 ('UNKNOWN_READBACK', 'Readback failure', 'NORMAL_INSTALLATION', 'write attempted; readback fails', 'write; commit; fail readback; recover', 'no false durable result', 'unknown until verified or restart required', NVS, 'readback-error'),
 ('RECOVERY_REQUIRED', 'Unresolved recovery', 'NORMAL_INSTALLATION', 'persistent backend errors', 'attempt bounded recovery', 'single-writer exclusion persists', 'restart required after policy exhaustion', NVS, 'reopen-error'),
 ('CAP_NORMAL', 'Normal profile capacity', 'NORMAL_INSTALLATION', 'synthetic profile prepared', 'attempt replacement and observe result', 'analytical count is not API admission', 'actual API outcome measured', NVS, None),
 ('CAP_COMBINED', 'Combined profile capacity', 'COMBINED_OCCUPANCY', 'synthetic profile prepared', 'attempt replacement and observe result', 'unrelated owners preserved', 'actual API outcome measured', NVS, None),
 ('CAP_REPLACE', 'Replacement pressure', 'REPLACEMENT_PRESSURE', 'synthetic allocation history', 'replace largest record; observe placement', 'history is not simultaneous occupancy', 'actual API and GC outcome measured', NVS, None),
 ('CAP_HIGH', 'High pressure capacity', 'HIGH_PRESSURE', 'analytical profile only', 'verify achievable setup before replacement', 'do not infer writable feasibility', 'setup and admission independently measured', BLOCKED, None),
 ('CAP_EXHAUST', 'Physical exhaustion boundary', 'EXHAUSTION', 'analytical demand above physical slots', 'verify feasibility without claiming fixture exists', 'impossible analytical demand is not a prepared state', 'physical feasibility rejected or separately demonstrated', BLOCKED, None),
 ('COEXIST', 'Shared NVS preservation', 'COMBINED_OCCUPANCY', 'synthetic unrelated owner records', 'replace Wi-Fi slot; compare synthetic owner records', 'unrelated owners remain intact', 'owner records unchanged', NVS, None),
 ('GC_REPEAT', 'Repeated replacement and obsolete records', 'REPLACEMENT_PRESSURE', 'synthetic allocation history', 'repeat slot replacements; observe page state', 'no unrelated record loss', 'GC and replacement results recorded', NVS, None),
 ('GC_TRANSITION', 'Page transition recovery', 'HIGH_PRESSURE', 'verified page transition setup', 'trigger transition; interrupt; restart', 'previous verified authority or fail closed', 'recovery and page state observed', POWER, 'gc-transition'),
 ('STALE_WORKER', 'Reject stale worker', 'EMPTY', 'mock queued promotion', 'submit stale copied identity to accepted worker', 'no unauthorized write', 'worker rejects stale job', OFFLINE, None),
 ('QUEUED_FENCE', 'Fence queued worker on restart', 'EMPTY', 'mock queued promotion held before claim', 'admit restart; release worker', 'revoked job cannot execute', 'zero mock writes', OFFLINE, None),
 ('RUNNING_DRAIN', 'Drain running worker', 'EMPTY', 'mock worker held after claim', 'request restart; release worker; poll', 'running worker is not cancelled', 'no premature quiescence', OFFLINE, None),
 ('UNRESOLVED', 'Preserve unknown outcome', 'EMPTY', 'mock ambiguous write', 'fail mock write; exhaust recovery', 'no false durable or aborted result', 'restart required; admission denied', OFFLINE, 'mock-write-error'),
 ('RESTART_SCAN', 'Restart scan after quiescence', 'EMPTY', 'mock restart with drained worker', 'poll and run restart scan', 'scan cannot precede fence', 'boot readiness after successful scan', OFFLINE, None),
 ('FIXTURE_MISMATCH', 'Reject mismatched fixture', 'EMPTY', 'mock identity mismatch', 'evaluate fixture guard', 'no storage admission', 'denied', OFFLINE, None),
 ('NO_ENROLLMENT', 'Reject missing enrollment', 'EMPTY', 'immutable empty allowlist', 'evaluate fixture guard', 'default admission denied', 'denied', OFFLINE, None),
 ('WRONG_PARTITION', 'Reject wrong partition', 'EMPTY', 'mock partition mismatch', 'evaluate fixture guard', 'no storage admission', 'denied', OFFLINE, None),
 ('WRONG_APP', 'Reject wrong application identity', 'EMPTY', 'mock app mismatch', 'evaluate fixture guard', 'no storage admission', 'denied', OFFLINE, None),
 ('NO_AUTH', 'Reject missing fixture preflight approval', 'EMPTY', 'mock trusted fixture approval absent', 'evaluate fixture guard without an approved fixture', 'fixture preflight never grants BLE Local or ADR-073 Admin authority', 'fixture preflight denied; Wi-Fi Admin authorization not tested', OFFLINE, None),
 ('PRODUCTION_DENY', 'Prohibit production execution', 'EMPTY', 'production or unknown target', 'evaluate selection and isolation', 'no production execution path', 'denied', OFFLINE, None),
 ('SYNTHETIC_SECRET', 'Synthetic credentials and retirement', 'EMPTY', 'mock synthetic credentials', 'run mock worker; inspect retirement and output', 'credential bytes not logged or retained', 'zeroized candidate; no credential output', OFFLINE, None),
 ('SDK_552', 'Qualify v5.5.2 separately', 'NORMAL_INSTALLATION', 'v5.5.2 fixture prerequisites verified', 'prepare isolated fixture and measure', 'evidence is version specific', 'v5.5.2 observations recorded', NVS, None),
 ('SDK_554', 'Qualify v5.5.4 separately', 'NORMAL_INSTALLATION', 'v5.5.4 compatibility unresolved', 'resolve compiler and fixture prerequisites first', 'no cross-version NVS image claim', 'blocked until separate qualification', BLOCKED, None),
]

def catalog():
 cases=[]
 for ident, objective, profile, state, sequence, invariant, observable, cls, fault in ROWS:
  sdk=['v5.5.2'] if ident=='SDK_552' else ['v5.5.4'] if ident=='SDK_554' else ['v5.5.2','v5.5.4']
  cases.append({'id':ident,'objective':objective,'sdk':sdk,'occupancy_profile':profile,
   'nvs_state':state,'sequence':sequence,'invariant':invariant,'observable':observable,
   'fault_point':fault,'classification':cls,'requires_real_nvs':cls in (NVS,POWER,BLOCKED),
   'requires_controlled_restart':cls==POWER,'requires_power_interruption':cls==POWER,
   'requires_hardware':cls!=OFFLINE,'executable_in_m4_2b_3b':cls==OFFLINE,
   'prohibition_reason':None if cls==OFFLINE else 'No fixture authorization, real NVS or power interruption in M4.2B.3B',
   'record_size_evidence':'fixed ABI wifi_min/wifi_max; occupancy_model.evidence'})
 return {'catalog_version':VERSION,'source_revision':occupancy.PIN,'field_semantics':FIELD_SEMANTICS,'cases':cases}

def required_evidence(sdk):
 fixed,nim=occupancy.evidence(sdk,FEATURES)
 return {'sdk':sdk,'target':'esp32s3','features':FEATURES.copy(),
  'fixed_abi_sha256':hashlib.sha256((HERE/('fixed_abi_v552.json' if sdk=='v5.5.2' else 'fixed_abi_v554.json')).read_bytes()).hexdigest(),
  'nimble_abi_sha256':hashlib.sha256((HERE/'nimble_abi_evidence.json').read_bytes()).hexdigest(),
  'config_sha256':fixed['sdkconfig_sha256'],'nimble_config_sha256':nim['sdkconfig_header_sha256']}

def select_case(version, ident, sdk, evidence, supplied_catalog=None):
 result={'CASE_DEFINED':False,'CASE_SELECTED':False,'CASE_EXECUTION_PERMITTED':False,'CASE_EXECUTED':False,'CASE_RESULT':'NOT_RUN'}
 try:
  original=catalog()
  if version!=VERSION: raise ValueError('unknown catalog version')
  if supplied_catalog is not None and supplied_catalog!=original: raise ValueError('catalog metadata changed')
  matches=[c for c in original['cases'] if c['id']==ident]
  if len(matches)!=1: raise ValueError('unknown case')
  case=matches[0]; result['CASE_DEFINED']=True; result['CASE_SELECTED']=True
  if sdk not in case['sdk']: raise ValueError('unsupported SDK')
  if evidence!=required_evidence(sdk): raise ValueError('missing or mismatched evidence')
  if case['classification']!=OFFLINE or case['requires_real_nvs'] or case['requires_hardware'] or case['requires_power_interruption'] or not case['executable_in_m4_2b_3b']:
   raise ValueError('execution prohibited by classification')
  result['CASE_EXECUTION_PERMITTED']=True
 except (ValueError, KeyError, OSError, TypeError) as exc:
  result['REJECTION_REASON']=str(exc)
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--catalog',action='store_true')
 parser.add_argument('--select'); parser.add_argument('--version'); parser.add_argument('--sdk'); parser.add_argument('--evidence',type=Path)
 args=parser.parse_args()
 if args.catalog and not any((args.select,args.version,args.sdk,args.evidence)):
  print(json.dumps(catalog(),indent=2,sort_keys=True)); return
 if not args.select: parser.error('specify --catalog or --select')
 try: evidence=json.loads(args.evidence.read_text()) if args.evidence else None
 except (OSError,ValueError): evidence=None
 result=select_case(args.version,args.select,args.sdk,evidence)
 print(json.dumps(result,indent=2,sort_keys=True))
 raise SystemExit(0 if result['CASE_EXECUTION_PERMITTED'] else 1)
if __name__=='__main__': main()
