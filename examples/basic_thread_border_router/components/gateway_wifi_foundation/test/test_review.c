#define main baseline_test
#include "test_wifi.c"
#undef main
static wifi_request_t request(uint8_t id) { wifi_request_t r={0}; r.op=3; r.id[0]=id; r.config.mode=1; r.config.ssid_len=1; r.config.ssid[0]='x'; r.config.secret_len=8; memset(r.config.secret,'q',8); return r; }
static void validate(wifi_tx_t *t,wifi_request_t *r,uint64_t n,uint64_t stable) { assert(wifi_tx_start(t,r,true,n)); assert(wifi_tx_event(t,WIFI_EVENT_ASSOCIATED,n+1)); assert(wifi_tx_event(t,WIFI_EVENT_IP_READY,n+stable)); }
static bool zeroed(const void *p,size_t n) { const uint8_t *b=p; for(size_t i=0;i<n;i++) if(b[i]) return false; return true; }
int main(void) {
 baseline_test();
 mock_t m={0}; wifi_tx_t t; wifi_request_t r=request(1); assert(wifi_tx_init(&t,store(&m)));
 /* Strict validation cutoff: 44,999 succeeds, 45,000 fails. */
 validate(&t,&r,0,39999); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,44999)); assert(t.state==WIFI_TX_VALIDATED && !m.pending);
 assert(wifi_tx_event(&t,WIFI_EVENT_PROMOTION_ADMIT,45001)); assert(m.pending); promote(&m);
 assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,t.id,0,t.ticket,80000)); assert(t.state==WIFI_TX_COMMITTED && t.generation==1 && zeroed(&t.candidate,sizeof(t.candidate)));
 r=request(2); validate(&t,&r,81000,40000); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,126000)); assert(t.state==WIFI_TX_ROLLBACK && !m.pending);
 assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,1,t.ticket,126001));
 r=request(3); validate(&t,&r,127000,40001); assert(!wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,171999)); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,172000)); assert(t.state==WIFI_TX_ROLLBACK);
 assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,1,t.ticket,172001));
 /* A loss before the gate prevents staging; no late promotion can occur. */
 r=request(4); validate(&t,&r,173000,2); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,178002)); assert(wifi_tx_event(&t,WIFI_EVENT_CARRIER_LOST,178003)); assert(t.state==WIFI_TX_ROLLBACK && !m.pending && zeroed(&t.candidate,sizeof(t.candidate)));
 assert(!wifi_tx_event(&t,WIFI_EVENT_PROMOTION_ADMIT,178004)); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,1,t.ticket,178005));
 /* Cancel before gate also wins. */
 r=request(5); validate(&t,&r,179000,2); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,184002)); assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,184003)); assert(!m.pending && t.state==WIFI_TX_ROLLBACK);
 assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,1,t.ticket,184004));
 /* False success, unknown backend and recovery retain exclusive ownership. */
 r=request(6); validate(&t,&r,185000,2); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,190002)); assert(wifi_tx_event(&t,WIFI_EVENT_PROMOTION_ADMIT,190003)); assert(memcmp(m.staged_id,t.id,16)==0 && m.staged_ticket==t.ticket && m.staged_generation==2); uint8_t oldid[16]; memcpy(oldid,t.id,16); uint64_t oldticket=t.ticket;
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,oldid,1,oldticket+1,190004)); assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,oldid,1,oldticket,190004)); assert(t.state==WIFI_TX_COMMITTING); m.pending=false; assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,oldid,1,oldticket,190004)); assert(t.generation==1 && t.state==WIFI_TX_COMMITTING); m.pending=true;
 assert(wifi_tx_event(&t,WIFI_EVENT_CARRIER_LOST,190005)); wifi_response_t response; assert(!wifi_tx_project(&t,4,&response)); m.cancel_unknown=true;
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,oldid,1,oldticket,190006)); assert(t.boundary_unresolved && t.state==WIFI_TX_COMMITTING);
 r=request(7); assert(wifi_tx_start_result(&t,&r,true,190007)==WIFI_START_BUSY); m.restart_fail=true; assert(!wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); assert(m.pending && t.state==WIFI_TX_COMMITTING);
 m.restart_fail=false; assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); assert(t.state==WIFI_TX_IDLE && t.generation==1 && zeroed(&t.candidate,sizeof(t.candidate)) && !m.pending);
 assert(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,oldid,1,oldticket,1));
 /* A validated but ungated candidate may wait without a new validation timeout. */
 validate(&t,&r,2,2); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,5004)); assert(wifi_tx_event(&t,WIFI_EVENT_PROMOTION_ADMIT,60000));
 assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,60001)); promote(&m); assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,t.id,1,t.ticket,90000)); assert(t.generation==2);
 assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); m.generation=UINT32_MAX; assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); r=request(8); assert(wifi_tx_start_result(&t,&r,true,1)==WIFI_START_GENERATION_EXHAUSTED && !m.pending);
 uint8_t p[40]={1,3,0,0,1}; p[20]=1; p[21]=1; p[22]=8; p[24]='x'; memset(p+25,'z',8); wifi_request_t z; assert(wifi_request_decode(p,33,&z));
 p[0]=2; assert(!wifi_request_decode(p,33,&z)); p[0]=1; p[1]=6; assert(!wifi_request_decode(p,33,&z)); p[1]=3; p[2]=1; assert(!wifi_request_decode(p,33,&z)); p[2]=0; p[23]=1; assert(!wifi_request_decode(p,33,&z)); p[23]=0; assert(!wifi_request_decode(p,32,&z)); assert(!wifi_request_decode(p,34,&z)); p[21]=0; assert(!wifi_request_decode(p,33,&z)); p[21]=1; p[22]=7; assert(!wifi_request_decode(p,33,&z));
 puts("addendum ordering regression tests passed"); return 0;
}
