#define QUAL_CASE_DRIVER
#include "test_worker_integration.c"
#undef main

static void synthetic_secret_case(void)
{
    mock_t mock={0}; wifi_storage_t store; wifi_recovery_t c={0}; wifi_tx_t tx;
    setup(&mock,&store,&c); wifi_storage_job_t job=begin_tx(&mock,&c,&tx);
    qual_worker_t worker; CHECK(qual_worker_start(&worker,&c)); mock.fault=FAIL_BEFORE;
    CHECK(schedule(&worker,&job)); wifi_recovery_poll(&c);
    CHECK(c.state==WIFI_REC_OUTCOME_UNKNOWN && rejects_new(&c));
    wifi_storage_completion_t completion; CHECK(!wifi_recovery_completion(&c,&completion));
    mock.reopen_failures=10;
    for(unsigned i=0;i<3;i++) {
        wifi_recovery_poll(&c); wifi_storage_job_t recover;
        CHECK(wifi_recovery_job(&c,&recover)); CHECK(schedule(&worker,&recover)); wifi_recovery_poll(&c);
    }
    wifi_recovery_poll(&c); CHECK(c.state==WIFI_REC_RESTART_REQUIRED);
    CHECK(wifi_recovery_retire_transaction(&c,&tx));
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)) && zero(&store.candidate,sizeof(store.candidate)));
    unsigned writes=mock.writes; CHECK(rejects_new(&c)); CHECK(mock.writes==writes);
    CHECK(qual_worker_stop(&worker));
}
int main(int argc, char **argv)
{
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"STALE_WORKER")) worker_normal_and_identity();
    else if(!strcmp(argv[1],"QUEUED_FENCE")) worker_queued_restart();
    else if(!strcmp(argv[1],"RUNNING_DRAIN")) worker_running_restart();
    else if(!strcmp(argv[1],"UNRESOLVED")) worker_uncertainty_and_budget();
    else if(!strcmp(argv[1],"RESTART_SCAN")) worker_running_restart();
    else if(!strcmp(argv[1],"SYNTHETIC_SECRET")) synthetic_secret_case();
    else return 2;
    printf("CASE_ASSERTIONS:%s:%u\n",argv[1],checks);
    return checks ? 0 : 3;
}
