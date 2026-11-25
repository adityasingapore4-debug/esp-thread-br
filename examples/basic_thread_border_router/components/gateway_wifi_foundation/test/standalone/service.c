#include "service.h"
#include <string.h>

static uint64_t now(offline_wifi_service_t *s) { return s->hooks.now_ms(s->hooks.ctx); }
static bool active(wifi_tx_state_t state) {
    return state==WIFI_TX_CONNECTING || state==WIFI_TX_VALIDATING_IP ||
        state==WIFI_TX_STABILITY || state==WIFI_TX_VALIDATED ||
        state==WIFI_TX_COMMITTING || state==WIFI_TX_ROLLBACK;
}
bool offline_wifi_init(offline_wifi_service_t *s, wifi_store_io_t io, uint64_t epoch,
                       wifi_recovery_policy_t policy, wifi_recovery_hooks_t hooks) {
    if (!s || !hooks.now_ms) return false;
    memset(s, 0, sizeof(*s)); s->hooks=hooks;
    return wifi_storage_init(&s->storage,io) &&
        wifi_recovery_init(&s->recovery,&s->storage,epoch,policy,hooks) &&
        wifi_tx_init(&s->tx,wifi_recovery_interface(&s->recovery));
}
bool offline_wifi_status(offline_wifi_service_t *s, uint8_t *out, size_t cap, size_t *len) {
    if (!s || !s->tx.previous_loaded || s->recovery.restarting || s->recovery.restart_required) return false;
    wifi_response_t r={0}; r.op=1; r.u.status.generation=s->tx.generation;
    if (s->tx.generation) {
        r.u.status.flags=1; r.u.status.mode=s->tx.previous.mode;
        r.u.status.ssid_len=s->tx.previous.ssid_len;
        memcpy(r.u.status.ssid,s->tx.previous.ssid,r.u.status.ssid_len);
        r.u.status.state=1; /* No actual radio association is claimed. */
    }
    if (s->tx.state==WIFI_TX_ROLLBACK) r.u.status.state=6;
    else if (active(s->tx.state)) r.u.status.state=2;
    return wifi_response_encode(&r,out,cap,len);
}
bool offline_wifi_scan_begin(offline_wifi_service_t *s) {
    if (!s || s->scanning || active(s->tx.state) || s->recovery.restarting || s->recovery.restart_required) return false;
    s->scan_started=now(s); s->scanning=true; s->scan_ready=false; memset(&s->scan,0,sizeof(s->scan)); s->scan.op=2; s->scan.phase=1; return true;
}
bool offline_wifi_scan_complete(offline_wifi_service_t *s, const wifi_scan_entry_t *entries, size_t count, bool truncated) {
    if (!s || !s->scanning || count>8 || (count && !entries)) return false;
    uint64_t t=now(s); if (t<s->scan_started || t-s->scan_started>=10000) { s->scanning=false; return false; }
    wifi_response_t r={0}; r.op=2; r.phase=count?0:2; r.u.scan.count=(uint8_t)count; r.u.scan.truncated=truncated?1:0;
    if (count) memcpy(r.u.scan.entries,entries,count*sizeof(*entries));
    uint8_t encoded[296]; size_t len;
    if (!wifi_response_encode(&r,encoded,sizeof(encoded),&len)) return false;
    s->scan=r; s->scanning=false; s->scan_ready=true; s->scan_completed=t; return true;
}
bool offline_wifi_scan_response(offline_wifi_service_t *s, uint8_t *out, size_t cap, size_t *len) {
    if (!s) return false;
    uint64_t t=now(s);
    if (s->scanning && (t<s->scan_started || t-s->scan_started>=10000)) { s->scanning=false; return false; }
    if (s->scan_ready && (t<s->scan_completed || t-s->scan_completed>=30000)) s->scan_ready=false;
    return (s->scanning || s->scan_ready) && wifi_response_encode(&s->scan,out,cap,len);
}
wifi_start_result_t offline_wifi_candidate(offline_wifi_service_t *s, const uint8_t *payload, size_t len) {
    wifi_request_t request={0}; wifi_start_result_t result=WIFI_START_INVALID;
    if (!s || !wifi_request_decode(payload,len,&request) || request.op!=3) goto done;
    if (s->scanning) { result=WIFI_START_BUSY; goto done; }
    /* Synthetic fixture input only. No claim of authorization or ingress admission. */
    result=wifi_recovery_start_result(&s->recovery,&s->tx,&request,true);
done: wifi_zero(&request,sizeof(request)); return result;
}
bool offline_wifi_event(offline_wifi_service_t *s, wifi_event_t e) {
    if (!s || e==WIFI_EVENT_REBOOT || e==WIFI_EVENT_COMMIT_SUCCESS || e==WIFI_EVENT_COMMIT_FAILED || e==WIFI_EVENT_ROLLBACK_SUCCESS || e==WIFI_EVENT_ROLLBACK_FAILED) return false;
    return wifi_tx_event(&s->tx,e,now(s));
}
bool offline_wifi_transaction(offline_wifi_service_t *s, uint8_t *out, size_t cap, size_t *len) {
    wifi_response_t r; return s && wifi_tx_project(&s->tx,4,&r) && wifi_response_encode(&r,out,cap,len);
}
bool offline_wifi_rollback_token(offline_wifi_service_t *s, offline_rollback_token_t *token) {
    if (!s || !token || s->tx.state!=WIFI_TX_ROLLBACK) return false;
    memcpy(token->id,s->tx.id,sizeof(token->id)); token->generation=s->tx.generation;
    token->ticket=s->tx.ticket; token->epoch=wifi_recovery_status(&s->recovery).epoch;
    return true;
}
bool offline_wifi_rollback_complete(offline_wifi_service_t *s, const offline_rollback_token_t *token, bool success) {
    return s && token && token->epoch==wifi_recovery_status(&s->recovery).epoch &&
        wifi_tx_complete(&s->tx,success?WIFI_EVENT_ROLLBACK_SUCCESS:WIFI_EVENT_ROLLBACK_FAILED,
                         token->id,token->generation,token->ticket,now(s));
}
bool offline_wifi_work(offline_wifi_service_t *s) {
    wifi_storage_job_t job; return s && wifi_recovery_job(&s->recovery,&job) && wifi_recovery_worker_run(&s->recovery,&job);
}
void offline_wifi_poll(offline_wifi_service_t *s) {
    if (!s) return;
    wifi_recovery_poll(&s->recovery);
    wifi_storage_completion_t result;
    if (wifi_recovery_completion(&s->recovery,&result)) (void)wifi_recovery_deliver(&s->recovery,&s->tx,&result);
}
void offline_wifi_restart_request(offline_wifi_service_t *s) { if (s) { s->scanning=false; s->scan_ready=false; wifi_recovery_request_restart(&s->recovery); } }
bool offline_wifi_restart_finish(offline_wifi_service_t *s) {
    return s && wifi_recovery_status(&s->recovery).boot_ready && wifi_tx_event(&s->tx,WIFI_EVENT_REBOOT,now(s));
}
