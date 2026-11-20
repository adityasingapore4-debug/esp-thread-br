#include "wifi_codec.h"
#include "wifi_transaction.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef struct { wifi_config_t committed, staged; uint32_t generation, staged_generation; bool fail_stage, fail_restore, pending, cancel_unknown; } mock_t;
static bool load(void *p,wifi_config_t *c,uint32_t *g) { mock_t *m=p; *c=m->committed; *g=m->generation; return true; }
static bool stage(void *p,const wifi_config_t *c,uint32_t g) { mock_t *m=p; if(m->fail_stage) { m->pending=true; return false; } m->staged=*c; m->staged_generation=g; m->pending=true; return true; }
static wifi_cancel_result_t cancel(void *p) { mock_t *m=p; if(m->cancel_unknown) return WIFI_CANCEL_UNKNOWN; if(!m->pending) return WIFI_CANCEL_DURABLE; m->pending=false; return WIFI_CANCEL_ABORTED; }
static bool restore(void *p,const wifi_config_t *c,uint32_t g) { mock_t *m=p; (void)c; (void)g; return !m->fail_restore; }
static void clear(void *p) { mock_t *m=p; wifi_zero(&m->staged,sizeof(m->staged)); m->pending=false; }
static bool restart(void *p) { mock_t *m=p; m->pending=false; return true; }
static wifi_store_t store(mock_t *m) { wifi_store_t s={m,load,stage,cancel,restore,clear,restart}; return s; }
static void promote(mock_t *m) { assert(m->pending); m->committed=m->staged; m->generation=m->staged_generation; m->pending=false; }
int main(void) {
 uint8_t v[]={1,3,0,0,0,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,1,4,8,0,'T','E','S','T','p','a','s','s','w','o','r','d'};
 wifi_request_t r; assert(wifi_request_decode(v,sizeof(v),&r)); assert(r.config.ssid_len==4 && r.config.secret_len==8);
 for(size_t i=0;i<sizeof(v);i++) { assert(!wifi_request_decode(v,i,&r)); } assert(wifi_request_decode(v,sizeof(v),&r));
 uint8_t bad[120]; memcpy(bad,v,sizeof(v)); bad[0]=2; assert(!wifi_request_decode(bad,sizeof(v),&r)); memcpy(bad,v,sizeof(v)); bad[23]=1; assert(!wifi_request_decode(bad,sizeof(v),&r)); memcpy(bad,v,sizeof(v)); bad[20]=3; assert(!wifi_request_decode(bad,sizeof(v),&r)); memcpy(bad,v,sizeof(v)); bad[28]=0; assert(!wifi_request_decode(bad,sizeof(v),&r)); memcpy(bad,v,sizeof(v)); bad[36]=0; assert(!wifi_request_decode(bad,37,&r));
 uint8_t max[119]={1,3}; max[4]=1; max[20]=1; max[21]=32; max[22]=63; memset(max+24,'a',95); assert(wifi_request_decode(max,sizeof(max),&r));
 uint8_t small[20]={1,1}; for(unsigned op=1;op<=5;op++) { small[1]=(uint8_t)op; small[4]=1; if(op!=3) assert(wifi_request_decode(small,op<=2?4:20,&r)); }
 wifi_response_t resp={0}; uint8_t out[296]; size_t n=0; resp.op=2; resp.u.scan.count=8; for(size_t i=0;i<8;i++) resp.u.scan.entries[i].ssid_len=32; assert(wifi_response_encode(&resp,out,sizeof(out),&n) && n==296); assert(!wifi_response_encode(&resp,out,295,&n)); resp.phase=1; assert(!wifi_response_encode(&resp,out,sizeof(out),&n));
 assert(wifi_request_decode(v,sizeof(v),&r)); mock_t m={0}; wifi_tx_t t; assert(wifi_tx_init(&t,store(&m))); assert(!wifi_tx_start(&t,&r,false,0)); assert(wifi_tx_start(&t,&r,true,0)); assert(!wifi_tx_start(&t,&r,true,0)); assert(wifi_tx_event(&t,WIFI_EVENT_ASSOCIATED,1)); assert(wifi_tx_event(&t,WIFI_EVENT_IP_READY,2)); assert(!wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,5001)); assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,5002)); promote(&m); assert(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,t.id,t.generation,t.ticket,5003)); assert(t.state==WIFI_TX_COMMITTED && t.generation==1); assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,5004)); assert(!wifi_tx_start(&t,&r,true,5005));
 r.id[0]=1; assert(wifi_tx_start(&t,&r,true,6000)); assert(wifi_tx_event(&t,WIFI_EVENT_CANCEL,6001)); assert(t.state==WIFI_TX_ROLLBACK); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,t.generation,t.ticket,6002)); assert(t.state==WIFI_TX_CANCELLED); assert(m.generation==1);
 r.id[0]=2; assert(wifi_tx_start(&t,&r,true,7000)); assert(wifi_tx_event(&t,WIFI_EVENT_ASSOCIATED,7001)); assert(wifi_tx_event(&t,WIFI_EVENT_TIMEOUT,52000)); assert(t.reason==2); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,t.generation,t.ticket,52001));
 r.id[0]=3; assert(wifi_tx_start(&t,&r,true,53000)); assert(wifi_tx_event(&t,WIFI_EVENT_ASSOCIATED,53001)); assert(wifi_tx_event(&t,WIFI_EVENT_IP_READY,53002)); m.fail_stage=true; assert(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,58002)); assert(t.reason==6 && t.state==WIFI_TX_ROLLBACK); assert(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_FAILED,t.id,t.generation,t.ticket,58003)); assert(t.reason==7);
 assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,58004)); assert(t.generation==1 && t.state==WIFI_TX_IDLE); m.generation=UINT32_MAX; assert(wifi_tx_event(&t,WIFI_EVENT_REBOOT,58005)); assert(!wifi_tx_start(&t,&r,true,58006));
 wifi_zero(&r,sizeof(r)); for(size_t i=0;i<sizeof(r);i++) assert(((uint8_t *)&r)[i]==0);
 puts("gateway wifi foundation tests passed"); return 0;
}
