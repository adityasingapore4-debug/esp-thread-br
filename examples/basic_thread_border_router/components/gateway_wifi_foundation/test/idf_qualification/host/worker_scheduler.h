#ifndef ADR091_HOST_WORKER_SCHEDULER_H
#define ADR091_HOST_WORKER_SCHEDULER_H
#include "wifi_recovery.h"
#include <pthread.h>

/* Host-only scheduler. Owner exclusively uses submit/wait/stop; the worker
 * copies jobs and only invokes the existing coordinator worker entry point.
 * Before stopping with submitted work, the serialized owner must request the
 * coordinator restart/fence. Stop is not cancellation or a storage result.
 * An already dispatched call drains; pending copies are discarded after join.
 * Coordinator, backend and owner state must outlive scheduler_stop/join.
 * Only the owner calls these APIs; pause is a deterministic host-test gate. */
typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    wifi_recovery_t *coordinator;
    wifi_storage_job_t job;
    bool pending, active, completed, accepted, stopping, started, paused, parked;
} qual_worker_t;
bool qual_worker_start(qual_worker_t *, wifi_recovery_t *);
bool qual_worker_submit(qual_worker_t *, const wifi_storage_job_t *);
bool qual_worker_wait(qual_worker_t *, bool *accepted);
bool qual_worker_pause(qual_worker_t *, bool pause);
bool qual_worker_wait_parked(qual_worker_t *);
bool qual_worker_request_stop(qual_worker_t *);
bool qual_worker_stop(qual_worker_t *);
#endif
