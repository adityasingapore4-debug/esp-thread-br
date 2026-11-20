#include "wifi_recovery.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
typedef enum { GOOD, FAIL_BEFORE, FAIL_AFTER, TORN, READBACK_FAIL, COMMIT_FAIL, REMOVE_FAIL } fault_t;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool entered, release;
} gate_t;
typedef struct {
    uint8_t disk[2][WIFI_STORE_RECORD_MAX];
    size_t len[2];
    uint64_t now;
    unsigned writes, reopens, reads, commits;
    unsigned reopen_failures;
    fault_t fault;
    bool written, uncertain;
    gate_t *gate;
} mock_t;
static bool zero(const void *p, size_t n)
{
    const uint8_t *b=p; while(n--) if(*b++) return false; return true;
}
static wifi_config_t config(unsigned v)
{
    wifi_config_t c={0}; c.mode=1; c.ssid_len=4; c.secret_len=8;
    memcpy(c.ssid,"TEST",4); c.ssid[3]=(uint8_t)('A'+v%26);
    memset(c.secret,(int)('a'+v%26),8); return c;
}
static wifi_io_result_t read_slot(void *ctx, unsigned slot, uint8_t *p, size_t cap, size_t *n)
{
    mock_t *m=ctx; assert(slot<2); m->reads++; *n=0;
    if(m->uncertain || (m->written && m->fault==READBACK_FAIL)) return WIFI_IO_ERROR;
    if(!m->len[slot]) return WIFI_IO_MISSING;
    if(m->len[slot]>cap) return WIFI_IO_INVALID;
    memcpy(p,m->disk[slot],m->len[slot]); *n=m->len[slot]; return WIFI_IO_OK;
}
static bool write_slot(void *ctx, unsigned slot, const uint8_t *p, size_t n)
{
    mock_t *m=ctx; assert(slot<2 && n<=WIFI_STORE_RECORD_MAX);
    if(m->gate) {
        gate_t *g=m->gate;
        assert(!pthread_mutex_lock(&g->mutex));
        g->entered=true; assert(!pthread_cond_signal(&g->cond));
        while(!g->release) assert(!pthread_cond_wait(&g->cond,&g->mutex));
        assert(!pthread_mutex_unlock(&g->mutex));
    }
    m->writes++; m->written=true;
    if(m->fault==REMOVE_FAIL) { m->uncertain=true; return false; }
    if(m->fault==FAIL_BEFORE) return false;
    memcpy(m->disk[slot],p,n); m->len[slot]=n;
    if(m->fault==TORN) { m->disk[slot][n-1]^=1; return false; }
    return m->fault!=FAIL_AFTER;
}
static bool commit(void *ctx)
{
    mock_t *m=ctx; m->commits++; return m->fault!=COMMIT_FAIL;
}
static bool reopen(void *ctx)
{
    mock_t *m=ctx; m->reopens++;
    if(m->uncertain) return false;
    if(m->reopen_failures) { m->reopen_failures--; return false; }
    m->written=false; return true;
}
static uint64_t now(void *ctx) { return ((mock_t *)ctx)->now; }
static bool restart_required(void *ctx) { return ((mock_t *)ctx)->uncertain; }
static wifi_store_io_t boundary(mock_t *m)
{
    wifi_store_io_t io={m,read_slot,write_slot,commit,reopen}; return io;
}
static wifi_recovery_hooks_t hooks(mock_t *m)
{
    wifi_recovery_hooks_t h={m,now,restart_required}; return h;
}
static void seed(mock_t *m, unsigned slot, uint32_t gen, unsigned value)
{
    wifi_config_t c=config(value);
    CHECK(wifi_record_encode(&c,gen,m->disk[slot],sizeof(m->disk[slot]),&m->len[slot]));
}
static void setup_epoch(mock_t *m, wifi_storage_t *s, wifi_recovery_t *c, uint64_t epoch)
{
    CHECK(wifi_storage_init(s,boundary(m)));
    CHECK(wifi_recovery_init(c,s,epoch,(wifi_recovery_policy_t){3,100},hooks(m)));
}
static void setup(mock_t *m, wifi_storage_t *s, wifi_recovery_t *c) { setup_epoch(m,s,c,11); }
static wifi_storage_job_t admit(wifi_recovery_t *c, uint32_t generation, uint64_t ticket)
{
    wifi_config_t candidate=config(9); uint8_t id[16]={1};
    wifi_store_t store=wifi_recovery_interface(c); wifi_storage_job_t job;
    CHECK(store.stage(store.ctx,&candidate,generation,id,ticket));
    wifi_zero(&candidate,sizeof(candidate));
    CHECK(wifi_recovery_job(c,&job)); return job;
}
static wifi_storage_job_t run_next(wifi_recovery_t *c)
{
    wifi_storage_job_t job;
    CHECK(wifi_recovery_job(c,&job));
    CHECK(wifi_recovery_worker_run(c,&job)); wifi_recovery_poll(c); return job;
}
static bool rejects_new(wifi_recovery_t *c)
{
    wifi_config_t candidate=config(10); uint8_t id[16]={2};
    wifi_store_t st=wifi_recovery_interface(c);
    return !st.stage(st.ctx,&candidate,c->operation.generation, id,c->operation.ticket+1);
}
static wifi_storage_job_t begin_existing_tx(mock_t *m, wifi_recovery_t *c, wifi_tx_t *tx)
{
    wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config=config(9);
    CHECK(wifi_recovery_start_result(c,tx,&r,true)==WIFI_START_ACCEPTED);
    CHECK(wifi_tx_event(tx,WIFI_EVENT_ASSOCIATED,++m->now));
    CHECK(wifi_tx_event(tx,WIFI_EVENT_IP_READY,++m->now)); m->now+=5000;
    CHECK(wifi_tx_event(tx,WIFI_EVENT_STABILITY_COMPLETE,m->now));
    CHECK(wifi_tx_event(tx,WIFI_EVENT_PROMOTION_ADMIT,++m->now));
    wifi_zero(&r,sizeof(r));
    wifi_storage_job_t job; CHECK(wifi_recovery_job(c,&job)); return job;
}
static wifi_storage_job_t begin_tx(mock_t *m, wifi_recovery_t *c, wifi_tx_t *tx)
{
    CHECK(wifi_tx_init(tx,wifi_recovery_interface(c)));
    return begin_existing_tx(m,c,tx);
}
static void queued_cancel(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&m,&s,&c); wifi_storage_job_t old=begin_tx(&m,&c,&tx);
    CHECK(wifi_recovery_status(&c).state==WIFI_REC_QUEUED_NOT_EXECUTING);
    wifi_store_t st=wifi_recovery_interface(&c);
    CHECK(st.cancel_commit(st.ctx)==WIFI_CANCEL_ABORTED);
    CHECK(!wifi_recovery_worker_run(&c,&old)); CHECK(!m.writes);
    CHECK(zero(&s.candidate,sizeof(s.candidate)));
    wifi_storage_completion_t result; CHECK(wifi_recovery_completion(&c,&result));
    CHECK(result.outcome==WIFI_CANCEL_ABORTED);
    CHECK(wifi_recovery_deliver(&c,&tx,&result));
    CHECK(tx.state==WIFI_TX_ROLLBACK && zero(&tx.candidate,sizeof(tx.candidate)));
    CHECK(!wifi_recovery_deliver(&c,&tx,&result));
    puts("queued cancellation fences delayed dispatch and clears both candidates: PASS");
}
static void recovery_matrix(void)
{
    const fault_t faults[]={FAIL_BEFORE,FAIL_AFTER,TORN,READBACK_FAIL,COMMIT_FAIL};
    for(unsigned gen=1;gen<=8;gen++) for(size_t f=0;f<sizeof(faults)/sizeof(faults[0]);f++) {
        mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0};
        unsigned slot=(gen-1)%2;
        seed(&m,slot,gen,gen); if(gen>1) seed(&m,1u-slot,gen-1,gen-1);
        uint8_t previous[WIFI_STORE_RECORD_MAX]; memcpy(previous,m.disk[slot],sizeof(previous));
        setup(&m,&s,&c); wifi_storage_job_t old=admit(&c,gen+1,1); m.fault=faults[f];
        CHECK(wifi_recovery_worker_run(&c,&old)); wifi_recovery_poll(&c);
        CHECK(wifi_recovery_status(&c).state==WIFI_REC_OUTCOME_UNKNOWN);
        CHECK(rejects_new(&c));
        CHECK(!zero(&s.candidate,sizeof(s.candidate)));
        CHECK(!memcmp(previous,m.disk[slot],sizeof(previous)));
        wifi_storage_completion_t result; CHECK(!wifi_recovery_completion(&c,&result));
        m.fault=GOOD; wifi_recovery_poll(&c);
        CHECK(wifi_recovery_status(&c).state==WIFI_REC_RECOVERY_REQUIRED);
        wifi_storage_job_t recovery=run_next(&c);
        bool promoted=faults[f]!=FAIL_BEFORE && faults[f]!=TORN;
        CHECK(wifi_recovery_completion(&c,&result));
        CHECK(result.outcome==(promoted?WIFI_CANCEL_DURABLE:WIFI_CANCEL_ABORTED));
        CHECK(!zero(result.operation.id,16) && result.operation.epoch==11 && result.operation.ticket==1);
        CHECK(zero(&s.candidate,sizeof(s.candidate)));
        CHECK(!memcmp(previous,m.disk[slot],sizeof(previous)));
        CHECK(!wifi_recovery_worker_run(&c,&old)); CHECK(!wifi_recovery_worker_run(&c,&recovery));
        CHECK(m.writes==1 && wifi_recovery_status(&c).attempts==1);
        wifi_store_t st=wifi_recovery_interface(&c); wifi_config_t loaded; uint32_t g=0;
        CHECK(st.load(st.ctx,&loaded,&g)); CHECK(g==(promoted?gen+1:gen));
    }
    puts("40 recovery faults across eight alternating prior generations: PASS");
}
static void bounded_recovery(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0};
    setup(&m,&s,&c); admit(&c,1,1); m.fault=FAIL_BEFORE; run_next(&c);
    m.reopen_failures=10;
    for(unsigned i=0;i<3;i++) {
        wifi_recovery_poll(&c); CHECK(wifi_recovery_status(&c).attempts==i+1);
        run_next(&c); CHECK(wifi_recovery_status(&c).state==WIFI_REC_OUTCOME_UNKNOWN);
        CHECK(rejects_new(&c));
    }
    wifi_recovery_poll(&c);
    wifi_recovery_status_t status=wifi_recovery_status(&c);
    CHECK(status.state==WIFI_REC_RESTART_REQUIRED && status.quiescent && status.candidate_retired);
    CHECK(zero(&s.candidate,sizeof(s.candidate)));
    unsigned reopens=m.reopens, writes=m.writes;
    for(unsigned i=0;i<100;i++) {
        wifi_recovery_poll(&c); wifi_recovery_request_restart(&c); CHECK(rejects_new(&c));
    }
    CHECK(m.reopens==reopens && m.writes==writes && c.attempts==3);
    puts("reopen errors exhaust exactly three configured attempts; repeated polls do no I/O: PASS");
}
static void deadlines(void)
{
    for(unsigned backwards=0;backwards<2;backwards++) {
        mock_t m={0}; m.now=10; wifi_storage_t s; wifi_recovery_t c={0};
        setup(&m,&s,&c); wifi_storage_job_t old=admit(&c,1,1);
        m.now=backwards?9:110; wifi_recovery_poll(&c);
        wifi_recovery_status_t status=wifi_recovery_status(&c);
        CHECK(status.state==WIFI_REC_RESTART_REQUIRED && status.quiescent && status.candidate_retired);
        CHECK(!wifi_recovery_worker_run(&c,&old)); CHECK(!m.writes && rejects_new(&c));
    }
    /* No overflowing absolute deadline arithmetic near uint64 exhaustion. */
    mock_t m={0}; m.now=UINT64_MAX-10; wifi_storage_t s; wifi_recovery_t c={0};
    setup(&m,&s,&c); admit(&c,1,1); m.now=0; wifi_recovery_poll(&c);
    CHECK(c.state==WIFI_REC_RESTART_REQUIRED);
    mock_t late={0}; wifi_storage_t late_s; wifi_recovery_t late_c={0};
    setup(&late,&late_s,&late_c); admit(&late_c,1,1); late.fault=FAIL_AFTER;
    run_next(&late_c); late.fault=GOOD; wifi_recovery_poll(&late_c);
    wifi_storage_job_t job; CHECK(wifi_recovery_job(&late_c,&job));
    CHECK(wifi_recovery_worker_run(&late_c,&job));
    late.now=100; wifi_recovery_poll(&late_c);
    CHECK(late_c.state==WIFI_REC_RESTART_REQUIRED && wifi_recovery_status(&late_c).quiescent);
    puts("elapsed policy, backward clock and wrap fail closed without writes: PASS");
}
static void queued_restart_and_epoch(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    seed(&m,0,1,2); setup(&m,&s,&c);
    wifi_storage_job_t old=begin_tx(&m,&c,&tx);
    unsigned reads=m.reads, reopens=m.reopens;
    wifi_recovery_request_restart(&c);
    CHECK(m.reads==reads && m.reopens==reopens); /* Owner request never runs NVS. */
    CHECK(!wifi_recovery_worker_run(&c,&old)); CHECK(!m.writes);
    wifi_recovery_poll(&c); wifi_storage_job_t scan=run_next(&c);
    CHECK(c.epoch==12 && wifi_recovery_status(&c).boot_ready);
    wifi_request_t early={0}; early.op=3; early.id[0]=2; early.config=config(8);
    CHECK(wifi_recovery_start_result(&c,&tx,&early,true)==WIFI_START_BUSY);
    CHECK(!wifi_recovery_worker_run(&c,&scan));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_REBOOT,0));
    CHECK(tx.state==WIFI_TX_IDLE && tx.generation==1 && !tx.recent_count && zero(tx.id,16));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&s.candidate,sizeof(s.candidate)));
    wifi_storage_completion_t stale={old,WIFI_CANCEL_DURABLE};
    CHECK(!wifi_recovery_deliver(&c,&tx,&stale));
    m.now=0; /* A restarted owner clock may use a reset epoch. */
    /* The coordinator clock itself remains monotonic in a soft recovery. */
    m.now=c.last_ms;
    wifi_request_t unauthorized={0}; unauthorized.op=3; unauthorized.id[0]=1; unauthorized.config=config(9);
    CHECK(wifi_tx_start_result(&tx,&unauthorized,false,m.now)==WIFI_START_UNAUTHORIZED);
    wifi_storage_job_t fresh=begin_existing_tx(&m,&c,&tx);
    CHECK(fresh.epoch==12 && fresh.ticket>old.ticket);
    CHECK(!wifi_recovery_worker_run(&c,&old));
    run_next(&c); wifi_storage_completion_t result;
    CHECK(wifi_recovery_completion(&c,&result));
    stale=result; stale.operation.epoch=11; CHECK(!wifi_recovery_deliver(&c,&tx,&stale));
    CHECK(wifi_recovery_deliver(&c,&tx,&result)); CHECK(tx.state==WIFI_TX_COMMITTED);
    puts("queued restart, fresh epoch, stale callbacks and fresh authorization: PASS");
}
typedef struct { wifi_recovery_t *c; wifi_storage_job_t job; bool ran; } thread_arg_t;
static void *thread_run(void *ctx)
{
    thread_arg_t *arg=ctx; arg->ran=wifi_recovery_worker_run(arg->c,&arg->job); return NULL;
}
static void running_restart(void)
{
    for(unsigned outcome=0;outcome<3;outcome++) for(unsigned deadline=0;deadline<2;deadline++) {
        mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
        setup(&m,&s,&c); wifi_storage_job_t job=begin_tx(&m,&c,&tx);
        gate_t gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,false,false};
        m.gate=&gate; m.fault=outcome==0?GOOD:outcome==1?FAIL_AFTER:REMOVE_FAIL;
        thread_arg_t arg={&c,job,false}; pthread_t thread;
        CHECK(!pthread_create(&thread,NULL,thread_run,&arg));
        CHECK(!pthread_mutex_lock(&gate.mutex));
        while(!gate.entered) CHECK(!pthread_cond_wait(&gate.cond,&gate.mutex));
        CHECK(!pthread_mutex_unlock(&gate.mutex));
        CHECK(wifi_recovery_status(&c).state==WIFI_REC_EXECUTING);
        wifi_store_t st=wifi_recovery_interface(&c);
        CHECK(st.cancel_commit(st.ctx)==WIFI_CANCEL_UNKNOWN);
        CHECK(!st.restart(st.ctx));
        CHECK(!wifi_recovery_init(&c,&s,99,(wifi_recovery_policy_t){1,10},hooks(&m)));
        if(deadline) m.now+=100;
        wifi_recovery_poll(&c);
        CHECK(!wifi_recovery_status(&c).quiescent);
        CHECK(!wifi_recovery_status(&c).candidate_retired);
        CHECK(!wifi_recovery_retire_transaction(&c,&tx)); CHECK(rejects_new(&c));
        wifi_storage_job_t extra; CHECK(!wifi_recovery_job(&c,&extra));
        CHECK(!wifi_recovery_worker_run(&c,&job));
        CHECK(!pthread_mutex_lock(&gate.mutex)); gate.release=true;
        CHECK(!pthread_cond_signal(&gate.cond)); CHECK(!pthread_mutex_unlock(&gate.mutex));
        CHECK(!pthread_join(thread,NULL)); CHECK(arg.ran); m.gate=NULL;
        wifi_recovery_poll(&c);
        if(deadline || outcome==2) {
            CHECK(c.state==WIFI_REC_RESTART_REQUIRED && wifi_recovery_status(&c).quiescent);
            CHECK(wifi_recovery_retire_transaction(&c,&tx));
            CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&s.candidate,sizeof(s.candidate)));
            CHECK(tx.state==WIFI_TX_COMMITTING && tx.boundary_unresolved && tx.carrier_lost);
            CHECK(rejects_new(&c));
        } else {
            /* Restart scan is scheduled only after the active call returned. */
            if(!wifi_recovery_job(&c,&extra)) wifi_recovery_poll(&c);
            run_next(&c); CHECK(c.epoch==12 && wifi_recovery_status(&c).boot_ready);
            CHECK(wifi_tx_event(&tx,WIFI_EVENT_REBOOT,0));
            CHECK(tx.generation==1 && tx.state==WIFI_TX_IDLE);
            CHECK(zero(&tx.candidate,sizeof(tx.candidate)));
        }
        CHECK(!wifi_recovery_worker_run(&c,&job));
        CHECK(!pthread_mutex_destroy(&gate.mutex)); CHECK(!pthread_cond_destroy(&gate.cond));
    }
    puts("six real host-thread interleavings: running restart/deadline waits for quiescence: PASS");
}
static void uncertainty_and_actual_boot(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    seed(&m,0,1,1); setup(&m,&s,&c); wifi_storage_job_t old=begin_tx(&m,&c,&tx);
    m.fault=REMOVE_FAIL; run_next(&c);
    CHECK(c.state==WIFI_REC_RESTART_REQUIRED && c.attempts==0);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    CHECK(!wifi_recovery_worker_run(&c,&old)); CHECK(rejects_new(&c));
    wifi_storage_t unsafe; CHECK(!wifi_storage_init(&unsafe,boundary(&m)));
    /* Simulated actual boot: persistent bytes only survive; no process latch,
     * pending job, candidate, FSM, session or authorization is copied. */
    mock_t boot={0}; memcpy(boot.disk,m.disk,sizeof(boot.disk)); memcpy(boot.len,m.len,sizeof(boot.len));
    wifi_storage_t fresh_storage; wifi_recovery_t fresh={0}; wifi_tx_t fresh_tx;
    /* Fresh epoch allocation is an external boot-owner responsibility. */
    setup_epoch(&boot,&fresh_storage,&fresh,12);
    CHECK(wifi_tx_init(&fresh_tx,wifi_recovery_interface(&fresh)));
    CHECK(fresh_tx.state==WIFI_TX_IDLE && fresh_tx.generation==1 && !fresh_tx.recent_count);
    CHECK(zero(fresh_tx.id,16) && zero(&fresh_tx.candidate,sizeof(fresh_tx.candidate)));
    wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config=config(9);
    CHECK(wifi_tx_start_result(&fresh_tx,&r,false,0)==WIFI_START_UNAUTHORIZED);
    CHECK(wifi_recovery_start_result(&fresh,&fresh_tx,&r,false)==WIFI_START_UNAUTHORIZED);
    wifi_storage_job_t new_job=begin_existing_tx(&boot,&fresh,&fresh_tx);
    CHECK(old.stamp==new_job.stamp && old.ticket==new_job.ticket); /* ABA pressure. */
    CHECK(!wifi_recovery_worker_run(&fresh,&old));
    CHECK(wifi_recovery_worker_run(&fresh,&new_job)); wifi_recovery_poll(&fresh);
    wifi_storage_completion_t result; CHECK(wifi_recovery_completion(&fresh,&result));
    wifi_storage_completion_t stale={old,WIFI_CANCEL_DURABLE};
    CHECK(!wifi_recovery_deliver(&fresh,&fresh_tx,&stale));
    CHECK(wifi_recovery_deliver(&fresh,&fresh_tx,&result));
    CHECK(fresh_tx.generation==2 && fresh_tx.state==WIFI_TX_COMMITTED);
    puts("REMOVE_FAILED equivalent requires restart; boot restores only committed authority: PASS");
}
static void boot_selection_and_failures(void)
{
    for(unsigned variant=0;variant<3;variant++) {
        mock_t m={0}; seed(&m,0,1,1); seed(&m,1,2,2);
        if(variant) m.disk[1][0]^=1;
        if(variant==2) m.disk[0][0]^=1;
        wifi_storage_t s; wifi_recovery_t c={0};
        bool ok=wifi_storage_init(&s,boundary(&m)); CHECK(ok==(variant!=2));
        if(!ok) { CHECK(!wifi_recovery_init(&c,&s,1,(wifi_recovery_policy_t){1,10},hooks(&m))); continue; }
        CHECK(wifi_recovery_init(&c,&s,1,(wifi_recovery_policy_t){1,10},hooks(&m)));
        wifi_config_t loaded; uint32_t gen; wifi_store_t st=wifi_recovery_interface(&c);
        CHECK(st.load(st.ctx,&loaded,&gen)); CHECK(gen==(variant?1u:2u));
        wifi_recovery_request_restart(&c); m.reopen_failures=1;
        wifi_recovery_poll(&c); run_next(&c); CHECK(!wifi_recovery_status(&c).boot_ready);
        wifi_recovery_poll(&c); CHECK(c.state==WIFI_REC_RESTART_REQUIRED);
    }
    puts("boot highest/torn/both invalid and failed restart scan: PASS");
}
static void normal_completion(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&m,&s,&c); wifi_storage_job_t old=begin_tx(&m,&c,&tx);
    wifi_storage_job_t wrong=old; wrong.id[0]^=1;
    CHECK(!wifi_recovery_worker_run(&c,&wrong)); CHECK(!m.writes);
    run_next(&c); CHECK(c.state==WIFI_REC_DURABLE_VERIFIED);
    CHECK(rejects_new(&c)); /* Result must reach its originating FSM first. */
    wifi_storage_completion_t result; CHECK(wifi_recovery_completion(&c,&result));
    wrong=old; wrong.ticket++; wifi_storage_completion_t stale={wrong,WIFI_CANCEL_DURABLE};
    CHECK(!wifi_recovery_deliver(&c,&tx,&stale));
    CHECK(wifi_recovery_deliver(&c,&tx,&result));
    CHECK(tx.state==WIFI_TX_COMMITTED && zero(&tx.candidate,sizeof(tx.candidate)));
    CHECK(zero(&s.candidate,sizeof(s.candidate)));
    CHECK(!wifi_recovery_deliver(&c,&tx,&result));
    puts("normal verified completion, copied identity and exactly-once delivery: PASS");
}
static void ownership_and_pre_gate(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}, second={0}; wifi_tx_t tx;
    setup(&m,&s,&c);
    CHECK(!wifi_recovery_init(&second,&s,12,(wifi_recovery_policy_t){1,10},hooks(&m)));
    CHECK(wifi_tx_init(&tx,wifi_recovery_interface(&c)));
    wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config=config(9);
    CHECK(wifi_tx_start(&tx,&r,true,0));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_CANCEL,1));
    CHECK(tx.state==WIFI_TX_ROLLBACK && zero(&tx.candidate,sizeof(tx.candidate)) && !m.writes);
    CHECK(wifi_tx_complete(&tx,WIFI_EVENT_ROLLBACK_SUCCESS,tx.id,0,tx.ticket,2));
    r.id[0]=2; CHECK(wifi_tx_start(&tx,&r,true,3));
    /* Restart failure before a promotion also needs an owner-side secret
     * retirement path; no storage operation identity exists yet. */
    wifi_recovery_request_restart(&c); m.reopen_failures=10;
    for(unsigned i=0;i<3;i++) { wifi_recovery_poll(&c); run_next(&c); }
    wifi_recovery_poll(&c); CHECK(c.state==WIFI_REC_RESTART_REQUIRED);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && tx.carrier_lost);
    CHECK(tx.state==WIFI_TX_CONNECTING); /* No invented terminal result. */
    puts("one coordinator owner, pre-gate rollback and pre-gate restart retirement: PASS");
}
static void unknown_transaction_and_mismatch(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&m,&s,&c); begin_tx(&m,&c,&tx); m.fault=READBACK_FAIL; run_next(&c);
    wifi_request_t r={0}; r.op=3; r.id[0]=2; r.config=config(10);
    CHECK(wifi_tx_start_result(&tx,&r,true,++m.now)==WIFI_START_BUSY);
    CHECK(wifi_recovery_start_result(&c,&tx,&r,true)==WIFI_START_BUSY);
    /* A checksummed, higher record with different candidate bytes is not
     * proof of the admitted operation. Exhaust budget without another write. */
    seed(&m,0,1,3); m.fault=GOOD;
    for(unsigned i=0;i<3;i++) {
        wifi_recovery_poll(&c); run_next(&c);
        CHECK(c.state==WIFI_REC_OUTCOME_UNKNOWN && rejects_new(&c));
    }
    wifi_recovery_poll(&c); CHECK(c.state==WIFI_REC_RESTART_REQUIRED && m.writes==1);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    wifi_response_t response;
    CHECK(!wifi_tx_project(&tx,4,&response));
    CHECK(zero(&s.candidate,sizeof(s.candidate)) && zero(&tx.candidate,sizeof(tx.candidate)));
    puts("UNKNOWN keeps FSM busy; mismatched authority cannot invent success; projection disabled: PASS");
}
static void idle_restart_admission(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&m,&s,&c); CHECK(wifi_tx_init(&tx,wifi_recovery_interface(&c)));
    wifi_recovery_request_restart(&c);
    wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config=config(9);
    CHECK(wifi_recovery_start_result(&c,&tx,&r,false)==WIFI_START_UNAUTHORIZED);
    CHECK(wifi_recovery_start_result(&c,&tx,&r,true)==WIFI_START_BUSY);
    CHECK(tx.state==WIFI_TX_IDLE && zero(&tx.candidate,sizeof(tx.candidate)));
    m.now=100; wifi_recovery_poll(&c);
    CHECK(wifi_recovery_start_result(&c,&tx,&r,true)==WIFI_START_BUSY);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    puts("idle FSM cannot admit a candidate during recovery or restart-required: PASS");
}
int main(void)
{
    queued_cancel(); recovery_matrix(); bounded_recovery(); deadlines();
    queued_restart_and_epoch(); running_restart(); uncertainty_and_actual_boot();
    boot_selection_and_failures(); normal_completion(); ownership_and_pre_gate();
    unknown_transaction_and_mismatch();
    idle_restart_admission();
    printf("HOST_RECOVERY_SIMULATION: %u checks passed\n",checks);
    return 0;
}
