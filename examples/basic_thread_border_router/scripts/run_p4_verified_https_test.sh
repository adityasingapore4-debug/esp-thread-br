#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
p4_companion="${UN_COMPANION_ROOT:-/home/aditya/development/projects/universal_node_companion}"
p4_flutter="${UN_FLUTTER:-/home/aditya/development/flutter/bin/flutter}"
p4_test_dir="$(mktemp -d /tmp/p4-verified-https.XXXXXX)"
trap 'rm -rf "$p4_test_dir"' EXIT
export P4_TLS_FIXTURE="${P4_TLS_FIXTURE:-/tmp/p488-generated}"
# Copy package resolution, making relative roots absolute. Sources stay read-only.
python3 - "$p4_companion" "$p4_test_dir" <<'PY'
import json,sys
from pathlib import Path
from urllib.parse import urljoin
root,out=map(Path,sys.argv[1:]); config=root/'.dart_tool/package_config.json'
data=json.loads(config.read_text())
for p in data['packages']:p['rootUri']=urljoin(config.as_uri(),p['rootUri'])
data['packages'].append({'name':'p4_gateway_fixture','rootUri':out.as_uri(),'packageUri':'lib/','languageVersion':'3.9'})
(out/'lib').mkdir()
(out/'.dart_tool').mkdir();(out/'.dart_tool/package_config.json').write_text(json.dumps(data))
graph=json.loads((root/'.dart_tool/package_graph.json').read_text());graph['roots']=['p4_gateway_fixture']
graph['packages'].append({'name':'p4_gateway_fixture','version':'0.0.0','dependencies':['universal_node_companion','flutter','flutter_test','cryptography','async']})
(out/'.dart_tool/package_graph.json').write_text(json.dumps(graph))
(out/'pubspec.yaml').write_text('name: p4_gateway_fixture\nenvironment:\n  sdk: ">=3.9.0 <4.0.0"\ndependencies:\n  flutter:\n    sdk: flutter\ndev_dependencies:\n  flutter_test:\n    sdk: flutter\n')
src=Path('components/universal_node_gateway/test/p4_verified_https_test.dart.in').read_text()
(out/'test').mkdir();(out/'test/p4_verified_https_test.dart').write_text(src.replace('@@COMPANION_ROOT@@',str(root)))
PY
cd "$p4_test_dir"
"$p4_flutter" test --no-pub test/p4_verified_https_test.dart
