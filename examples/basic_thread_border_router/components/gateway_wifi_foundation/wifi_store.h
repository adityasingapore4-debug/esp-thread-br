#ifndef GATEWAY_WIFI_STORE_H
#define GATEWAY_WIFI_STORE_H
#include "wifi_transaction.h"

#define WIFI_STORE_RECORD_MAX 111u
#define WIFI_STORE_NAMESPACE "gw_wifi"
#define WIFI_STORE_PARTITION "nvs"

typedef enum { WIFI_IO_OK, WIFI_IO_MISSING, WIFI_IO_INVALID, WIFI_IO_ERROR } wifi_io_result_t;
/* No retained pointers, callbacks or background writes. reopen must quiesce the
 * boundary and discard old handles before opening a fresh read view. ERROR is
 * never evidence of nonpersistence. read must never exceed cap. */
typedef struct {
    void *ctx;
    wifi_io_result_t (*read)(void *, unsigned, uint8_t *, size_t, size_t *);
    bool (*write)(void *, unsigned, const uint8_t *, size_t);
    bool (*commit)(void *);
    bool (*reopen)(void *);
} wifi_store_io_t;
typedef enum { WIFI_STORAGE_READY, WIFI_STORAGE_QUEUED, WIFI_STORAGE_UNKNOWN,
               WIFI_STORAGE_DURABLE, WIFI_STORAGE_ABORTED } wifi_storage_state_t;
/* Secret-bearing owner-private memory: never expose as diagnostics. All calls
 * (including worker execution/recovery) require exclusive owner serialization.
 * Run blocking execution away from BLE; no thread/task is created here. */
typedef struct {
    wifi_store_io_t io;
    wifi_config_t candidate;
    uint8_t id[16];
    uint32_t target, generation;
    uint64_t ticket, last_ticket;
    int slot, target_slot;
    wifi_storage_state_t state;
    bool ready, coordinator_attached;
} wifi_storage_t;

bool wifi_record_encode(const wifi_config_t *, uint32_t, uint8_t *, size_t, size_t *);
bool wifi_record_decode(const uint8_t *, size_t, wifi_config_t *, uint32_t *);
bool wifi_storage_init(wifi_storage_t *, wifi_store_io_t);
wifi_store_t wifi_storage_interface(wifi_storage_t *);
/* Called only for a previously admitted stage. Stale work is side-effect free.
 * A DURABLE return is verified; UNKNOWN requires recovery, never rollback. */
wifi_cancel_result_t wifi_storage_execute(wifi_storage_t *, const uint8_t[16], uint32_t, uint64_t);
/* One reopen and one two-slot scan, no retry loop. A boundary that cannot reopen
 * leaves UNKNOWN and excludes new work. Vendor wall-clock bound is unqualified. */
wifi_cancel_result_t wifi_storage_recover(wifi_storage_t *);
/* Coordinator-only lifetime boundary. Requires queued work invalidated and all
 * running calls returned/completions drained. Retires RAM, never proves abort,
 * never clears an adapter latch and cannot permit another write. */
void wifi_storage_retire_for_restart(wifi_storage_t *);
#endif
