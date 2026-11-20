#include "wifi_transaction.h"
#include <limits.h>
#include <string.h>
static bool active(wifi_tx_state_t s) { return s==WIFI_TX_CONNECTING || s==WIFI_TX_VALIDATING_IP || s==WIFI_TX_STABILITY || s==WIFI_TX_COMMITTING || s==WIFI_TX_ROLLBACK; }
static void clear_candidate(wifi_tx_t *t) { wifi_zero(&t->candidate,sizeof(t->candidate)); t->store.clear(t->store.ctx); }
static void rollback(wifi_tx_t *t, uint8_t reason) { t->reason=reason; t->state=WIFI_TX_ROLLBACK; clear_candidate(t); if(!t->store.restore(t->store.ctx,&t->previous,t->generation)) { t->reason=7; t->state=WIFI_TX_FAILED; } }
bool wifi_tx_init(wifi_tx_t *t,wifi_store_t s) { if(!t || !s.load || !s.stage || !s.cancel_commit || !s.restore || !s.clear) return false; memset(t,0,sizeof(*t)); t->store=s; t->previous_loaded=s.load(s.ctx,&t->previous,&t->generation); if(!t->previous_loaded) { wifi_zero(&t->previous,sizeof(t->previous)); return false; } return true; }
bool wifi_tx_start(wifi_tx_t *t,const wifi_request_t *r,bool authorized,uint64_t now) { if(!t || !r || !authorized || !t->previous_loaded || r->op!=3 || active(t->state) || t->generation==UINT32_MAX || t->recent_count==8 || t->ticket==UINT64_MAX || now<t->last_ms) return false; for(size_t i=0;i<t->recent_count;i++) if(memcmp(t->recent_ids[i],r->id,16)==0) return false; uint8_t any=0; for(size_t i=0;i<16;i++) any|=r->id[i]; if(!any || r->config.mode<1 || r->config.mode>2 || !r->config.ssid_len || r->config.ssid_len>32 || r->config.secret_len<8 || r->config.secret_len>63) return false; for(size_t i=0;i<r->config.ssid_len;i++) if(!r->config.ssid[i]) return false; for(size_t i=0;i<r->config.secret_len;i++) if(r->config.secret[i]<0x20 || r->config.secret[i]>0x7e) return false; t->ticket++; memcpy(t->id,r->id,16); memcpy(t->recent_ids[t->recent_count++],r->id,16); t->candidate=r->config; t->started_ms=t->last_ms=now; t->reason=0; t->state=WIFI_TX_CONNECTING; return true; }
static bool event(wifi_tx_t *t,wifi_event_t e,uint64_t now) {
 if(!t || now<t->last_ms) return false;
 t->last_ms=now;
 if(e==WIFI_EVENT_REBOOT) { clear_candidate(t); wifi_zero(&t->previous,sizeof(t->previous)); t->state=WIFI_TX_IDLE; wifi_zero(t->recent_ids,sizeof(t->recent_ids)); t->recent_count=0; t->previous_loaded=t->store.load(t->store.ctx,&t->previous,&t->generation); return t->previous_loaded; }
 if(e==WIFI_EVENT_CARRIER_LOST) { wifi_zero(t->recent_ids,sizeof(t->recent_ids)); t->recent_count=0; }
 if(!active(t->state)) { if(e==WIFI_EVENT_CARRIER_LOST) wifi_zero(t->id,sizeof(t->id)); return (e==WIFI_EVENT_CANCEL && t->state==WIFI_TX_COMMITTED) || e==WIFI_EVENT_CARRIER_LOST; }
 if(t->state==WIFI_TX_ROLLBACK) { if(e==WIFI_EVENT_ROLLBACK_SUCCESS) { t->state=t->reason==4?WIFI_TX_CANCELLED:WIFI_TX_FAILED; return true; } if(e==WIFI_EVENT_ROLLBACK_FAILED) { t->reason=7; t->state=WIFI_TX_FAILED; return true; } return false; }
 if(e==WIFI_EVENT_COMMIT_SUCCESS && t->state==WIFI_TX_COMMITTING) { t->generation++; t->previous=t->candidate; t->state=WIFI_TX_COMMITTED; clear_candidate(t); return true; }
 if(t->state==WIFI_TX_COMMITTING && (e==WIFI_EVENT_CANCEL || e==WIFI_EVENT_CARRIER_LOST || e==WIFI_EVENT_LINK_LOST || e==WIFI_EVENT_TIMEOUT || now-t->started_ms>=45000u)) { wifi_cancel_result_t cr=t->store.cancel_commit(t->store.ctx); if(cr!=WIFI_CANCEL_ABORTED && cr!=WIFI_CANCEL_DURABLE) return false; if(cr==WIFI_CANCEL_DURABLE) { t->generation++; t->previous=t->candidate; t->state=WIFI_TX_COMMITTED; clear_candidate(t); if(e==WIFI_EVENT_CARRIER_LOST) wifi_zero(t->id,sizeof(t->id)); return true; } rollback(t,e==WIFI_EVENT_CANCEL?4:e==WIFI_EVENT_CARRIER_LOST?5:e==WIFI_EVENT_LINK_LOST?3:1); return true; }
 if(e==WIFI_EVENT_TIMEOUT || now-t->started_ms>=45000u) { rollback(t,t->state==WIFI_TX_VALIDATING_IP?2:1); return true; }
 if(e==WIFI_EVENT_CANCEL || e==WIFI_EVENT_CARRIER_LOST || e==WIFI_EVENT_LINK_LOST || (e==WIFI_EVENT_COMMIT_FAILED && t->state==WIFI_TX_COMMITTING)) { rollback(t,e==WIFI_EVENT_CANCEL?4:e==WIFI_EVENT_CARRIER_LOST?5:e==WIFI_EVENT_LINK_LOST?3:6); return true; }
 if(e==WIFI_EVENT_ASSOCIATED && t->state==WIFI_TX_CONNECTING) { t->state=WIFI_TX_VALIDATING_IP; return true; }
 if(e==WIFI_EVENT_IP_READY && t->state==WIFI_TX_VALIDATING_IP) { t->state=WIFI_TX_STABILITY; t->stable_ms=now; return true; }
 if(e==WIFI_EVENT_STABILITY_COMPLETE && t->state==WIFI_TX_STABILITY && now-t->stable_ms>=5000u) { t->state=WIFI_TX_COMMITTING; if(!t->store.stage(t->store.ctx,&t->candidate,t->generation+1u)) rollback(t,6); return true; }
 return false;
}

bool wifi_tx_event(wifi_tx_t *t, wifi_event_t e, uint64_t now) {
 if(e==WIFI_EVENT_COMMIT_SUCCESS || e==WIFI_EVENT_COMMIT_FAILED || e==WIFI_EVENT_ROLLBACK_SUCCESS || e==WIFI_EVENT_ROLLBACK_FAILED) return false;
 return event(t,e,now);
}
bool wifi_tx_complete(wifi_tx_t *t, wifi_event_t e, const uint8_t id[16], uint32_t prior_generation, uint64_t ticket, uint64_t now) {
 if(!t || !id || memcmp(t->id,id,16)!=0 || t->generation!=prior_generation || t->ticket!=ticket) return false;
 if(e!=WIFI_EVENT_COMMIT_SUCCESS && e!=WIFI_EVENT_COMMIT_FAILED && e!=WIFI_EVENT_ROLLBACK_SUCCESS && e!=WIFI_EVENT_ROLLBACK_FAILED) return false;
 return event(t,e,now);
}
