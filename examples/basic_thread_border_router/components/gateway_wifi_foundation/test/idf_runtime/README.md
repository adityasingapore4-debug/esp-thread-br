# ADR-091 synthetic ESP32-S3 runtime self-test

This is a **separate development-only ESP-IDF project**, not the Gateway application. It builds the existing codec, transaction, storage and recovery sources into a private synthetic owner. It has no BLE, CLI, networking, authorization, Wi-Fi driver or NVS service and has no externally callable event or candidate API. It does not modify the Gateway's `0x42` fail-closed guards. The only candidate payload is a fixed synthetic fixture used by the compiled-in self-test; the foundation's boolean admission argument is not a security mechanism and must not be reused as one.

On boot, `app_main` starts one task. The task initializes one owner and serializes its internal synthetic events in a fixed eight-entry FIFO. Queue-full rejection is tested locally; this fixture does not claim reliable delivery from real producers, and no external event producer exists. ESP-IDF `esp_timer_get_time()` supplies monotonic milliseconds. The test waits 5.1 seconds for each of two stability windows and explicitly steps the existing promotion and recovery worker from the same task. Its in-memory backend performs no actual flash I/O; simulated rollback verifies prior committed records and does not reconnect to an AP. On completion or failure it prints results, wipes owner RAM and deletes its task. Task creation failure is reported separately.

The test checks initial synthetic status, a scan response, FIFO capacity, validated commit, cancellation, connection loss and rollback, and an uncertain write followed by fenced restart and verified slot recovery. It is not an implementation of BLE admission, all five wire operations, asynchronous radio behavior or a production service. The unresolved same-generation authorization and ordered carrier-loss handoff remain prerequisites for integration. No hardware result is claimed until this project is actually run on a device.

## Resources and validation

On the compiled ESP32-S3 image, `xtensa-esp32s3-elf-nm -S` reports 1,088 bytes for the static synthetic owner including the eight-entry queue and two in-memory slots. One temporary task requests 6,144 bytes of stack plus FreeRTOS task bookkeeping. The device prints actual stack high-water bytes; this has not yet been observed on hardware. The project boot stack and ESP-IDF system memory are additional. Host ABI gives a different structure size and its fake high-water number is not a target measurement.

From the Gateway example directory:

```sh
bash components/gateway_wifi_foundation/test/idf_runtime/host/run.sh
SANITIZER=address bash components/gateway_wifi_foundation/test/idf_runtime/host/run.sh
SANITIZER=undefined bash components/gateway_wifi_foundation/test/idf_runtime/host/run.sh
bash components/gateway_wifi_foundation/test/standalone/run.sh
bash components/gateway_wifi_foundation/test/run_storage_tests.sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
idf.py -C components/gateway_wifi_foundation/test/idf_runtime \
  -B /tmp/adr091-owner-build -D SDKCONFIG=/tmp/adr091-owner-sdkconfig \
  -D IDF_TARGET=esp32s3 build
```

The isolated build passed: application `0x30f40` (200,512 bytes), smallest application partition `0x100000`, headroom `0xcf0c0` (848,064 bytes, 81%). This is the **self-test project's** default partition layout, not the Gateway application's partition headroom. Host tests passed normally, with ASan and with UBSan. The existing standalone and storage regressions passed. The host FreeRTOS/clock shims run the same owner logic deterministically, but do not model task scheduling or measure target resources.

## Optional hardware procedure — not executed

Use a dedicated ESP32-S3 development device whose existing contents may be overwritten. This isolated image flashes its own bootloader and default partition table; it is not the Gateway application and will replace the device's existing boot layout. After choosing the correct serial port, from the Gateway example directory run:

```sh
source /home/aditya/esp/esp-idf-5.5.4/export.sh
idf.py -C components/gateway_wifi_foundation/test/idf_runtime \
  -B /tmp/adr091-owner-build -D SDKCONFIG=/tmp/adr091-owner-sdkconfig \
  -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with the selected device port. Wait at least 12 seconds and observe `ADR091_RUNTIME_SELFTEST: PASS` and the reported `stack_high_water_bytes`; any `FAIL`, reset or missing final line is not a pass. Exit monitor with Ctrl-]. Flashing and the device self-test were not performed by this task.
