#include "wifi_store.h"
#include <limits.h>
#include <string.h>

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
/* CRC-32/ISO-HDLC: accidental corruption detection, not authentication. */
static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c=UINT32_MAX;
    while(n--) {
        c^=*p++;
        for(unsigned i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&UINT32_C(0xedb88320));
    }
    return ~c;
}
static bool valid_config(const wifi_config_t *c)
{
    if(!c || c->mode<1 || c->mode>2 || !c->ssid_len || c->ssid_len>32 ||
       c->secret_len<8 || c->secret_len>63) return false;
    for(size_t i=0;i<c->ssid_len;i++) if(!c->ssid[i]) return false;
    for(size_t i=0;i<c->secret_len;i++)
        if(c->secret[i]<0x20 || c->secret[i]>0x7e) return false;
    return true;
}
static bool same(const wifi_config_t *a, const wifi_config_t *b)
{
    return a->mode==b->mode && a->ssid_len==b->ssid_len &&
        a->secret_len==b->secret_len &&
        memcmp(a->ssid,b->ssid,a->ssid_len)==0 &&
        memcmp(a->secret,b->secret,a->secret_len)==0;
}
bool wifi_record_encode(const wifi_config_t *c, uint32_t g, uint8_t *p, size_t cap, size_t *n)
{
    if(n) *n=0;
    if(!p || !n || !g || !valid_config(c)) return false;
    size_t len=16u+c->ssid_len+c->secret_len;
    if(cap<len) return false;
    /* GW, schema, reserved, generation BE, mode/lengths/reserved, payload, CRC BE. */
    p[0]='G'; p[1]='W'; p[2]=1; p[3]=0; put32(p+4,g);
    p[8]=c->mode; p[9]=c->ssid_len; p[10]=c->secret_len; p[11]=0;
    memcpy(p+12,c->ssid,c->ssid_len);
    memcpy(p+12+c->ssid_len,c->secret,c->secret_len);
    put32(p+len-4,crc32(p,len-4)); *n=len;
    return true;
}
bool wifi_record_decode(const uint8_t *p, size_t n, wifi_config_t *c, uint32_t *g)
{
    if(c) wifi_zero(c,sizeof(*c));
    if(g) *g=0;
    if(!p || !c || !g || n<16 || n>WIFI_STORE_RECORD_MAX ||
       p[0]!='G' || p[1]!='W' || p[2]!=1 || p[3] || p[11] ||
       !get32(p+4) || p[9]>32 || p[10]>63 ||
       n!=16u+p[9]+p[10] || get32(p+n-4)!=crc32(p,n-4)) return false;
    c->mode=p[8]; c->ssid_len=p[9]; c->secret_len=p[10];
    memcpy(c->ssid,p+12,c->ssid_len);
    memcpy(c->secret,p+12+c->ssid_len,c->secret_len);
    if(!valid_config(c)) { wifi_zero(c,sizeof(*c)); return false; }
    *g=get32(p+4); return true;
}

static bool scan(wifi_storage_t *s, wifi_config_t *out, uint32_t *g, int *slot)
{
    uint8_t bytes[WIFI_STORE_RECORD_MAX]={0};
    wifi_config_t c[2]={{0}};
    uint32_t gen[2]={0}; bool valid[2]={false,false}, empty[2]={false,false};
    bool ok=false;
    wifi_zero(out,sizeof(*out)); *g=0; *slot=-1;
    for(unsigned i=0;i<2;i++) {
        size_t n=0;
        wifi_io_result_t r=s->io.read(s->io.ctx,i,bytes,sizeof(bytes),&n);
        if(r==WIFI_IO_ERROR) goto done;
        empty[i]=r==WIFI_IO_MISSING;
        valid[i]=r==WIFI_IO_OK && wifi_record_decode(bytes,n,&c[i],&gen[i]);
        wifi_zero(bytes,sizeof(bytes));
    }
    if(!valid[0] && !valid[1]) { ok=empty[0] && empty[1]; goto done; }
    if(valid[0] && valid[1] && gen[0]==gen[1] && !same(&c[0],&c[1])) goto done;
    *slot=valid[0] && (!valid[1] || gen[0]>=gen[1])?0:1;
    *out=c[*slot]; *g=gen[*slot]; ok=true;
done:
    wifi_zero(bytes,sizeof(bytes)); wifi_zero(c,sizeof(c));
    return ok;
}
static void wipe_operation(wifi_storage_t *s)
{
    wifi_zero(&s->candidate,sizeof(s->candidate));
    wifi_zero(s->id,sizeof(s->id)); s->target=0; s->ticket=0;
}
static bool load(void *ctx, wifi_config_t *out, uint32_t *g)
{
    wifi_storage_t *s=ctx;
    if(!out || !g) return false;
    wifi_zero(out,sizeof(*out)); *g=0;
    if(!s->ready || s->state==WIFI_STORAGE_UNKNOWN || s->state==WIFI_STORAGE_QUEUED) return false;
    int slot=-1;
    if(!scan(s,out,g,&slot)) { s->ready=false; return false; }
    s->generation=*g; s->slot=slot; return true;
}
static bool stage(void *ctx, const wifi_config_t *c, uint32_t g, const uint8_t id[16], uint64_t ticket)
{
    wifi_storage_t *s=ctx;
    uint8_t any=0;
    if(id) for(unsigned i=0;i<16;i++) any|=id[i];
    if(!s->ready || s->state==WIFI_STORAGE_QUEUED || s->state==WIFI_STORAGE_UNKNOWN ||
       !valid_config(c) || !any || !ticket || ticket<=s->last_ticket ||
       s->generation==UINT32_MAX || g!=s->generation+1u) return false;
    wipe_operation(s);
    s->candidate.mode=c->mode; s->candidate.ssid_len=c->ssid_len; s->candidate.secret_len=c->secret_len;
    memcpy(s->candidate.ssid,c->ssid,c->ssid_len);
    memcpy(s->candidate.secret,c->secret,c->secret_len);
    memcpy(s->id,id,16); s->target=g; s->ticket=s->last_ticket=ticket;
    s->target_slot=s->slot==0?1:0; s->state=WIFI_STORAGE_QUEUED;
    return true;
}
static wifi_cancel_result_t cancel(void *ctx)
{
    wifi_storage_t *s=ctx;
    if(s->state==WIFI_STORAGE_UNKNOWN) return WIFI_CANCEL_UNKNOWN;
    if(s->state==WIFI_STORAGE_DURABLE) return WIFI_CANCEL_DURABLE;
    s->state=WIFI_STORAGE_ABORTED; wipe_operation(s);
    return WIFI_CANCEL_ABORTED;
}
static bool restore(void *ctx, const wifi_config_t *prior, uint32_t g)
{
    wifi_config_t current={0}; uint32_t found=0;
    bool ok=load(ctx,&current,&found) && found==g && (!g || same(prior,&current));
    wifi_zero(&current,sizeof(current));
    /* Storage check only. The future runtime owner must restore association. */
    return ok;
}
static void clear(void *ctx)
{
    wifi_storage_t *s=ctx;
    /* clear is never a cancellation fence; unresolved verification needs RAM. */
    if(s->state!=WIFI_STORAGE_QUEUED && s->state!=WIFI_STORAGE_UNKNOWN) wipe_operation(s);
}
static bool restart(void *ctx)
{
    wifi_storage_t *s=ctx;
    wifi_config_t c={0}; uint32_t g=0; int slot=-1;
    /* Quarantine before attempting recovery; failure must block mutations. */
    s->ready=false;
    if(!s->io.reopen(s->io.ctx)) return false;
    /* Successful reopen fences all queued worker calls, including on scan error. */
    wipe_operation(s); s->state=WIFI_STORAGE_UNKNOWN;
    bool ok=scan(s,&c,&g,&slot);
    wifi_zero(&c,sizeof(c));
    if(!ok) return false;
    s->generation=g; s->slot=slot; s->ready=true; s->state=WIFI_STORAGE_READY;
    return true;
}
bool wifi_storage_init(wifi_storage_t *s, wifi_store_io_t io)
{
    if(!s || !io.read || !io.write || !io.commit || !io.reopen) return false;
    /* Fresh instance only; never reinitialize over a live worker. */
    memset(s,0,sizeof(*s)); s->io=io; s->slot=-1;
    return restart(s);
}
wifi_store_t wifi_storage_interface(wifi_storage_t *s)
{
    wifi_store_t iface={s,load,stage,cancel,restore,clear,restart}; return iface;
}
wifi_cancel_result_t wifi_storage_execute(wifi_storage_t *s, const uint8_t id[16], uint32_t g, uint64_t ticket)
{
    if(!s || !id || !s->ready || s->state!=WIFI_STORAGE_QUEUED ||
       s->target!=g || s->ticket!=ticket || memcmp(s->id,id,16)) return WIFI_CANCEL_UNKNOWN;
    uint8_t bytes[WIFI_STORE_RECORD_MAX]={0}, readback[WIFI_STORE_RECORD_MAX]={0};
    wifi_config_t authoritative={0}; size_t n=0, got=0;
    uint32_t found=0; int slot=-1;
    if(!wifi_record_encode(&s->candidate,g,bytes,sizeof(bytes),&n)) {
        s->state=WIFI_STORAGE_ABORTED; wipe_operation(s); return WIFI_CANCEL_ABORTED;
    }
    /* Once write starts, even an error may have persisted. Never infer abort. */
    s->state=WIFI_STORAGE_UNKNOWN;
    bool ok=s->io.write(s->io.ctx,(unsigned)s->target_slot,bytes,n) &&
        s->io.commit(s->io.ctx) &&
        s->io.read(s->io.ctx,(unsigned)s->target_slot,readback,sizeof(readback),&got)==WIFI_IO_OK &&
        got==n && memcmp(bytes,readback,n)==0 &&
        scan(s,&authoritative,&found,&slot) && found==g &&
        slot==s->target_slot && same(&authoritative,&s->candidate);
    wifi_zero(bytes,sizeof(bytes)); wifi_zero(readback,sizeof(readback));
    wifi_zero(&authoritative,sizeof(authoritative));
    if(!ok) return WIFI_CANCEL_UNKNOWN;
    s->generation=g; s->slot=slot; s->state=WIFI_STORAGE_DURABLE;
    wipe_operation(s); return WIFI_CANCEL_DURABLE;
}
wifi_cancel_result_t wifi_storage_recover(wifi_storage_t *s)
{
    if(!s || !s->ready || s->state!=WIFI_STORAGE_UNKNOWN || !s->target)
        return WIFI_CANCEL_UNKNOWN;
    if(!s->io.reopen(s->io.ctx)) return WIFI_CANCEL_UNKNOWN;
    wifi_config_t current={0}; uint32_t g=0; int slot=-1;
    bool ok=scan(s,&current,&g,&slot);
    wifi_cancel_result_t result=WIFI_CANCEL_UNKNOWN;
    if(ok && g==s->target && slot==s->target_slot && same(&current,&s->candidate)) {
        result=WIFI_CANCEL_DURABLE; s->state=WIFI_STORAGE_DURABLE;
    } else if(ok && g==s->generation && slot==s->slot) {
        result=WIFI_CANCEL_ABORTED; s->state=WIFI_STORAGE_ABORTED;
    }
    wifi_zero(&current,sizeof(current));
    if(result!=WIFI_CANCEL_UNKNOWN) {
        s->generation=g; s->slot=slot; wipe_operation(s);
    }
    return result;
}
