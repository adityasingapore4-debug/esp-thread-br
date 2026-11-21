"""Read-only source/build checks; never imports HIL or accesses hardware."""
from pathlib import Path
import json
import re
import struct
import subprocess
import sys

project = Path(__file__).resolve().parents[1]
main = project / "main"
for path in main.glob("*.c"):
    text = path.read_text()
    # There is intentionally no NVS call, partition mutator, reboot, radio,
    # production registration, or storage-worker entry in any application source.
    assert not re.search(
        r"\b(?:nvs_\w+|esp_restart|esp_partition_(?:write\w*|erase\w*)|"
        r"esp_flash_(?:write\w*|erase\w*)|esp_wifi_\w+|nimble_\w+|"
        r"universal_node_\w+|wifi_storage_\w+|wifi_recovery_\w+)\s*\(", text
    ), path
assert "{NULL,0}" in (main / "fixture_allowlist.c").read_text()
assert "WIFI_NVS_ISOLATED_SYNTHETIC_TEST" not in (main / "CMakeLists.txt").read_text()
assert "nvs_flash" not in (main / "CMakeLists.txt").read_text()
assert (project / "partitions.qual.csv").read_text().count("nvs, data, nvs, 0x9000, 0x6000,") == 1

repo = Path(subprocess.check_output(["git", "-C", str(project), "rev-parse", "--show-toplevel"], text=True).strip())
allowed = project.relative_to(repo).as_posix() + "/"
baseline = "ecc083dfa7a01590ba2d2190b7bf5369d0022a04"
review = "10367ab83b46ab98150252453f06fdc9b6f1da61"
subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", baseline, review], check=True)
subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor", review, "HEAD"], check=True)
committed = subprocess.check_output(["git", "-C", str(repo), "diff", "--name-only", baseline, "HEAD"], text=True).splitlines()
assert committed and all(path.startswith(allowed) for path in committed), committed
changed = subprocess.check_output(["git", "-C", str(repo), "diff", "--name-only", "HEAD"], text=True).splitlines()
new = subprocess.check_output(["git", "-C", str(repo), "ls-files", "--others", "--exclude-standard"], text=True).splitlines()
assert all(path.startswith(allowed) for path in changed + new), (changed, new)

if len(sys.argv) == 2:
    build = Path(sys.argv[1])
    description = json.loads((build / "project_description.json").read_text())
    assert description["project_path"] == str(project)
    assert description["target"] == "esp32s3"
    components = description["build_component_info"]
    assert not ({"nvs_flash", "esp_wifi", "bt", "openthread", "universal_node_gateway", "gateway_wifi_foundation"} & set(components))
    sources = components["main"]["sources"]
    assert set(sources) == {str(main / name) for name in (
        "qualification_main.c", "fixture_guard.c", "fixture_allowlist.c", "fixture_platform.c", "result_protocol.c")}
    commands = json.loads((build / "compile_commands.json").read_text())
    assert all("WIFI_NVS_ISOLATED_SYNTHETIC_TEST" not in command["command"] for command in commands)
    sdk = json.loads((build / "config/sdkconfig.json").read_text())
    assert sdk["PARTITION_TABLE_CUSTOM"] and sdk["PARTITION_TABLE_CUSTOM_FILENAME"] == "partitions.qual.csv"
    table = (build / "partition_table/partition-table.bin").read_bytes()
    partitions = {}
    for offset in range(0, len(table), 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from("<HBBII16sI", table, offset)
        if magic != 0x50AA:
            break
        partitions[label.split(b"\0", 1)[0].decode("ascii")] = (kind, subtype, address, size, flags)
    assert partitions == {"nvs": (1, 2, 0x9000, 0x6000, 0), "qual_app": (0, 0, 0x10000, 0x100000, 0)}, partitions
    assert not sdk.get("NVS_ENCRYPTION", False) and not sdk.get("SECURE_FLASH_ENC_ENABLED", False)
    print("ESP_IDF_BUILD_ISOLATION: PASS; no NVS, radio or production component selected")
    print("GENERATED_TEST_PARTITION_GEOMETRY: PASS; nvs 0x9000/0x6000, qual_app 0x10000/0x100000")
print(f"SOURCE_AND_SCOPE_ISOLATION: PASS; {len(committed)} committed / {len(changed)} modified / {len(new)} new files inside qualification project")
