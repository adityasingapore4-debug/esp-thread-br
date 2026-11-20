#define main baseline_test
#include "test_wifi.c"
#undef main
static void begin(wifi_tx_t *t,wifi_request_t *r,uint64_t n) { assert(wifi_tx_start(t,r,true,n)); assert(wifi_tx_event(t,WIFI_EVENT_ASSOCIATED,n+1)); assert(wifi_tx_event(t,WIFI_EVENT_IP_READY,n+2)); assert(wifi_tx_event(t,WIFI_EVENT_STABILITY_COMPLETE,n+5002)); }
static wifi_request_t request(void) { wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config.mode=1; r.config.ssid_len=1; r.config.ssid[0]='x'; r.config.secret_len=8; memset(r.config.secret,'q',8); return r; }
int main(void) {
 baseline_test();
 mock_t m={0}; wifi_tx_t t; wifi_request_t r=request(); assert(wifi_tx_init(&t,store(&m)));
 assert(wifi_tx_start_result(&t,&r,false,0)==WIFI_START_UNAUTHORIZED);
 begin(&t,&r,0); wifi_response_t w; assert(wifi_tx_project(&t,4,&w)); assert(w.phase==1 && w.u.tx.state==3);
 assert(wifi_tx_start_result(&t,&r,true,5003)==WIFI_START_BUSY);
 uint64_t ticket=t.ticket; uint8_t id[16]; memcpy(id,t.id,16);
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,id,0,ticket,5003)); assert(t.state==WIFI_TX_COMMITTING && t.generation==0 && m.pending);
 m.pending=false; assert(!wifi_tx_event(&t,WIFI_EVENT_CANCEL,5003)); assert(t.state==WIFI_TX_COMMITTING && t.generation==0); m.pending=true;
 m.cancel_unknown=true; assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,id,0,ticket,5004)); assert(t.state==WIFI_TX_COMMITTING && m.pending);
 m.cancel_unknown=false; assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,id,0,ticket,5005)); assert(t.state==WIFI_TX_ROLLBACK && !m.pending && m.generation==0);
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,id,0,ticket,5006)); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,id,0,ticket,5007));
 assert(wifi_tx_start_result(&t,&r,true,5008)==WIFI_START_DUPLICATE);
 r.id[0]=2; begin(&t,&r,6000); promote(&m); assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,t.id,0,t.ticket,11003)); assert(t.state==WIFI_TX_COMMITTED && t.generation==1);
 r.id[0]=3; begin(&t,&r,12000); assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); assert(t.state==WIFI_TX_IDLE && t.generation==1 && !m.pending && t.last_ms==0);
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,r.id,1,t.ticket,1));
 for(unsigned i=0;i<10;i++) { r.id[0]=(uint8_t)(10+i); assert(wifi_tx_start(&t,&r,true,10+i*2)); assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,11+i*2)); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,t.generation,t.ticket,11+i*2)); }
 assert(wifi_tx_start_result(&t,&r,true,40)==WIFI_START_DUPLICATE); r.id[0]=10; assert(wifi_tx_start(&t,&r,true,41));
 wifi_response_t x={0}; uint8_t b[64]; size_t n=0; x.op=3; x.u.tx.id[0]=1; x.u.tx.state=4; x.u.tx.generation=0; assert(!wifi_response_encode(&x,b,sizeof(b),&n)); x.u.tx.generation=1; x.u.tx.reason=4; assert(!wifi_response_encode(&x,b,sizeof(b),&n)); x.u.tx.reason=0; x.phase=1; assert(!wifi_response_encode(&x,b,sizeof(b),&n)); x.phase=0; assert(wifi_response_encode(&x,b,sizeof(b),&n) && n==28); x.u.tx.state=6; x.phase=1; assert(!wifi_response_encode(&x,b,sizeof(b),&n));
 puts("independent review regression tests passed"); return 0;
}
