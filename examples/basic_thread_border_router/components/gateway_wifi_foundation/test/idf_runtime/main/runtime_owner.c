#include "runtime_owner.h"
#include "wifi_recovery.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

/* Private, synthetic-only owner. No public event submission, radio or NVS.
 * All owner functions and the bounded queue run on this one FreeRTOS task. */
#define OWNER_STACK_BYTES 6144u
#define QUEUE_CAPACITY 8u
typedef enum { EV_ASSOC, EV_IP, EV_STABLE, EV_PROMOTE, EV_CANCEL, EV_LINK_LOST, EV_ROLLBACK_DONE } owner_event_t;
typedef struct {
    uint8_t slot[2][WIFI_STORE_RECORD_MAX]; size_t length[2];
    bool fail_commit, fail_reopen;
} synthetic_disk_t;
typedef struct {
    synthetic_disk_t disk; wifi_storage_t storage; wifi_recovery_t recovery; wifi_tx_t tx;
    owner_event_t queue[QUEUE_CAPACITY]; unsigned head, count;
} synthetic_owner_t;
static synthetic_owner_t owner;
static uint64_t monotonic_ms(void *ctx) { (void)ctx; return (uint64_t)esp_timer_get_time()/1000u; }
static wifi_io_result_t read_slot(void *ctx,unsigned slot,uint8_t *p,size_t cap,size_t *n) {
    synthetic_disk_t *d=ctx; if(slot>1 || !n) return WIFI_IO_ERROR; *n=0;
    if(!d->length[slot]) return WIFI_IO_MISSING;
    if(d->length[slot]>cap) return WIFI_IO_INVALID;
    *n=d->length[slot]; memcpy(p,d->slot[slot],*n); return WIFI_IO_OK;
}
static bool write_slot(void *ctx,unsigned slot,const uint8_t *p,size_t n) {
    synthetic_disk_t *d=ctx; if(slot>1 || n>WIFI_STORE_RECORD_MAX) return false;
    memcpy(d->slot[slot],p,n); d->length[slot]=n; return true;
}
static bool commit(void *ctx) { return !((synthetic_disk_t *)ctx)->fail_commit; }
static bool reopen_store(void *ctx) { return !((synthetic_disk_t *)ctx)->fail_reopen; }
static bool init(void) {
    memset(&owner,0,sizeof(owner));
    wifi_store_io_t io={&owner.disk,read_slot,write_slot,commit,reopen_store};
    wifi_recovery_hooks_t hooks={NULL,monotonic_ms,NULL};
    return wifi_storage_init(&owner.storage,io) &&
        wifi_recovery_init(&owner.recovery,&owner.storage,1,(wifi_recovery_policy_t){3,10000},hooks) &&
        wifi_tx_init(&owner.tx,wifi_recovery_interface(&owner.recovery));
}
static bool enqueue(owner_event_t e) {
    if(owner.count==QUEUE_CAPACITY) return false;
    owner.queue[(owner.head+owner.count)%QUEUE_CAPACITY]=e; owner.count++; return true;
}
static bool drain(void) {
    while(owner.count) {
        owner_event_t e=owner.queue[owner.head]; owner.head=(owner.head+1)%QUEUE_CAPACITY; owner.count--;
        wifi_event_t we;
        switch(e) {
        case EV_ASSOC: we=WIFI_EVENT_ASSOCIATED; break;
        case EV_IP: we=WIFI_EVENT_IP_READY; break;
        case EV_STABLE: we=WIFI_EVENT_STABILITY_COMPLETE; break;
        case EV_PROMOTE: we=WIFI_EVENT_PROMOTION_ADMIT; break;
        case EV_CANCEL: we=WIFI_EVENT_CANCEL; break;
        case EV_LINK_LOST: we=WIFI_EVENT_LINK_LOST; break;
        case EV_ROLLBACK_DONE:
            if(!wifi_tx_complete(&owner.tx,WIFI_EVENT_ROLLBACK_SUCCESS,owner.tx.id,owner.tx.generation,owner.tx.ticket,monotonic_ms(NULL))) return false;
            continue;
        default: return false;
        }
        if(!wifi_tx_event(&owner.tx,we,monotonic_ms(NULL))) return false;
    }
    return true;
}
static bool event(owner_event_t e) { return enqueue(e) && drain(); }
static bool run_worker(void) {
    wifi_storage_job_t job; wifi_storage_completion_t completion;
    if(!wifi_recovery_job(&owner.recovery,&job) || !wifi_recovery_worker_run(&owner.recovery,&job)) return false;
    wifi_recovery_poll(&owner.recovery);
    if(wifi_recovery_completion(&owner.recovery,&completion)) return wifi_recovery_deliver(&owner.recovery,&owner.tx,&completion);
    return true;
}
static bool synthetic_candidate(uint8_t id) {
    /* Fixed bytes, exclusively reachable from the compiled-in self-test. */
    uint8_t bytes[36]={1,3,0,0}; wifi_request_t request; bytes[4]=id;
    bytes[20]=1; bytes[21]=4; bytes[22]=8; memcpy(bytes+24,"TESTsynthetic",12);
    bool ok=wifi_request_decode(bytes,sizeof(bytes),&request) &&
        wifi_recovery_start_result(&owner.recovery,&owner.tx,&request,true)==WIFI_START_ACCEPTED;
    wifi_zero(&request,sizeof(request)); wifi_zero(bytes,sizeof(bytes)); return ok;
}
static bool scan_and_status(void) {
    uint8_t out[296]; size_t n; wifi_response_t r={0};
    r.op=1; if(!wifi_response_encode(&r,out,sizeof(out),&n) || n!=12 || out[8]) return false;
    memset(&r,0,sizeof(r)); r.op=2; r.phase=1;
    if(!wifi_response_encode(&r,out,sizeof(out),&n) || out[2]!=1) return false;
    r.phase=0; r.u.scan.count=1; r.u.scan.entries[0].ssid_len=4;
    r.u.scan.entries[0].security=1; r.u.scan.entries[0].channel=1;
    r.u.scan.entries[0].rssi=-45; memcpy(r.u.scan.entries[0].ssid,"TEST",4);
    return wifi_response_encode(&r,out,sizeof(out),&n) && n==16 && out[4]==1;
}
static bool validate_candidate(void) {
    if(!enqueue(EV_ASSOC) || !enqueue(EV_IP) || !drain()) return false;
    /* Observe the complete normative window using ESP-IDF monotonic time. */
    vTaskDelay(pdMS_TO_TICKS(5100)); return event(EV_STABLE) && owner.tx.state==WIFI_TX_VALIDATED;
}
static bool selftest(void) {
    if(!init() || !scan_and_status()) return false;
    for(unsigned i=0;i<QUEUE_CAPACITY;i++) if(!enqueue(EV_ASSOC)) return false;
    if(enqueue(EV_ASSOC)) return false;
    owner.head=owner.count=0; /* Saturation test only; no lifecycle event admitted. */
    puts("ADR091_RUNTIME_SELFTEST: status scan queue PASS");
    if(!synthetic_candidate(1) || !validate_candidate() || !event(EV_PROMOTE) || !run_worker() || owner.tx.state!=WIFI_TX_COMMITTED || owner.tx.generation!=1) return false;
    puts("ADR091_RUNTIME_SELFTEST: validation commit PASS");
    if(!synthetic_candidate(2) || !event(EV_CANCEL) || owner.tx.state!=WIFI_TX_ROLLBACK || !event(EV_ROLLBACK_DONE) || owner.tx.state!=WIFI_TX_CANCELLED || owner.tx.generation!=1) return false;
    if(!synthetic_candidate(3) || !event(EV_LINK_LOST) || !event(EV_ROLLBACK_DONE) || owner.tx.state!=WIFI_TX_FAILED || owner.tx.generation!=1) return false;
    puts("ADR091_RUNTIME_SELFTEST: cancellation rollback PASS");
    if(!synthetic_candidate(4) || !validate_candidate() || !event(EV_PROMOTE)) return false;
    owner.disk.fail_commit=true; owner.disk.fail_reopen=true;
    if(!run_worker() || owner.tx.state!=WIFI_TX_COMMITTING || owner.recovery.state!=WIFI_REC_OUTCOME_UNKNOWN) return false;
    owner.disk.fail_commit=false; owner.disk.fail_reopen=false;
    wifi_recovery_request_restart(&owner.recovery); wifi_recovery_poll(&owner.recovery);
    if(!run_worker() || !wifi_recovery_status(&owner.recovery).boot_ready || !wifi_tx_event(&owner.tx,WIFI_EVENT_REBOOT,monotonic_ms(NULL)) || owner.tx.state!=WIFI_TX_IDLE || owner.tx.generation!=2) return false;
    puts("ADR091_RUNTIME_SELFTEST: uncertain outcome restart recovery PASS"); return true;
}
static void owner_task(void *arg) {
    (void)arg; bool ok=selftest();
    printf("ADR091_RUNTIME_SELFTEST: %s owner_bytes=%u queue_capacity=%u stack_config_bytes=%u stack_high_water_bytes=%u\n",ok?"PASS":"FAIL",(unsigned)sizeof(owner),QUEUE_CAPACITY,OWNER_STACK_BYTES,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    /* No queued worker survives this single-task fixture. Retire all synthetic RAM. */
    wifi_zero(&owner,sizeof(owner)); vTaskDelete(NULL);
}
bool adr091_synthetic_owner_start(void) {
    return xTaskCreate(owner_task,"adr091_selftest",OWNER_STACK_BYTES,NULL,5,NULL)==pdPASS;
}
