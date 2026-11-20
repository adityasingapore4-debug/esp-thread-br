#include "wifi_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
typedef enum { NONE, WRITE_BEFORE, WRITE_TORN, WRITE_AFTER, COMMIT_BEFORE,
               COMMIT_AFTER, READ_FAIL, READ_DIFFER, REOPEN_FAIL } fault_t;
typedef struct {
    uint8_t disk[2][WIFI_STORE_RECORD_MAX], pending[WIFI_STORE_RECORD_MAX];
    size_t len[2], pending_len;
    unsigned pending_slot, writes, commits, reopens;
    bool immediate, written;
    fault_t fault;
} mock_t;
static wifi_io_result_t mock_read(void *ctx, unsigned slot, uint8_t *p, size_t cap, size_t *n)
{
    mock_t *m=ctx; CHECK(slot<2);
    *n=0;
    if(m->written && m->fault==READ_FAIL) return WIFI_IO_ERROR;
    if(!m->len[slot]) return WIFI_IO_MISSING;
    if(m->len[slot]>cap) return WIFI_IO_INVALID;
    memcpy(p,m->disk[slot],m->len[slot]); *n=m->len[slot];
    if(m->written && m->fault==READ_DIFFER) p[*n-1]^=1;
    return WIFI_IO_OK;
}
static void persist(mock_t *m)
{
    memcpy(m->disk[m->pending_slot],m->pending,m->pending_len);
    m->len[m->pending_slot]=m->pending_len;
}
static bool mock_write(void *ctx, unsigned slot, const uint8_t *p, size_t n)
{
    mock_t *m=ctx; CHECK(slot<2 && n<=WIFI_STORE_RECORD_MAX);
    m->writes++; m->written=true;
    if(m->fault==WRITE_BEFORE) return false;
    memcpy(m->pending,p,n); m->pending_slot=slot; m->pending_len=n;
    if(m->fault==WRITE_TORN) {
        persist(m); m->disk[slot][n-1]^=1; return false;
    }
    if(m->immediate || m->fault==WRITE_AFTER) persist(m);
    return m->fault!=WRITE_AFTER;
}
static bool mock_commit(void *ctx)
{
    mock_t *m=ctx; m->commits++;
    if(m->fault==COMMIT_BEFORE) return false;
    persist(m); return m->fault!=COMMIT_AFTER;
}
static bool mock_reopen(void *ctx)
{
    mock_t *m=ctx; m->reopens++;
    if(m->fault==REOPEN_FAIL) return false;
    wifi_zero(m->pending,sizeof(m->pending)); m->pending_len=0; m->written=false;
    return true;
}
static wifi_store_io_t boundary(mock_t *m)
{
    wifi_store_io_t io={m,mock_read,mock_write,mock_commit,mock_reopen}; return io;
}
static wifi_config_t config(unsigned v)
{
    wifi_config_t c={0}; c.mode=(uint8_t)(1+v%2); c.ssid_len=4; c.secret_len=8;
    memcpy(c.ssid,"TEST",4); c.ssid[3]=(uint8_t)('A'+v%26);
    memset(c.secret,(int)('a'+v%26),8); return c;
}
static bool equal(const wifi_config_t *a, const wifi_config_t *b)
{
    return a->mode==b->mode && a->ssid_len==b->ssid_len && a->secret_len==b->secret_len &&
        !memcmp(a->ssid,b->ssid,a->ssid_len) && !memcmp(a->secret,b->secret,a->secret_len);
}
static bool zero(const void *p, size_t n)
{
    const uint8_t *b=p; while(n--) if(*b++) return false; return true;
}
static void seed(mock_t *m, unsigned slot, unsigned v, uint32_t g)
{
    wifi_config_t c=config(v);
    CHECK(wifi_record_encode(&c,g,m->disk[slot],sizeof(m->disk[slot]),&m->len[slot]));
    wifi_zero(&c,sizeof(c));
}
static void expect_load(wifi_storage_t *s, uint32_t g, unsigned v)
{
    wifi_store_t store=wifi_storage_interface(s); wifi_config_t c={0}, expected=config(v);
    uint32_t found=99; CHECK(store.load(store.ctx,&c,&found)); CHECK(found==g);
    if(g) CHECK(equal(&c,&expected)); else CHECK(zero(&c,sizeof(c)));
    wifi_zero(&c,sizeof(c)); wifi_zero(&expected,sizeof(expected));
}
static void admit(wifi_storage_t *s, unsigned v, uint32_t g, uint64_t ticket)
{
    wifi_config_t c=config(v); uint8_t id[16]={1};
    wifi_store_t store=wifi_storage_interface(s);
    CHECK(store.stage(store.ctx,&c,g,id,ticket));
    /* stage copies synchronously and does not write anything. */
    wifi_zero(&c,sizeof(c));
}
static wifi_cancel_result_t execute(wifi_storage_t *s, uint32_t g, uint64_t ticket)
{
    uint8_t id[16]={1}; return wifi_storage_execute(s,id,g,ticket);
}
static void record_tests(void)
{
    uint8_t p[WIFI_STORE_RECORD_MAX], original[WIFI_STORE_RECORD_MAX];
    wifi_config_t c=config(0), decoded; size_t n=0; uint32_t g=0;
    CHECK(wifi_record_encode(&c,1,p,sizeof(p),&n)); CHECK(n==28);
    CHECK(wifi_record_decode(p,n,&decoded,&g)); CHECK(g==1 && equal(&c,&decoded));
    memcpy(original,p,n);
    for(size_t i=0;i<n;i++) {
        CHECK(!wifi_record_decode(p,i,&decoded,&g));
        CHECK(!g && zero(&decoded,sizeof(decoded)));
        p[i]^=1; CHECK(!wifi_record_decode(p,n,&decoded,&g)); p[i]^=1;
    }
    CHECK(!memcmp(original,p,n));
    p[n]=0; CHECK(!wifi_record_decode(p,n+1,&decoded,&g));
    CHECK(!wifi_record_encode(&c,0,p,sizeof(p),&n));
    c.mode=0; CHECK(!wifi_record_encode(&c,1,p,sizeof(p),&n));
    c=config(0); c.ssid_len=33; CHECK(!wifi_record_encode(&c,1,p,sizeof(p),&n));
    c=config(0); c.secret_len=64; CHECK(!wifi_record_encode(&c,1,p,sizeof(p),&n));
    c=config(0); c.secret[0]=0; CHECK(!wifi_record_encode(&c,1,p,sizeof(p),&n));
    c=config(0); c.ssid[0]=0; CHECK(!wifi_record_encode(&c,1,p,sizeof(p),&n));
    c=config(0); c.ssid_len=32; c.secret_len=63;
    memset(c.ssid,'S',32); memset(c.secret,'x',63);
    CHECK(wifi_record_encode(&c,UINT32_MAX,p,sizeof(p),&n)); CHECK(n==111);
    CHECK(wifi_record_decode(p,n,&decoded,&g)); CHECK(g==UINT32_MAX && equal(&c,&decoded));
    CHECK(!wifi_record_encode(&c,1,p,110,&n));
    wifi_zero(p,sizeof(p)); wifi_zero(original,sizeof(original));
    wifi_zero(&c,sizeof(c)); wifi_zero(&decoded,sizeof(decoded));
    puts("record encoding, bounds, truncation and corruption: PASS");
}
static void boot_tests(void)
{
    mock_t m={0}; wifi_storage_t s;
    CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,0,0);
    seed(&m,0,0,1); seed(&m,1,1,2);
    CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,2,1);
    m.disk[1][0]^=1;
    CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,1,0);
    m.disk[1][0]^=1; m.disk[0][0]^=1;
    CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,2,1);
    m.disk[1][0]^=1; CHECK(!wifi_storage_init(&s,boundary(&m)));
    memset(&m,0,sizeof(m)); seed(&m,0,0,7); seed(&m,1,1,7);
    CHECK(!wifi_storage_init(&s,boundary(&m)));
    seed(&m,1,0,7); CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,7,0);
    memset(&m,0,sizeof(m)); seed(&m,0,0,UINT32_MAX);
    CHECK(wifi_storage_init(&s,boundary(&m)));
    wifi_config_t c=config(1); uint8_t id[16]={1}; wifi_store_t st=wifi_storage_interface(&s);
    CHECK(!st.stage(st.ctx,&c,0,id,1)); CHECK(!m.writes);
    puts("boot selection, independent slots, equal-generation conflict and overflow: PASS");
}
static void repeated_commits(void)
{
    mock_t m={0}; wifi_storage_t s; CHECK(wifi_storage_init(&s,boundary(&m)));
    for(uint32_t g=1;g<=24;g++) {
        admit(&s,g,g,g); CHECK(m.writes==g-1);
        CHECK(execute(&s,g,g)==WIFI_CANCEL_DURABLE);
        CHECK(zero(&s.candidate,sizeof(s.candidate)));
        expect_load(&s,g,g);
        CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,g,g);
        if(g>1) {
            unsigned latest=(g-1)%2; m.disk[latest][0]^=1;
            wifi_storage_t recovered; CHECK(wifi_storage_init(&recovered,boundary(&m)));
            expect_load(&recovered,g-1,g-1); m.disk[latest][0]^=1;
        }
    }
    puts("24 alternating generations, restart and previous-slot recovery: PASS");
}
static void failures(void)
{
    const fault_t faults[]={WRITE_BEFORE,WRITE_TORN,WRITE_AFTER,COMMIT_BEFORE,COMMIT_AFTER,READ_FAIL,READ_DIFFER};
    for(unsigned immediate=0;immediate<2;immediate++) {
        for(unsigned prior=1;prior<=6;prior++) {
            for(size_t f=0;f<sizeof(faults)/sizeof(faults[0]);f++) {
                mock_t m={0}; wifi_storage_t s;
                unsigned old=(prior-1)%2;
                seed(&m,old,prior,prior);
                if(prior>1) seed(&m,1u-old,prior-1,prior-1);
                uint8_t previous[WIFI_STORE_RECORD_MAX]; memcpy(previous,m.disk[old],sizeof(previous));
                CHECK(wifi_storage_init(&s,boundary(&m))); admit(&s,prior+1,prior+1,1);
                m.immediate=immediate!=0; m.fault=faults[f];
                CHECK(execute(&s,prior+1,1)==WIFI_CANCEL_UNKNOWN);
                CHECK(!memcmp(previous,m.disk[old],sizeof(previous)));
                wifi_store_t st=wifi_storage_interface(&s); wifi_config_t c=config(9); uint8_t id[16]={2};
                CHECK(st.cancel_commit(st.ctx)==WIFI_CANCEL_UNKNOWN);
                CHECK(!st.stage(st.ctx,&c,prior+1,id,2));
                st.clear(st.ctx); CHECK(!zero(&s.candidate,sizeof(s.candidate)));
                uint32_t g=0; CHECK(!st.load(st.ctx,&c,&g)); CHECK(!g && zero(&c,sizeof(c)));
                m.fault=REOPEN_FAIL; CHECK(wifi_storage_recover(&s)==WIFI_CANCEL_UNKNOWN);
                m.fault=NONE;
                bool persisted=faults[f]==WRITE_AFTER || faults[f]==COMMIT_AFTER ||
                    faults[f]==READ_FAIL || faults[f]==READ_DIFFER || (immediate && faults[f]==COMMIT_BEFORE);
                /* Fresh backend against persistent bytes before resolving the
                 * old transaction: simulated abrupt loss of its entire RAM. */
                wifi_storage_t boot;
                CHECK(wifi_storage_init(&boot,boundary(&m)));
                expect_load(&boot,persisted?prior+1:prior,persisted?prior+1:prior);
                CHECK(wifi_storage_recover(&s)==(persisted?WIFI_CANCEL_DURABLE:WIFI_CANCEL_ABORTED));
                CHECK(zero(&s.candidate,sizeof(s.candidate)));
                expect_load(&s,persisted?prior+1:prior,persisted?prior+1:prior);
                CHECK(!memcmp(previous,m.disk[old],sizeof(previous)));
                CHECK(wifi_storage_init(&s,boundary(&m)));
                expect_load(&s,persisted?prior+1:prior,persisted?prior+1:prior);
            }
        }
    }
    puts("84 fault cases: deferred/immediate persistence, six previous generations: PASS");
}
static void fencing(void)
{
    mock_t m={0}; wifi_storage_t s; CHECK(wifi_storage_init(&s,boundary(&m)));
    wifi_store_t st=wifi_storage_interface(&s); admit(&s,1,1,1);
    CHECK(st.cancel_commit(st.ctx)==WIFI_CANCEL_ABORTED);
    CHECK(zero(&s.candidate,sizeof(s.candidate)));
    CHECK(execute(&s,1,1)==WIFI_CANCEL_UNKNOWN); CHECK(!m.writes);
    admit(&s,2,1,2);
    CHECK(execute(&s,1,1)==WIFI_CANCEL_UNKNOWN); CHECK(!m.writes);
    uint8_t wrong[16]={9};
    CHECK(wifi_storage_execute(&s,wrong,1,2)==WIFI_CANCEL_UNKNOWN);
    CHECK(execute(&s,2,2)==WIFI_CANCEL_UNKNOWN); CHECK(!m.writes);
    CHECK(execute(&s,1,2)==WIFI_CANCEL_DURABLE);
    CHECK(st.cancel_commit(st.ctx)==WIFI_CANCEL_DURABLE);
    admit(&s,3,2,3); m.fault=REOPEN_FAIL;
    CHECK(!st.restart(st.ctx)); CHECK(!s.ready);
    CHECK(execute(&s,2,3)==WIFI_CANCEL_UNKNOWN); CHECK(m.writes==1);
    m.fault=NONE; CHECK(st.restart(st.ctx));
    CHECK(zero(&s.candidate,sizeof(s.candidate)));
    CHECK(execute(&s,2,3)==WIFI_CANCEL_UNKNOWN); CHECK(m.writes==1);
    expect_load(&s,1,2);
    puts("cancel fence, stale ID/generation/ticket, restart failure and queued power loss: PASS");
}
static void mismatched_recovery(void)
{
    mock_t m={0}; seed(&m,0,1,1); wifi_storage_t s;
    CHECK(wifi_storage_init(&s,boundary(&m))); admit(&s,2,2,1);
    m.fault=READ_FAIL; CHECK(execute(&s,2,1)==WIFI_CANCEL_UNKNOWN);
    seed(&m,1,3,2); m.fault=NONE;
    CHECK(wifi_storage_recover(&s)==WIFI_CANCEL_UNKNOWN);
    CHECK(s.state==WIFI_STORAGE_UNKNOWN);
    wifi_store_t st=wifi_storage_interface(&s);
    CHECK(st.restart(st.ctx)); expect_load(&s,2,3);
    /* Restart reports authority, never invents the prior transaction result. */
    CHECK(s.state==WIFI_STORAGE_READY && zero(&s.candidate,sizeof(s.candidate)));
    puts("valid but mismatched readback remains unresolved until explicit restart: PASS");
}
static uint32_t test_crc(const uint8_t *p, size_t n)
{
    uint32_t c=UINT32_MAX;
    while(n--) {
        c^=*p++;
        for(unsigned i=0;i<8;i++) c=(c>>1)^((0u-(c&1u))&UINT32_C(0xedb88320));
    }
    return ~c;
}
static void semantic_record_tests(void)
{
    uint8_t p[WIFI_STORE_RECORD_MAX]; size_t n=0;
    wifi_config_t c=config(0), out; uint32_t g;
    /* Malformations with a recomputed valid CRC exercise schema and grammar
     * independently of checksum failure. */
    const unsigned offsets[]={2,3,4,5,6,7,8,9,10,11,12,16};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        CHECK(wifi_record_encode(&c,1,p,sizeof(p),&n));
        unsigned offset=offsets[i];
        if(offset>=4 && offset<=7) memset(p+4,0,4);
        else p[offset]=offset==2?2:offset==3 || offset==11?1:0;
        uint32_t crc=test_crc(p,n-4);
        p[n-4]=(uint8_t)(crc>>24); p[n-3]=(uint8_t)(crc>>16);
        p[n-2]=(uint8_t)(crc>>8); p[n-1]=(uint8_t)crc;
        CHECK(!wifi_record_decode(p,n,&out,&g));
        CHECK(!g && zero(&out,sizeof(out)));
    }
    mock_t m={0}; m.len[0]=WIFI_STORE_RECORD_MAX+1;
    wifi_storage_t s; CHECK(!wifi_storage_init(&s,boundary(&m)));
    seed(&m,1,2,3); CHECK(wifi_storage_init(&s,boundary(&m))); expect_load(&s,3,2);
    puts("valid-CRC malformed grammar and oversized slot: PASS");
}
static void transaction_test(void)
{
    mock_t m={0}; wifi_storage_t s; wifi_tx_t tx;
    CHECK(wifi_storage_init(&s,boundary(&m)));
    CHECK(wifi_tx_init(&tx,wifi_storage_interface(&s)));
    wifi_request_t r={0}; r.op=3; r.id[0]=1; r.config=config(1);
    CHECK(wifi_tx_start(&tx,&r,true,0));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_ASSOCIATED,1));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_IP_READY,2));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_STABILITY_COMPLETE,5002));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_PROMOTION_ADMIT,5003));
    CHECK(wifi_tx_event(&tx,WIFI_EVENT_CANCEL,5004));
    CHECK(execute(&s,1,tx.ticket)==WIFI_CANCEL_DURABLE);
    CHECK(wifi_tx_complete(&tx,WIFI_EVENT_COMMIT_SUCCESS,r.id,0,tx.ticket,60000));
    CHECK(tx.state==WIFI_TX_COMMITTED && tx.generation==1);
    CHECK(zero(&tx.candidate,sizeof(tx.candidate)));
    puts("accepted transaction interface: admitted promotion wins later cancel: PASS");
}
int main(void)
{
    record_tests(); boot_tests(); repeated_commits(); failures(); fencing();
    mismatched_recovery(); semantic_record_tests(); transaction_test();
    printf("HOST_MOCK_VALIDATION: %u checks passed\n",checks);
    return 0;
}
