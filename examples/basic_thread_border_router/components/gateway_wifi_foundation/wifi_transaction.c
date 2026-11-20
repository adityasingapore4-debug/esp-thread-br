#include "wifi_transaction.h"
#include <limits.h>
#include <string.h>
static bool active(wifi_tx_state_t s) { return s==WIFI_TX_CONNECTING || s==WIFI_TX_VALIDATING_IP || s==WIFI_TX_STABILITY || s==WIFI_TX_VALIDATED || s==WIFI_TX_COMMITTING || s==WIFI_TX_ROLLBACK; }
static void clear_candidate(wifi_tx_t *t) { wifi_zero(&t->candidate,sizeof(t->candidate)); t->store.clear(t->store.ctx); }
static void rollback(wifi_tx_t *t, uint8_t reason) { t->reason=reason; t->state=WIFI_TX_ROLLBACK; clear_candidate(t); if(!t->store.restore(t->store.ctx,&t->previous,t->generation)) { t->reason=7; t->state=WIFI_TX_FAILED; } }
bool wifi_tx_init(wifi_tx_t *t,wifi_store_t s) { if(!t || !s.load || !s.stage || !s.cancel_commit || !s.restore || !s.clear || !s.restart) return false; memset(t,0,sizeof(*t)); t->store=s; t->previous_loaded=s.load(s.ctx,&t->previous,&t->generation); if(!t->previous_loaded) { wifi_zero(&t->previous,sizeof(t->previous)); return false; } return true; }
wifi_start_result_t wifi_tx_start_result(wifi_tx_t *t,const wifi_request_t *r,bool authorized,uint64_t now) {
 if(!t || !r) return WIFI_START_INVALID;
 if(!authorized) return WIFI_START_UNAUTHORIZED;
 if(!t->previous_loaded || t->ticket==UINT64_MAX || now<t->last_ms) return WIFI_START_UNAVAILABLE;
 if(active(t->state)) return WIFI_START_BUSY;
 if(t->generation==UINT32_MAX) return WIFI_START_GENERATION_EXHAUSTED;
 for(size_t i=0;i<t->recent_count;i++) if(memcmp(t->recent_ids[i],r->id,16)==0) return WIFI_START_DUPLICATE;
 uint8_t any=0; for(size_t i=0;i<16;i++) any|=r->id[i];
 if(r->op!=3 || !any || r->config.mode<1 || r->config.mode>2 || !r->config.ssid_len || r->config.ssid_len>32 || r->config.secret_len<8 || r->config.secret_len>63) return WIFI_START_INVALID;
 for(size_t i=0;i<r->config.ssid_len;i++) if(!r->config.ssid[i]) return WIFI_START_INVALID;
 for(size_t i=0;i<r->config.secret_len;i++) if(r->config.secret[i]<0x20 || r->config.secret[i]>0x7e) return WIFI_START_INVALID;
 t->ticket++; memcpy(t->id,r->id,16); memcpy(t->recent_ids[t->recent_next],r->id,16);
 t->recent_next=(uint8_t)((t->recent_next+1u)%8u); if(t->recent_count<8) t->recent_count++;
 t->candidate=r->config; t->carrier_lost=false; t->boundary_unresolved=false; t->deferred_reason=0; t->started_ms=t->last_ms=now; t->reason=0; t->state=WIFI_TX_CONNECTING; return WIFI_START_ACCEPTED;
}
bool wifi_tx_start(wifi_tx_t *t,const wifi_request_t *r,bool a,uint64_t now) { return wifi_tx_start_result(t,r,a,now)==WIFI_START_ACCEPTED; }
static bool verified_commit(wifi_tx_t *t) {
 wifi_config_t c={0}; uint32_t g=0; bool ok=t->store.load(t->store.ctx,&c,&g);
 ok=ok && g==t->generation+1u && c.mode==t->candidate.mode && c.ssid_len==t->candidate.ssid_len && c.secret_len==t->candidate.secret_len && memcmp(c.ssid,t->candidate.ssid,c.ssid_len)==0 && memcmp(c.secret,t->candidate.secret,c.secret_len)==0;
 wifi_zero(&c,sizeof(c)); if(!ok) return false;
 t->generation++; t->previous=t->candidate; t->state=WIFI_TX_COMMITTED; clear_candidate(t); return true;
}

static bool event(wifi_tx_t *t,wifi_event_t e,uint64_t now) {
 if(!t) return false;
 if(e==WIFI_EVENT_REBOOT) { if(!t->store.restart(t->store.ctx)) return false; t->last_ms=now; clear_candidate(t); wifi_zero(&t->previous,sizeof(t->previous)); t->state=WIFI_TX_IDLE; wifi_zero(t->recent_ids,sizeof(t->recent_ids)); t->recent_count=0; t->recent_next=0; t->carrier_lost=false; t->boundary_unresolved=false; wifi_zero(t->id,sizeof(t->id)); t->previous_loaded=t->store.load(t->store.ctx,&t->previous,&t->generation); return t->previous_loaded; }
 if(now<t->last_ms) return false;
 t->last_ms=now;
 if(e==WIFI_EVENT_CARRIER_LOST) { t->carrier_lost=true; wifi_zero(t->recent_ids,sizeof(t->recent_ids)); t->recent_count=0; t->recent_next=0; }
 if(!active(t->state)) { if(e==WIFI_EVENT_CARRIER_LOST) wifi_zero(t->id,sizeof(t->id)); return (e==WIFI_EVENT_CANCEL && t->state==WIFI_TX_COMMITTED) || e==WIFI_EVENT_CARRIER_LOST; }
 if(t->state==WIFI_TX_ROLLBACK) { if(e==WIFI_EVENT_ROLLBACK_SUCCESS) { t->state=t->reason==4?WIFI_TX_CANCELLED:WIFI_TX_FAILED; return true; } if(e==WIFI_EVENT_ROLLBACK_FAILED) { t->reason=7; t->state=WIFI_TX_FAILED; return true; } return false; }
 if(t->state==WIFI_TX_COMMITTING) {
  if(e==WIFI_EVENT_CARRIER_LOST || e==WIFI_EVENT_CANCEL) {
   if(!t->deferred_reason) t->deferred_reason=e==WIFI_EVENT_CANCEL?4:5;
   return true; /* gate already won; do not cancel the irreversible write */
  }
  if(e==WIFI_EVENT_COMMIT_SUCCESS) return verified_commit(t);
  if(e==WIFI_EVENT_COMMIT_FAILED) {
   wifi_cancel_result_t cr=t->store.cancel_commit(t->store.ctx);
   if(cr==WIFI_CANCEL_ABORTED) { rollback(t,t->deferred_reason?t->deferred_reason:6); return true; }
   if(cr==WIFI_CANCEL_DURABLE) return verified_commit(t);
   t->boundary_unresolved=true; return false;
  }
  return false; /* validation deadline no longer governs admitted storage */
 }
 if(t->state!=WIFI_TX_VALIDATED && (e==WIFI_EVENT_TIMEOUT || now-t->started_ms>=45000u)) { rollback(t,t->state==WIFI_TX_VALIDATING_IP?2:1); return true; }
 if(e==WIFI_EVENT_CANCEL || e==WIFI_EVENT_CARRIER_LOST || e==WIFI_EVENT_LINK_LOST) { rollback(t,e==WIFI_EVENT_CANCEL?4:e==WIFI_EVENT_CARRIER_LOST?5:3); return true; }
 if(e==WIFI_EVENT_ASSOCIATED && t->state==WIFI_TX_CONNECTING) { t->state=WIFI_TX_VALIDATING_IP; return true; }
 if(e==WIFI_EVENT_IP_READY && t->state==WIFI_TX_VALIDATING_IP) { t->state=WIFI_TX_STABILITY; t->stable_ms=now; return true; }
 if(e==WIFI_EVENT_STABILITY_COMPLETE && t->state==WIFI_TX_STABILITY && now-t->stable_ms>=5000u) { t->state=WIFI_TX_VALIDATED; return true; }
 if(e==WIFI_EVENT_PROMOTION_ADMIT && t->state==WIFI_TX_VALIDATED) {
  t->state=WIFI_TX_COMMITTING;
  if(!t->store.stage(t->store.ctx,&t->candidate,t->generation+1u,t->id,t->ticket)) return event(t,WIFI_EVENT_COMMIT_FAILED,now);
  return true;
 }
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

bool wifi_tx_project(const wifi_tx_t *t,uint8_t op,wifi_response_t *r) {
 if(!t || !r || t->carrier_lost || op<3 || op>5 || t->state==WIFI_TX_IDLE) return false;
 memset(r,0,sizeof(*r)); r->op=op; memcpy(r->u.tx.id,t->id,16); r->u.tx.reason=t->reason; r->u.tx.generation=t->generation;
 switch(t->state) {
 case WIFI_TX_CONNECTING: r->u.tx.state=1; break;
 case WIFI_TX_VALIDATING_IP: r->u.tx.state=2; break;
 case WIFI_TX_STABILITY: case WIFI_TX_VALIDATED: case WIFI_TX_COMMITTING: r->u.tx.state=3; break;
 case WIFI_TX_COMMITTED: r->u.tx.state=4; break;
 case WIFI_TX_FAILED: r->u.tx.state=5; break;
 case WIFI_TX_CANCELLED: r->u.tx.state=6; break;
 case WIFI_TX_ROLLBACK: r->u.tx.state=7; break;
 default: return false;
 }
 r->phase=(r->u.tx.state>=4 && r->u.tx.state<=6)?0:1; return true;
}
