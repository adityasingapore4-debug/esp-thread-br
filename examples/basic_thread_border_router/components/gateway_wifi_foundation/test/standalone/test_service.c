#include "service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t slots[2][WIFI_STORE_RECORD_MAX]; size_t lengths[2];
    uint64_t time; bool fail_read, fail_reopen, fail_commit;
} fixture_t;
static wifi_io_result_t rd(void *v,unsigned slot,uint8_t *p,size_t cap,size_t *n) {
    fixture_t *f=v; assert(slot<2); *n=0; if(f->fail_read) return WIFI_IO_ERROR;
    if(!f->lengths[slot]) return WIFI_IO_MISSING;
    if(f->lengths[slot]>cap) return WIFI_IO_INVALID;
    *n=f->lengths[slot]; memcpy(p,f->slots[slot],*n); return WIFI_IO_OK;
}
static bool wr(void *v,unsigned slot,const uint8_t *p,size_t n) {
    fixture_t *f=v; assert(slot<2 && n<=WIFI_STORE_RECORD_MAX); memcpy(f->slots[slot],p,n); f->lengths[slot]=n; return true;
}
static bool commit(void *v) { return !((fixture_t *)v)->fail_commit; }
static bool reopen_store(void *v) { return !((fixture_t *)v)->fail_reopen; }
static uint64_t clock_ms(void *v) { return ((fixture_t *)v)->time; }
static void init(offline_wifi_service_t *s,fixture_t *f) {
    wifi_store_io_t io={f,rd,wr,commit,reopen_store};
    wifi_recovery_hooks_t hooks={f,clock_ms,NULL};
    assert(offline_wifi_init(s,io,1,(wifi_recovery_policy_t){3,10000},hooks));
}
static void payload(uint8_t *p, uint8_t id) {
    memset(p,0,36); p[0]=1; p[1]=3; p[4]=id; p[20]=1; p[21]=4; p[22]=8;
    memcpy(p+24,"TESTsynthetic",12);
}
static void start(offline_wifi_service_t *s,uint8_t id) {
    uint8_t p[36]; payload(p,id); assert(offline_wifi_candidate(s,p,sizeof(p))==WIFI_START_ACCEPTED); wifi_zero(p,sizeof(p));
}
static void validate(offline_wifi_service_t *s,fixture_t *f) {
    f->time+=1; assert(offline_wifi_event(s,WIFI_EVENT_ASSOCIATED));
    f->time+=1; assert(offline_wifi_event(s,WIFI_EVENT_IP_READY));
    f->time+=5000; assert(offline_wifi_event(s,WIFI_EVENT_STABILITY_COMPLETE));
    assert(s->tx.state==WIFI_TX_VALIDATED);
}
static void complete(offline_wifi_service_t *s) {
    assert(offline_wifi_event(s,WIFI_EVENT_PROMOTION_ADMIT));
    assert(offline_wifi_work(s)); offline_wifi_poll(s);
}
static void rollback_complete(offline_wifi_service_t *s) {
    offline_rollback_token_t token, stale;
    assert(offline_wifi_rollback_token(s,&token)); stale=token; stale.epoch++;
    assert(!offline_wifi_rollback_complete(s,&stale,true));
    assert(offline_wifi_rollback_complete(s,&token,true));
    assert(!offline_wifi_rollback_complete(s,&token,true));
}
static void expect_tx(offline_wifi_service_t *s,uint8_t state) {
    uint8_t out[296]; size_t n=0; assert(offline_wifi_transaction(s,out,sizeof(out),&n));
    assert(n==28 && out[20]==state);
}
static void test_status_scan(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); uint8_t out[296], p[36]; size_t n;
    assert(offline_wifi_status(&s,out,sizeof(out),&n) && n==12 && !out[8]);
    assert(offline_wifi_scan_begin(&s)); assert(offline_wifi_scan_response(&s,out,sizeof(out),&n) && out[2]==1);
    payload(p,1); assert(offline_wifi_candidate(&s,p,sizeof(p))==WIFI_START_BUSY);
    wifi_scan_entry_t ap={.ssid_len=4,.security=1,.rssi=-42,.channel=1}; memcpy(ap.ssid,"TEST",4);
    assert(offline_wifi_scan_complete(&s,&ap,1,false)); assert(offline_wifi_scan_response(&s,out,sizeof(out),&n) && n==16 && out[4]==1 && !memcmp(out+12,"TEST",4));
    f.time+=30000; assert(!offline_wifi_scan_response(&s,out,sizeof(out),&n));
    assert(offline_wifi_scan_begin(&s)); assert(offline_wifi_scan_complete(&s,NULL,0,false));
    assert(offline_wifi_scan_response(&s,out,sizeof(out),&n) && out[2]==2);
    assert(offline_wifi_scan_begin(&s)); f.time+=10000; assert(!offline_wifi_scan_response(&s,out,sizeof(out),&n));
    puts("status and bounded synthetic scans: PASS");
}
static void test_commit_restart(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); start(&s,1); expect_tx(&s,1); validate(&s,&f);
    complete(&s); expect_tx(&s,4); assert(s.tx.generation==1); uint8_t out[64]; size_t n;
    assert(offline_wifi_status(&s,out,sizeof(out),&n) && n==16 && out[8]==1 && !memcmp(out+12,"TEST",4));
    offline_wifi_restart_request(&s); offline_wifi_poll(&s); assert(offline_wifi_work(&s)); offline_wifi_poll(&s);
    assert(offline_wifi_restart_finish(&s)); assert(s.tx.state==WIFI_TX_IDLE && s.tx.generation==1);
    assert(!offline_wifi_transaction(&s,out,sizeof(out),&n));
    puts("validated commit and restart from verified record: PASS");
}
static void test_failure_cancel_timeout(void) {
    for(unsigned i=0;i<4;i++) {
        fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); start(&s,1); validate(&s,&f); complete(&s); assert(s.tx.generation==1);
        start(&s,2); wifi_event_t event=WIFI_EVENT_LINK_LOST;
        if(i==1) { f.time++; assert(offline_wifi_event(&s,WIFI_EVENT_ASSOCIATED)); f.time+=45000; event=WIFI_EVENT_TIMEOUT; }
        if(i==2) event=WIFI_EVENT_CANCEL;
        if(i==3) { f.time+=45000; event=WIFI_EVENT_TIMEOUT; }
        assert(offline_wifi_event(&s,event)); expect_tx(&s,7); assert(s.tx.generation==1);
        rollback_complete(&s); expect_tx(&s,i==2?6:5);
        assert(s.tx.reason==(i==0?3:i==1?2:i==2?4:1));
        assert(s.tx.generation==1 && !s.tx.candidate.secret_len);
    }
    puts("connection/IP failures, timeout, cancellation and rollback: PASS");
}
static void test_ordering(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); start(&s,1); validate(&s,&f);
    assert(offline_wifi_event(&s,WIFI_EVENT_CANCEL)); assert(!offline_wifi_event(&s,WIFI_EVENT_PROMOTION_ADMIT)); rollback_complete(&s);
    start(&s,2); validate(&s,&f); assert(offline_wifi_event(&s,WIFI_EVENT_PROMOTION_ADMIT));
    assert(offline_wifi_event(&s,WIFI_EVENT_CANCEL)); assert(offline_wifi_work(&s)); offline_wifi_poll(&s); expect_tx(&s,4);
    puts("cancellation ordering before and after promotion: PASS");
}
static void test_unknown(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); start(&s,1); validate(&s,&f);
    assert(offline_wifi_event(&s,WIFI_EVENT_PROMOTION_ADMIT)); f.fail_commit=true; f.fail_reopen=true;
    assert(offline_wifi_work(&s)); offline_wifi_poll(&s);
    assert(s.tx.state==WIFI_TX_COMMITTING); uint8_t p[36]; payload(p,2);
    assert(offline_wifi_candidate(&s,p,sizeof(p))==WIFI_START_BUSY);
    offline_wifi_poll(&s); assert(offline_wifi_work(&s)); offline_wifi_poll(&s); assert(s.tx.state==WIFI_TX_COMMITTING);
    f.fail_reopen=false; f.fail_commit=false;
    offline_wifi_restart_request(&s); offline_wifi_poll(&s); assert(offline_wifi_work(&s)); offline_wifi_poll(&s); assert(offline_wifi_restart_finish(&s));
    assert(s.tx.state==WIFI_TX_IDLE && s.tx.generation==1 && !s.tx.candidate.secret_len);
    puts("uncertain outcome excludes replacement until verified restart: PASS");
}
static void test_deadline_and_invalid(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); uint8_t p[36]; payload(p,1);
    p[35]=0; assert(offline_wifi_candidate(&s,p,sizeof(p))==WIFI_START_INVALID);
    payload(p,1); assert(offline_wifi_candidate(&s,p,sizeof(p))==WIFI_START_ACCEPTED);
    assert(!offline_wifi_event(&s,WIFI_EVENT_COMMIT_SUCCESS));
    f.time=1; assert(offline_wifi_event(&s,WIFI_EVENT_ASSOCIATED));
    f.time=39999; assert(offline_wifi_event(&s,WIFI_EVENT_IP_READY));
    f.time=44999; assert(offline_wifi_event(&s,WIFI_EVENT_STABILITY_COMPLETE));
    assert(s.tx.state==WIFI_TX_VALIDATED); assert(offline_wifi_event(&s,WIFI_EVENT_PROMOTION_ADMIT));
    f.time=60000; assert(offline_wifi_work(&s)); offline_wifi_poll(&s); expect_tx(&s,4);
    start(&s,2); f.time+=1; assert(offline_wifi_event(&s,WIFI_EVENT_ASSOCIATED));
    f.time+=39999; assert(offline_wifi_event(&s,WIFI_EVENT_IP_READY));
    f.time+=5000; assert(offline_wifi_event(&s,WIFI_EVENT_STABILITY_COMPLETE));
    assert(s.tx.state==WIFI_TX_ROLLBACK && s.tx.reason==1);
    rollback_complete(&s);
    puts("deadline boundary, delayed storage and malformed input: PASS");
}
static void test_restart_failure(void) {
    fixture_t f={0}; offline_wifi_service_t s; init(&s,&f); start(&s,1); validate(&s,&f);
    assert(offline_wifi_event(&s,WIFI_EVENT_PROMOTION_ADMIT)); wifi_storage_job_t stale;
    assert(wifi_recovery_job(&s.recovery,&stale));
    f.fail_reopen=true; offline_wifi_restart_request(&s); offline_wifi_poll(&s);
    assert(!wifi_recovery_worker_run(&s.recovery,&stale));
    assert(offline_wifi_work(&s)); offline_wifi_poll(&s);
    assert(!offline_wifi_restart_finish(&s)); uint8_t p[36]; payload(p,2);
    assert(offline_wifi_candidate(&s,p,sizeof(p))==WIFI_START_BUSY);
    f.fail_reopen=false; offline_wifi_poll(&s); assert(offline_wifi_work(&s)); offline_wifi_poll(&s);
    assert(offline_wifi_restart_finish(&s)); assert(s.tx.generation==0);
    puts("restart fence rejects stale work and failed restart blocks admission: PASS");
}
int main(void) { test_status_scan(); test_commit_restart(); test_failure_cancel_timeout(); test_ordering(); test_unknown(); test_deadline_and_invalid(); test_restart_failure(); puts("7 standalone service cases passed"); return 0; }
