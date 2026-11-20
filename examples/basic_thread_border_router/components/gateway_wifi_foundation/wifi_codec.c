#include "wifi_codec.h"
#include <string.h>
void wifi_zero(void *p, size_t n) { volatile uint8_t *q = p; while (n--) *q++ = 0; }
static bool nonzero(const uint8_t *p, size_t n) { uint8_t v=0; for(size_t i=0;i<n;i++) v|=p[i]; return v!=0; }
bool wifi_request_decode(const uint8_t *in, size_t n, wifi_request_t *out) {
 if (!out) return false;
 wifi_zero(out,sizeof(*out));
 if (!in || n<4 || in[0]!=1 || in[1]<1 || in[1]>5 || in[2] || in[3]) return false;
 uint8_t op=in[1];
 if ((op<=2 && n!=4) || (op>=4 && (n!=20 || !nonzero(in+4,16)))) return false;
 if (op==3) {
  if (n<24 || !nonzero(in+4,16) || (in[20]!=1 && in[20]!=2) || !in[21] || in[21]>32 || in[22]<8 || in[22]>63 || in[23] || n!=24u+(size_t)in[21]+in[22]) return false;
  for(size_t i=0;i<in[21];i++) if (!in[24+i]) return false;
  for(size_t i=0;i<in[22];i++) if (in[24+in[21]+i]<0x20 || in[24+in[21]+i]>0x7e) return false;
  out->config.mode=in[20]; out->config.ssid_len=in[21]; out->config.secret_len=in[22];
  memcpy(out->config.ssid,in+24,in[21]); memcpy(out->config.secret,in+24+in[21],in[22]);
 }
 out->op=op; if(op>=3) memcpy(out->id,in+4,16); return true;
}
static void be32(uint8_t *p,uint32_t v) { p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
bool wifi_response_encode(const wifi_response_t *r,uint8_t *out,size_t cap,size_t *written) {
 if(written) *written=0;
 if(!r || !out || !written || r->op<1 || r->op>5 || r->phase>2 || (r->phase==2 && r->op!=2)) return false;
 size_t n=0;
 if(r->op==1) {
  uint8_t f=r->u.status.flags,s=r->u.status.state,m=r->u.status.mode,l=r->u.status.ssid_len;
  if(r->phase!=0 || (f&~7u) || s>6 || m>2 || l>32 || ((f&4u)&&!(f&2u)) || (!(f&1u) && (r->u.status.generation || m || l)) || ((f&1u) && (!r->u.status.generation || !m || !l)) || (s==0 && f) || (s==3 && (!(f&2u)||(f&4u))) || (s==4 && (f&6u)!=6u) || ((s==1 || s==2) && (f&6u))) return false;
  n=12u+l;
 } else if(r->op==2) {
  if(r->u.scan.count>8 || r->u.scan.truncated>1 || (r->phase==1 && (r->u.scan.count || r->u.scan.truncated)) || (r->phase==2 && r->u.scan.count) || (r->phase==0 && !r->u.scan.count)) return false;
  n=8; for(size_t i=0;i<r->u.scan.count;i++) { const wifi_scan_entry_t *e=&r->u.scan.entries[i]; if(e->ssid_len>32 || (e->security>3 && e->security!=255)) return false; n+=4u+e->ssid_len; }
 } else {
  uint8_t s=r->u.tx.state; if(!nonzero(r->u.tx.id,16) || s<1 || s>7 || r->u.tx.reason>8 || r->phase==2 || ((s==4 || s==5 || s==6) && r->phase!=0) || ((s==1 || s==2 || s==3 || s==7) && r->phase!=1) || (s==4 && (!r->u.tx.generation || r->u.tx.reason))) return false; n=28;
 }
 if(cap<n) return false;
 memset(out,0,n); out[0]=1; out[1]=r->op; out[2]=r->phase;
 if(r->op==1) { be32(out+4,r->u.status.generation); out[8]=r->u.status.flags; out[9]=r->u.status.state; out[10]=r->u.status.mode; out[11]=r->u.status.ssid_len; memcpy(out+12,r->u.status.ssid,r->u.status.ssid_len); }
 else if(r->op==2) { out[4]=r->u.scan.count; out[5]=r->u.scan.truncated; size_t o=8; for(size_t i=0;i<r->u.scan.count;i++) { const wifi_scan_entry_t *e=&r->u.scan.entries[i]; out[o]=e->ssid_len; out[o+1]=e->security; out[o+2]=(uint8_t)e->rssi; out[o+3]=e->channel; memcpy(out+o+4,e->ssid,e->ssid_len); o+=4u+e->ssid_len; } }
 else { memcpy(out+4,r->u.tx.id,16); out[20]=r->u.tx.state; out[21]=r->u.tx.reason; be32(out+24,r->u.tx.generation); }
 *written=n; return true;
}
