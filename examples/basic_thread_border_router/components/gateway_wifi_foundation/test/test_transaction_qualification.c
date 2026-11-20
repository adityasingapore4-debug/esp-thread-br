#define main baseline_test
#include "test_wifi.c"
#undef main
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static wifi_request_t request(unsigned id) { wifi_request_t r={0}; r.op=3; r.id[0]=(uint8_t)id; r.config.mode=1; r.config.ssid_len=1; r.config.ssid[0]='s'; r.config.secret_len=8; memset(r.config.secret,'z',8); return r; }
static bool empty(const void *p,size_t n) { const uint8_t *b=p; for(size_t i=0;i<n;i++) if(b[i]) return false; return true; }
int main(void) {
 mock_t m={0}; wifi_tx_t t; CHECK(wifi_tx_init(&t,store(&m))); wifi_request_t r;
 for(unsigned i=1;i<=10;i++) { r=request(i); CHECK(wifi_tx_start(&t,&r,true,i*2)); CHECK(wifi_tx_event(&t,WIFI_EVENT_CANCEL,i*2+1)); CHECK(empty(&t.candidate,sizeof t.candidate)); CHECK(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,t.id,t.generation,t.ticket,i*2+1)); }
 r=request(10); CHECK(wifi_tx_start_result(&t,&r,true,23)==WIFI_START_DUPLICATE); r=request(1); CHECK(wifi_tx_start(&t,&r,true,24));
 uint8_t id[16]; memcpy(id,t.id,16); uint64_t ticket=t.ticket; m.fail_restore=true; CHECK(wifi_tx_event(&t,WIFI_EVENT_CANCEL,25)); CHECK(t.state==WIFI_TX_FAILED && t.reason==7); CHECK(empty(&t.candidate,sizeof t.candidate)); CHECK(!wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,id,t.generation,ticket,26)); m.fail_restore=false;
 r=request(12); CHECK(wifi_tx_start(&t,&r,true,27)); CHECK(wifi_tx_event(&t,WIFI_EVENT_ASSOCIATED,28)); CHECK(wifi_tx_event(&t,WIFI_EVENT_IP_READY,29)); CHECK(wifi_tx_event(&t,WIFI_EVENT_STABILITY_COMPLETE,5029)); CHECK(wifi_tx_event(&t,WIFI_EVENT_PROMOTION_ADMIT,5030));
 memcpy(id,t.id,16); ticket=t.ticket; m.pending=false; CHECK(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,id,t.generation,ticket,5031)); CHECK(t.state==WIFI_TX_COMMITTING && t.generation==0); m.pending=true;
 CHECK(wifi_tx_complete(&t,WIFI_EVENT_COMMIT_FAILED,id,t.generation,ticket,5032)); CHECK(empty(&t.candidate,sizeof t.candidate)); CHECK(!wifi_tx_complete(&t,WIFI_EVENT_COMMIT_SUCCESS,id,t.generation,ticket,5033));
 CHECK(wifi_tx_complete(&t,WIFI_EVENT_ROLLBACK_SUCCESS,id,t.generation,ticket,5034)); CHECK(wifi_tx_event(&t,WIFI_EVENT_REBOOT,0)); CHECK(empty(&t.candidate,sizeof t.candidate));
 printf("transaction qualification: %u checks passed\n",checks); return 0;
}
