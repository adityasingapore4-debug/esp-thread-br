#ifndef ADR091_OFFLINE_SERVICE_H
#define ADR091_OFFLINE_SERVICE_H
#ifndef ADR091_STANDALONE_HOST_TEST
#error "Offline host qualification only; no ingress or firmware registration"
#endif
#include "wifi_recovery.h"

/* Single-owner, synchronous host fixture. This is not an authorization API.
 * The synthetic caller and its buffers must never be connected to an ingress. */
typedef struct {
    wifi_storage_t storage;
    wifi_recovery_t recovery;
    wifi_tx_t tx;
    wifi_recovery_hooks_t hooks;
    bool scanning;
    bool scan_ready;
    uint64_t scan_started, scan_completed;
    wifi_response_t scan;
} offline_wifi_service_t;

bool offline_wifi_init(offline_wifi_service_t *, wifi_store_io_t, uint64_t,
                       wifi_recovery_policy_t, wifi_recovery_hooks_t);
bool offline_wifi_status(offline_wifi_service_t *, uint8_t *, size_t, size_t *);
bool offline_wifi_scan_begin(offline_wifi_service_t *);
bool offline_wifi_scan_complete(offline_wifi_service_t *, const wifi_scan_entry_t *, size_t, bool);
bool offline_wifi_scan_response(offline_wifi_service_t *, uint8_t *, size_t, size_t *);
wifi_start_result_t offline_wifi_candidate(offline_wifi_service_t *, const uint8_t *, size_t);
bool offline_wifi_event(offline_wifi_service_t *, wifi_event_t);
bool offline_wifi_transaction(offline_wifi_service_t *, uint8_t *, size_t, size_t *);
typedef struct { uint8_t id[16]; uint32_t generation; uint64_t ticket, epoch; } offline_rollback_token_t;
bool offline_wifi_rollback_token(offline_wifi_service_t *, offline_rollback_token_t *);
bool offline_wifi_rollback_complete(offline_wifi_service_t *, const offline_rollback_token_t *, bool);
/* Worker is deliberately explicit. No callbacks, threads or transport are created. */
bool offline_wifi_work(offline_wifi_service_t *);
void offline_wifi_poll(offline_wifi_service_t *);
void offline_wifi_restart_request(offline_wifi_service_t *);
bool offline_wifi_restart_finish(offline_wifi_service_t *);
#endif
