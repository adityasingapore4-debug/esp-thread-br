#define main baseline_test
#include "test_wifi.c"
#undef main
static void begin(wifi_tx_t *t, wifi_request_t *r, uint64_t now) {
 assert(wifi_tx_start(t,r,true,now));
 assert(wifi_tx_event(t,WIFI_EVENT_ASSOCIATED,now+1));
 assert(wifi_tx_event(t,WIFI_EVENT_IP_READY,now+2));
 assert(wifi_tx_event(t,WIFI_EVENT_STABILITY_COMPLETE,now+5002));
 assert(wifi_tx_event(t,WIFI_EVENT_PROMOTION_ADMIT,now+5002));
 assert(t->state==WIFI_TX_COMMITTING);
}
static wifi_request_t request(void) {
 wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config.mode=1; r.config.ssid_len=1; r.config.ssid[0]='x'; r.config.secret_len=8; memset(r.config.secret,'q',8); return r;
}
int main(void) {
 baseline_test();
 mock_t m={0}; wifi_tx_t t; wifi_request_t r=request(); assert(wifi_tx_init(&t,store(&m)));
 /* Gate first: disconnect does not revoke a validated in-flight write. */
 begin(&t,&r,0); uint64_t old=t.ticket; uint8_t id[16]; memcpy(id,r.id,16);
 assert(wifi_tx_event(&t,WIFI_EVENT_CARRIER_LOST,5003)); assert(m.pending && t.state==WIFI_TX_COMMITTING);
 wifi_response_t hidden; assert(!wifi_tx_project(&t,4,&hidden)); promote(&m);
 assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,id,0,old,50000)); assert(t.state==WIFI_TX_COMMITTED && t.generation==1);
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,id,0,old,50001));
 r.id[0]=2; begin(&t,&r,50002); assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,55005)); assert(m.pending && t.state==WIFI_TX_COMMITTING);
 assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,t.id,1,t.ticket,55006)); assert(t.state==WIFI_TX_ROLLBACK && !m.pending);
 assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,1,t.ticket,55007)); assert(t.state==WIFI_TX_CANCELLED);
 r.id[0]=3; begin(&t,&r,56000); assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); assert(t.state==WIFI_TX_IDLE && m.generation==1 && !m.pending);
 wifi_response_t x={0}; uint8_t b[296]; size_t z;
 x.op=3; x.u.tx.id[0]=1; x.u.tx.state=1; x.phase=0; assert(!wifi_response_encode(&x,b,sizeof(b),&z)); x.phase=1; assert(wifi_response_encode(&x,b,sizeof(b),&z) && z==28); x.u.tx.state=4; assert(!wifi_response_encode(&x,b,sizeof(b),&z)); x.phase=0; assert(!wifi_response_encode(&x,b,sizeof(b),&z)); x.u.tx.generation=1; assert(wifi_response_encode(&x,b,sizeof(b),&z)); x.u.tx.reason=1; assert(!wifi_response_encode(&x,b,sizeof(b),&z));
 memset(&x,0,sizeof(x)); x.op=1; x.u.status.flags=4; assert(!wifi_response_encode(&x,b,sizeof(b),&z)); x.u.status.flags=1; x.u.status.state=1; x.u.status.generation=1; x.u.status.mode=1; x.u.status.ssid_len=1; x.u.status.ssid[0]=0xff; assert(wifi_response_encode(&x,b,sizeof(b),&z) && z==13 && b[12]==0xff);
 memset(&x,0,sizeof(x)); x.op=2; x.phase=2; assert(wifi_response_encode(&x,b,sizeof(b),&z) && z==8); x.u.scan.count=9; assert(!wifi_response_encode(&x,b,sizeof(b),&z)); x.u.scan.count=1; x.phase=0; x.u.scan.entries[0].ssid_len=33; assert(!wifi_response_encode(&x,b,sizeof(b),&z));
 puts("closure race and response tests passed"); return 0;
}
