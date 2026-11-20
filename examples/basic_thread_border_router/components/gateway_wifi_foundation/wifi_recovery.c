#include "wifi_recovery.h"
#include <limits.h>
#include <string.h>

enum { MAIL_IDLE=0, MAIL_QUEUED=1, MAIL_RUNNING=2, MAIL_DONE=3, MAIL_MASK=3 };
static uint_fast32_t mail(const wifi_recovery_t *c)
{
    return atomic_load_explicit(&c->mailbox,memory_order_acquire);
}
static bool same_job(const wifi_storage_job_t *a, const wifi_storage_job_t *b)
{
    return a->epoch==b->epoch && a->stamp==b->stamp && a->ticket==b->ticket &&
        a->generation==b->generation && a->kind==b->kind && !memcmp(a->id,b->id,16);
}
static bool timed_out(wifi_recovery_t *c)
{
    uint64_t now=c->hooks.now_ms(c->hooks.ctx);
    bool expired=now<c->last_ms || now<c->started_ms ||
        now-c->started_ms>=c->policy.max_elapsed_ms;
    c->last_ms=now;
    return expired;
}
static bool fence_queued(wifi_recovery_t *c)
{
    uint_fast32_t value=mail(c);
    if((value&MAIL_MASK)==MAIL_QUEUED) {
        if(!atomic_compare_exchange_strong_explicit(&c->mailbox,&value,MAIL_IDLE,
                memory_order_acq_rel,memory_order_acquire)) return false;
        if(c->work.kind==WIFI_WORK_PROMOTE) c->backend.cancel_commit(c->backend.ctx);
        return true;
    }
    return value==MAIL_IDLE;
}
static void require_restart(wifi_recovery_t *c)
{
    c->restart_required=true; c->boot_ready=false;
    c->state=WIFI_REC_RESTART_REQUIRED;
    /* A running/DONE worker still owns backend memory. Retire only after
     * acquiring its completion or atomically invalidating a queued dispatch. */
    if(fence_queued(c)) {
        wifi_storage_retire_for_restart(c->storage);
        c->retired=true;
    }
}
static bool queue(wifi_recovery_t *c, wifi_work_kind_t kind)
{
    if(mail(c)!=MAIL_IDLE || c->serial==UINT32_MAX/4u) {
        require_restart(c); return false;
    }
    c->work=c->operation;
    c->work.epoch=c->epoch; c->work.stamp=(uint64_t)(++c->serial)*4u;
    c->work.kind=kind;
    atomic_store_explicit(&c->mailbox,(uint_fast32_t)c->work.stamp|MAIL_QUEUED,memory_order_release);
    return true;
}
static bool load(void *ctx, wifi_config_t *out, uint32_t *generation)
{
    wifi_recovery_t *c=ctx;
    if(out) wifi_zero(out,sizeof(*out));
    if(generation) *generation=0;
    if(!out || !generation || mail(c)!=MAIL_IDLE || c->restarting ||
       c->restart_required || (c->state!=WIFI_REC_IDLE &&
       c->state!=WIFI_REC_DURABLE_VERIFIED && c->state!=WIFI_REC_NOT_PROMOTED_FENCED)) return false;
    return c->backend.load(c->backend.ctx,out,generation);
}
static bool stage(void *ctx, const wifi_config_t *candidate, uint32_t generation,
                  const uint8_t id[16], uint64_t ticket)
{
    wifi_recovery_t *c=ctx;
    if(c->restart_required || c->restarting || c->boot_ready || mail(c)!=MAIL_IDLE ||
       (c->state!=WIFI_REC_IDLE && (!c->delivered ||
       (c->state!=WIFI_REC_DURABLE_VERIFIED && c->state!=WIFI_REC_NOT_PROMOTED_FENCED)))) return false;
    uint64_t now=c->hooks.now_ms(c->hooks.ctx);
    if(now<c->last_ms || c->serial==UINT32_MAX/4u) { require_restart(c); return false; }
    if(!c->backend.stage(c->backend.ctx,candidate,generation,id,ticket)) return false;
    memset(&c->operation,0,sizeof(c->operation));
    memcpy(c->operation.id,id,16); c->operation.generation=generation;
    c->operation.ticket=ticket; c->operation.epoch=c->epoch;
    c->started_ms=c->last_ms=now; c->attempts=0; c->retired=false;
    c->delivered=false; c->boot_ready=false;
    if(!queue(c,WIFI_WORK_PROMOTE)) return false;
    c->operation=c->work; c->state=WIFI_REC_QUEUED_NOT_EXECUTING;
    return true;
}
static wifi_cancel_result_t cancel(void *ctx)
{
    wifi_recovery_t *c=ctx;
    if(c->restart_required || c->restarting) return WIFI_CANCEL_UNKNOWN;
    if(c->state==WIFI_REC_DURABLE_VERIFIED) return WIFI_CANCEL_DURABLE;
    if(c->state==WIFI_REC_NOT_PROMOTED_FENCED || c->state==WIFI_REC_IDLE) return WIFI_CANCEL_ABORTED;
    if(c->state!=WIFI_REC_QUEUED_NOT_EXECUTING || !fence_queued(c)) return WIFI_CANCEL_UNKNOWN;
    c->state=WIFI_REC_NOT_PROMOTED_FENCED;
    return WIFI_CANCEL_ABORTED;
}
static bool restore(void *ctx, const wifi_config_t *prior, uint32_t generation)
{
    wifi_recovery_t *c=ctx;
    if(mail(c)!=MAIL_IDLE || c->restart_required || c->restarting ||
       (c->state!=WIFI_REC_NOT_PROMOTED_FENCED && c->state!=WIFI_REC_IDLE &&
        c->state!=WIFI_REC_DURABLE_VERIFIED)) return false;
    return c->backend.restore(c->backend.ctx,prior,generation);
}
static void clear(void *ctx)
{
    wifi_recovery_t *c=ctx;
    if(mail(c)==MAIL_IDLE && !c->restart_required && !c->restarting &&
       (c->state==WIFI_REC_IDLE || c->state==WIFI_REC_DURABLE_VERIFIED ||
        c->state==WIFI_REC_NOT_PROMOTED_FENCED)) c->backend.clear(c->backend.ctx);
}
static bool restart(void *ctx)
{
    wifi_recovery_t *c=ctx;
    if(c->boot_ready && mail(c)==MAIL_IDLE && !c->restart_required) {
        c->boot_ready=false; return true;
    }
    wifi_recovery_request_restart(c);
    return false;
}
bool wifi_recovery_init(wifi_recovery_t *c, wifi_storage_t *storage, uint64_t epoch,
                        wifi_recovery_policy_t policy, wifi_recovery_hooks_t hooks)
{
    if(!c || c->initialized || !storage || storage->coordinator_attached || !storage->ready ||
       storage->state!=WIFI_STORAGE_READY || !epoch || !hooks.now_ms ||
       !policy.max_attempts || !policy.max_elapsed_ms) return false;
    memset(c,0,sizeof(*c)); atomic_init(&c->mailbox,MAIL_IDLE);
    c->storage=storage; c->backend=wifi_storage_interface(storage);
    c->epoch=epoch; c->policy=policy; c->hooks=hooks; c->state=WIFI_REC_IDLE;
    c->last_ms=c->started_ms=hooks.now_ms(hooks.ctx); c->initialized=true;
    storage->coordinator_attached=true;
    return true;
}
wifi_store_t wifi_recovery_interface(wifi_recovery_t *c)
{
    wifi_store_t iface={c,load,stage,cancel,restore,clear,restart}; return iface;
}
wifi_start_result_t wifi_recovery_start_result(wifi_recovery_t *c, wifi_tx_t *tx,
                                              const wifi_request_t *request, bool authorized)
{
    if(!c || !tx || !request || !c->initialized || tx->store.ctx!=c) return WIFI_START_INVALID;
    if(!authorized) return WIFI_START_UNAUTHORIZED;
    if(c->restart_required || c->restarting || c->boot_ready || mail(c)!=MAIL_IDLE ||
       (c->state!=WIFI_REC_IDLE && (!c->delivered ||
        (c->state!=WIFI_REC_DURABLE_VERIFIED && c->state!=WIFI_REC_NOT_PROMOTED_FENCED)))) return WIFI_START_BUSY;
    return wifi_tx_start_result(tx,request,true,c->hooks.now_ms(c->hooks.ctx));
}
bool wifi_recovery_job(const wifi_recovery_t *c, wifi_storage_job_t *job)
{
    if(!c || !job || !c->initialized || (mail(c)&MAIL_MASK)!=MAIL_QUEUED) return false;
    *job=c->work; return true;
}
bool wifi_recovery_worker_run(wifi_recovery_t *c, const wifi_storage_job_t *job)
{
    if(!c || !job || !c->initialized || !job->stamp || job->stamp>UINT32_MAX ||
       (job->stamp&MAIL_MASK)) return false;
    uint_fast32_t expected=(uint_fast32_t)job->stamp|MAIL_QUEUED;
    if(!atomic_compare_exchange_strong_explicit(&c->mailbox,&expected,
            (uint_fast32_t)job->stamp|MAIL_RUNNING,memory_order_acq_rel,memory_order_acquire)) return false;
    /* Owner cannot alter work/backend while RUNNING. Validate full copied
     * identity after claiming; forged metadata never reaches the backend. */
    if(!same_job(job,&c->work)) {
        atomic_store_explicit(&c->mailbox,(uint_fast32_t)job->stamp|MAIL_QUEUED,memory_order_release);
        return false;
    }
    c->worker_boot=false;
    switch(job->kind) {
    case WIFI_WORK_PROMOTE:
        c->worker_outcome=wifi_storage_execute(c->storage,job->id,job->generation,job->ticket); break;
    case WIFI_WORK_RECOVER:
        c->worker_outcome=wifi_storage_recover(c->storage); break;
    case WIFI_WORK_RESTART_SCAN:
        c->worker_boot=c->backend.restart(c->backend.ctx);
        c->worker_outcome=WIFI_CANCEL_UNKNOWN; break;
    default: c->worker_outcome=WIFI_CANCEL_UNKNOWN; break;
    }
    c->worker_restart=c->hooks.restart_required && c->hooks.restart_required(c->hooks.ctx);
    atomic_store_explicit(&c->mailbox,(uint_fast32_t)job->stamp|MAIL_DONE,memory_order_release);
    return true;
}
void wifi_recovery_request_restart(wifi_recovery_t *c)
{
    if(!c || !c->initialized || c->restart_required || c->restarting) return;
    if(c->state==WIFI_REC_IDLE || c->state==WIFI_REC_DURABLE_VERIFIED ||
       c->state==WIFI_REC_NOT_PROMOTED_FENCED) {
        c->started_ms=c->last_ms=c->hooks.now_ms(c->hooks.ctx); c->attempts=0;
    }
    c->restarting=true; c->boot_ready=false; c->state=WIFI_REC_RECOVERY_REQUIRED;
    fence_queued(c); /* RUNNING/DONE is explicitly not a successful fence. */
}
void wifi_recovery_poll(wifi_recovery_t *c)
{
    if(!c || !c->initialized) return;
    uint_fast32_t value=mail(c);
    if((value&MAIL_MASK)==MAIL_DONE) {
        /* Acquire of DONE drains the sole callback mailbox. No caller-supplied
         * completion can manufacture a backend result. */
        atomic_store_explicit(&c->mailbox,MAIL_IDLE,memory_order_release);
        if(c->restart_required) { require_restart(c); return; }
        if(c->worker_restart) { require_restart(c); return; }
        if((c->work.kind!=WIFI_WORK_PROMOTE || c->restarting) && timed_out(c)) {
            require_restart(c); return;
        }
        if(c->work.kind==WIFI_WORK_RESTART_SCAN) {
            if(c->worker_boot) {
                if(c->epoch==UINT64_MAX) { require_restart(c); return; }
                c->epoch++; c->restarting=false; c->boot_ready=true;
                c->retired=true; c->state=WIFI_REC_IDLE; c->delivered=false;
                wifi_zero(&c->operation,sizeof(c->operation));
                return;
            }
            c->state=WIFI_REC_RECOVERY_REQUIRED;
        } else if(!c->restarting) {
            if(c->worker_outcome==WIFI_CANCEL_DURABLE) c->state=WIFI_REC_DURABLE_VERIFIED;
            else if(c->worker_outcome==WIFI_CANCEL_ABORTED) c->state=WIFI_REC_NOT_PROMOTED_FENCED;
            else c->state=WIFI_REC_OUTCOME_UNKNOWN;
            return;
        }
    }
    if(c->restart_required) { require_restart(c); return; }
    if(!c->restarting && (c->state==WIFI_REC_IDLE || c->state==WIFI_REC_DURABLE_VERIFIED ||
                         c->state==WIFI_REC_NOT_PROMOTED_FENCED)) return;
    if(timed_out(c)) { require_restart(c); return; }
    if(mail(c)!=MAIL_IDLE) return; /* Cannot interrupt synchronous flash work. */
    if(!c->restarting && c->state!=WIFI_REC_OUTCOME_UNKNOWN &&
       c->state!=WIFI_REC_RECOVERY_REQUIRED) return;
    c->state=WIFI_REC_RECOVERY_REQUIRED;
    if(c->attempts>=c->policy.max_attempts) { require_restart(c); return; }
    c->attempts++;
    queue(c,c->restarting?WIFI_WORK_RESTART_SCAN:WIFI_WORK_RECOVER);
}
wifi_recovery_status_t wifi_recovery_status(const wifi_recovery_t *c)
{
    wifi_recovery_status_t s={c->state,c->epoch,c->attempts,mail(c)==MAIL_IDLE,
                              c->retired,c->boot_ready};
    if(!c->restart_required && (mail(c)&MAIL_MASK)==MAIL_RUNNING) s.state=WIFI_REC_EXECUTING;
    return s;
}
bool wifi_recovery_completion(const wifi_recovery_t *c, wifi_storage_completion_t *out)
{
    if(!out || c->delivered || c->restart_required || c->restarting || mail(c)!=MAIL_IDLE ||
       (c->state!=WIFI_REC_DURABLE_VERIFIED && c->state!=WIFI_REC_NOT_PROMOTED_FENCED)) return false;
    out->operation=c->operation;
    out->outcome=c->state==WIFI_REC_DURABLE_VERIFIED?WIFI_CANCEL_DURABLE:WIFI_CANCEL_ABORTED;
    return true;
}
bool wifi_recovery_deliver(wifi_recovery_t *c, wifi_tx_t *tx, const wifi_storage_completion_t *result)
{
    wifi_storage_completion_t current;
    if(!tx || !result || tx->store.ctx!=c || !wifi_recovery_completion(c,&current) ||
       !same_job(&result->operation,&current.operation) || result->outcome!=current.outcome ||
       result->operation.epoch!=c->epoch || !result->operation.generation ||
       tx->state!=WIFI_TX_COMMITTING || tx->ticket!=result->operation.ticket ||
       tx->generation!=result->operation.generation-1u || memcmp(tx->id,result->operation.id,16)) return false;
    c->delivered=true;
    wifi_event_t event=result->outcome==WIFI_CANCEL_DURABLE?WIFI_EVENT_COMMIT_SUCCESS:WIFI_EVENT_COMMIT_FAILED;
    if(wifi_tx_complete(tx,event,result->operation.id,result->operation.generation-1u,
                        result->operation.ticket,c->hooks.now_ms(c->hooks.ctx))) return true;
    /* Failed authoritative reload or wrong FSM identity cannot free exclusion. */
    require_restart(c); return false;
}
bool wifi_recovery_retire_transaction(wifi_recovery_t *c, wifi_tx_t *tx)
{
    if(!tx || tx->store.ctx!=c || !c->restart_required || !c->retired || mail(c)!=MAIL_IDLE) return false;
    wifi_zero(&tx->candidate,sizeof(tx->candidate));
    tx->boundary_unresolved=true; tx->carrier_lost=true;
    return true;
}
