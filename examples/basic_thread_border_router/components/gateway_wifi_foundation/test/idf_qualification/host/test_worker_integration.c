/* Reuse the existing synthetic NVS boundary and deterministic fault fixtures,
 * not the production NVS adapter. The accepted recovery tests are also run by
 * their own runner; renaming main here keeps fixture helpers local to this TU. */
#define main accepted_recovery_test_main
#include "../../test_recovery.c"
#undef main
#include "worker_scheduler.h"

static bool schedule(qual_worker_t *worker, const wifi_storage_job_t *job)
{
    bool accepted=false;
    CHECK(qual_worker_submit(worker,job));
    CHECK(qual_worker_wait(worker,&accepted));
    return accepted;
}
static void worker_normal_and_identity(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t coordinator={0}; wifi_tx_t tx;
    setup(&mock,&store,&coordinator); wifi_storage_job_t valid=begin_tx(&mock,&coordinator,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&coordinator));
    for(unsigned i=0;i<6;i++) {
        wifi_storage_job_t wrong=valid;
        switch(i) {case 0:wrong.epoch++;break;case 1:wrong.stamp+=4;break;
        case 2:wrong.ticket++;break;case 3:wrong.id[0]^=1;break;
        case 4:wrong.generation++;break;default:wrong.kind=WIFI_WORK_RECOVER;break;}
        CHECK(!schedule(&worker,&wrong)); CHECK(!mock.writes);
        CHECK(tx.state==WIFI_TX_COMMITTING && tx.generation==0);
    }
    wifi_storage_job_t input=valid;
    CHECK(qual_worker_submit(&worker,&input)); memset(&input,0,sizeof(input));
    bool accepted=false; CHECK(qual_worker_wait(&worker,&accepted) && accepted);
    /* Worker completion alone cannot advance owner FSM. */
    CHECK(tx.state==WIFI_TX_COMMITTING && tx.generation==0);
    wifi_recovery_poll(&coordinator); wifi_storage_completion_t result;
    CHECK(wifi_recovery_completion(&coordinator,&result));
    CHECK(result.operation.epoch==valid.epoch && result.operation.stamp==valid.stamp &&
          result.operation.ticket==valid.ticket && result.operation.generation==valid.generation &&
          !memcmp(result.operation.id,valid.id,16));
    wifi_storage_completion_t forged=result; forged.operation.epoch--;
    CHECK(!wifi_recovery_deliver(&coordinator,&tx,&forged));
    CHECK(tx.state==WIFI_TX_COMMITTING);
    CHECK(wifi_recovery_deliver(&coordinator,&tx,&result));
    CHECK(tx.state==WIFI_TX_COMMITTED && tx.generation==1);
    CHECK(!wifi_recovery_deliver(&coordinator,&tx,&result));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&store.candidate,sizeof(store.candidate)));
    CHECK(!schedule(&worker,&valid)); CHECK(mock.writes==1);
    CHECK(qual_worker_stop(&worker)); CHECK(!worker.coordinator && !worker.started);
    puts("worker copied identity, owner delivery, stale and duplicate completion: PASS");
}
static void worker_queued_restart(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t old=begin_tx(&mock,&c,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c));
    CHECK(qual_worker_pause(&worker,true)); CHECK(qual_worker_submit(&worker,&old));
    CHECK(qual_worker_wait_parked(&worker));
    wifi_recovery_request_restart(&c);
    CHECK(qual_worker_pause(&worker,false));
    bool accepted=true; CHECK(qual_worker_wait(&worker,&accepted) && !accepted); CHECK(!mock.writes);
    wifi_recovery_poll(&c); wifi_storage_job_t scan;
    CHECK(wifi_recovery_job(&c,&scan) && scan.kind==WIFI_WORK_RESTART_SCAN);
    CHECK(schedule(&worker,&scan)); wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).boot_ready);
    CHECK(!schedule(&worker,&old)); CHECK(!mock.writes);
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_REBOOT,0));
    CHECK(tx.state==WIFI_TX_IDLE && zero(&tx.candidate,sizeof(tx.candidate)));
    CHECK(qual_worker_stop(&worker));
    puts("queued restart fences stale scheduler copy before worker claim: PASS");
}
static void worker_running_restart(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t job=begin_tx(&mock,&c,&tx);
    gate_t gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,false,false};
    mock.gate=&gate; mock.fault=FAIL_AFTER;
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c)); CHECK(qual_worker_submit(&worker,&job));
    CHECK(!pthread_mutex_lock(&gate.mutex)); while(!gate.entered) CHECK(!pthread_cond_wait(&gate.cond,&gate.mutex));
    CHECK(!pthread_mutex_unlock(&gate.mutex));
    wifi_recovery_request_restart(&c); wifi_recovery_poll(&c);
    CHECK(!wifi_recovery_status(&c).quiescent && !wifi_recovery_status(&c).candidate_retired);
    CHECK(!qual_worker_submit(&worker,&job)); CHECK(tx.state==WIFI_TX_COMMITTING);
    CHECK(!pthread_mutex_lock(&gate.mutex)); gate.release=true; CHECK(!pthread_cond_signal(&gate.cond));
    CHECK(!pthread_mutex_unlock(&gate.mutex));
    bool accepted=false; CHECK(qual_worker_wait(&worker,&accepted) && accepted); mock.gate=NULL;
    wifi_recovery_poll(&c); wifi_storage_job_t scan;
    if(!wifi_recovery_job(&c,&scan)) { wifi_recovery_poll(&c); CHECK(wifi_recovery_job(&c,&scan)); }
    CHECK(scan.kind==WIFI_WORK_RESTART_SCAN); CHECK(schedule(&worker,&scan)); wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).boot_ready); CHECK(wifi_tx_event(&tx,WIFI_EVENT_REBOOT,0));
    CHECK(tx.state==WIFI_TX_IDLE && tx.generation==1);
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&store.candidate,sizeof(store.candidate)));
    CHECK(qual_worker_stop(&worker)); CHECK(!worker.coordinator);
    CHECK(!pthread_mutex_destroy(&gate.mutex)); CHECK(!pthread_cond_destroy(&gate.cond));
    puts("running ambiguous write drains before restart recovery: PASS");
}
static void worker_stop_pending(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t old=begin_tx(&mock,&c,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c));
    CHECK(qual_worker_pause(&worker,true)); CHECK(qual_worker_submit(&worker,&old));
    CHECK(qual_worker_wait_parked(&worker));
    wifi_recovery_request_restart(&c); CHECK(qual_worker_request_stop(&worker));
    bool accepted=true; CHECK(!qual_worker_wait(&worker,&accepted));
    CHECK(!qual_worker_submit(&worker,&old)); CHECK(qual_worker_stop(&worker));
    CHECK(!worker.started && !worker.coordinator && !mock.writes);
    CHECK(!wifi_recovery_status(&c).boot_ready && !wifi_recovery_status(&c).candidate_retired);
    CHECK(tx.state==WIFI_TX_COMMITTING && rejects_new(&c));
    CHECK(!wifi_recovery_worker_run(&c,&old));
    wifi_recovery_poll(&c); wifi_storage_job_t scan;
    CHECK(wifi_recovery_job(&c,&scan) && scan.kind==WIFI_WORK_RESTART_SCAN);
    CHECK(qual_worker_start(&worker,&c)); CHECK(schedule(&worker,&scan)); wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).boot_ready && !mock.writes);
    CHECK(qual_worker_stop(&worker));
    puts("stop discards fenced pending copy without declaring retirement: PASS");
}
static void worker_stop_running(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t job=begin_tx(&mock,&c,&tx);
    gate_t gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,false,false};
    mock.gate=&gate; mock.fault=FAIL_AFTER;
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c)); CHECK(qual_worker_submit(&worker,&job));
    CHECK(!pthread_mutex_lock(&gate.mutex)); while(!gate.entered) CHECK(!pthread_cond_wait(&gate.cond,&gate.mutex));
    CHECK(!pthread_mutex_unlock(&gate.mutex));
    wifi_recovery_request_restart(&c); CHECK(qual_worker_request_stop(&worker)); wifi_recovery_poll(&c);
    CHECK(!wifi_recovery_status(&c).quiescent && !wifi_recovery_status(&c).candidate_retired);
    CHECK(!wifi_recovery_retire_transaction(&c,&tx)); CHECK(rejects_new(&c));
    CHECK(!pthread_mutex_lock(&gate.mutex)); gate.release=true; CHECK(!pthread_cond_signal(&gate.cond));
    CHECK(!pthread_mutex_unlock(&gate.mutex)); CHECK(qual_worker_stop(&worker)); mock.gate=NULL;
    CHECK(!worker.started && !worker.coordinator); CHECK(!wifi_recovery_worker_run(&c,&job));
    wifi_recovery_poll(&c); wifi_storage_job_t scan;
    CHECK(wifi_recovery_job(&c,&scan) && scan.kind==WIFI_WORK_RESTART_SCAN);
    CHECK(qual_worker_start(&worker,&c)); CHECK(schedule(&worker,&scan)); wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).boot_ready); CHECK(wifi_tx_event(&tx,WIFI_EVENT_REBOOT,0));
    CHECK(tx.state==WIFI_TX_IDLE && tx.generation==1);
    CHECK(qual_worker_stop(&worker));
    CHECK(!pthread_mutex_destroy(&gate.mutex)); CHECK(!pthread_cond_destroy(&gate.cond));
    puts("stop drains claimed worker before retirement and restart scan: PASS");
}
static void worker_uncertainty_and_budget(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t job=begin_tx(&mock,&c,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c)); mock.fault=FAIL_BEFORE;
    CHECK(schedule(&worker,&job)); wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).state==WIFI_REC_OUTCOME_UNKNOWN && rejects_new(&c));
    wifi_storage_completion_t result; CHECK(!wifi_recovery_completion(&c,&result));
    mock.reopen_failures=10;
    for(unsigned i=0;i<3;i++) {
        wifi_recovery_poll(&c); wifi_storage_job_t recover;
        CHECK(wifi_recovery_job(&c,&recover)); CHECK(schedule(&worker,&recover)); wifi_recovery_poll(&c);
        CHECK(wifi_recovery_status(&c).state==WIFI_REC_OUTCOME_UNKNOWN);
    }
    wifi_recovery_poll(&c); CHECK(c.state==WIFI_REC_RESTART_REQUIRED);
    CHECK(wifi_recovery_status(&c).quiescent && rejects_new(&c));
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&store.candidate,sizeof(store.candidate)));
    CHECK(qual_worker_stop(&worker)); CHECK(!worker.coordinator);
    puts("unknown outcome blocks replacement; bounded recovery retires secrets: PASS");
}
static void worker_elapsed_budget(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t job=begin_tx(&mock,&c,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c));
    mock.now+=100; wifi_recovery_poll(&c);
    CHECK(wifi_recovery_status(&c).state==WIFI_REC_RESTART_REQUIRED);
    CHECK(wifi_recovery_status(&c).quiescent && rejects_new(&c));
    CHECK(!schedule(&worker,&job)); CHECK(!mock.writes);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&store.candidate,sizeof(store.candidate)));
    CHECK(qual_worker_stop(&worker));
    puts("synthetic elapsed budget fences queued worker: PASS");
}
int main(void)
{
    worker_normal_and_identity(); worker_queued_restart(); worker_running_restart(); worker_stop_pending(); worker_stop_running(); worker_uncertainty_and_budget(); worker_elapsed_budget();
    printf("HOST_WORKER_INTEGRATION: %u checks PASS\n",checks);
    return 0;
}
