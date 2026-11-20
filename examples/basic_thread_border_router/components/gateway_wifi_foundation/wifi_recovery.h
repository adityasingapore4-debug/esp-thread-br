#ifndef GATEWAY_WIFI_RECOVERY_H
#define GATEWAY_WIFI_RECOVERY_H
#include "wifi_store.h"
#include <stdatomic.h>

typedef enum {
    WIFI_REC_IDLE, WIFI_REC_QUEUED_NOT_EXECUTING, WIFI_REC_EXECUTING,
    WIFI_REC_DURABLE_VERIFIED, WIFI_REC_NOT_PROMOTED_FENCED,
    WIFI_REC_OUTCOME_UNKNOWN, WIFI_REC_RECOVERY_REQUIRED,
    WIFI_REC_RESTART_REQUIRED
} wifi_recovery_state_t;
typedef enum { WIFI_WORK_PROMOTE, WIFI_WORK_RECOVER, WIFI_WORK_RESTART_SCAN } wifi_work_kind_t;
/* Metadata only. Copy by value into scheduler queues; never enqueue pointers to
 * mutable job storage. stamp is a unique dispatch capability within this epoch. */
typedef struct {
    uint64_t epoch, stamp, ticket;
    uint32_t generation;
    uint8_t id[16];
    wifi_work_kind_t kind;
} wifi_storage_job_t;
typedef struct {
    uint32_t max_attempts;
    uint64_t max_elapsed_ms;
} wifi_recovery_policy_t;
typedef struct {
    void *ctx;
    uint64_t (*now_ms)(void *);
    /* Worker-only query, after synchronous calls have returned. May be NULL
     * for a host boundary that supports qualified reopen. */
    bool (*restart_required)(void *);
} wifi_recovery_hooks_t;
typedef struct {
    wifi_storage_job_t operation;
    wifi_cancel_result_t outcome;
} wifi_storage_completion_t;
typedef struct {
    wifi_recovery_state_t state;
    uint64_t epoch;
    uint32_t attempts;
    bool quiescent, candidate_retired, boot_ready;
} wifi_recovery_status_t;

/* Zero-initialize before first init. Never copy a live coordinator or use raw
 * backend APIs once attached. One owner serializes every API except worker_run.
 * worker_run may overlap owner requests; its mailbox uses acquire/release atomics.
 * The backend is touched by at most one side, only after an exclusive claim or
 * a successful queued fence/DONE acquire. No API creates threads or runs BLE. */
typedef struct {
    wifi_storage_t *storage;
    wifi_store_t backend;
    wifi_recovery_policy_t policy;
    wifi_recovery_hooks_t hooks;
    atomic_uint_fast32_t mailbox;
    wifi_storage_job_t work, operation;
    wifi_cancel_result_t worker_outcome;
    bool worker_restart, worker_boot;
    uint64_t epoch, started_ms, last_ms;
    uint32_t serial, attempts;
    wifi_recovery_state_t state;
    bool initialized, restarting, restart_required, retired, boot_ready, delivered;
} wifi_recovery_t;

/* Backend must already have completed boot initialization through its existing
 * owner. epoch must be fresh/nonzero across actual boots; no default policy or
 * production timeout is supplied. init refuses reuse of a live coordinator. */
bool wifi_recovery_init(wifi_recovery_t *, wifi_storage_t *, uint64_t,
                        wifi_recovery_policy_t, wifi_recovery_hooks_t);
wifi_store_t wifi_recovery_interface(wifi_recovery_t *);
/* Future owner's admission adapter. Authorization is supplied/consumed by the
 * existing authority; this only adds storage availability before the accepted
 * FSM start call, including when the FSM itself is currently idle. */
wifi_start_result_t wifi_recovery_start_result(wifi_recovery_t *, wifi_tx_t *, const wifi_request_t *, bool);
/* Owner-side scheduler seam. poll never performs NVS calls. Dispatch copied
 * jobs away from BLE, and poll again after completion. Repeated polls cannot
 * exceed the supplied attempt/deadline policy. */
bool wifi_recovery_job(const wifi_recovery_t *, wifi_storage_job_t *);
bool wifi_recovery_worker_run(wifi_recovery_t *, const wifi_storage_job_t *);
void wifi_recovery_poll(wifi_recovery_t *);
void wifi_recovery_request_restart(wifi_recovery_t *);
wifi_recovery_status_t wifi_recovery_status(const wifi_recovery_t *);
bool wifi_recovery_completion(const wifi_recovery_t *, wifi_storage_completion_t *);
/* Owner adapter: rejects prior-epoch/dispatch completions before invoking the
 * accepted FSM. Definitive callbacks still pass its authoritative load check. */
bool wifi_recovery_deliver(wifi_recovery_t *, wifi_tx_t *, const wifi_storage_completion_t *);
/* Safe only after RESTART_REQUIRED and a completed worker fence. Makes no wire
 * result; retains the FSM's pending exclusion, disables transaction projection
 * and retires the now-unneeded verification candidate. */
bool wifi_recovery_retire_transaction(wifi_recovery_t *, wifi_tx_t *);
#endif
