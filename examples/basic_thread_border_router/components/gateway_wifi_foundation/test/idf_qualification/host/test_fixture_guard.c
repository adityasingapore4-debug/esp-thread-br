#include "fixture_guard.h"
#include "fixture_platform.h"
#include "result_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
typedef struct {
    qual_approved_fixture_t observed;
    unsigned calls, fail_at;
} mock_t;
static mock_t app_mock;
static bool next(mock_t *mock) { mock->calls++; return mock->calls!=mock->fail_at; }
static bool silicon(void *ctx, uint8_t mac[6], uint32_t *target)
{
    mock_t *m=ctx; if(!next(m)) return false;
    memcpy(mac,m->observed.silicon_mac,6); *target=m->observed.target; return true;
}
static bool flash(void *ctx, uint32_t *size)
{
    mock_t *m=ctx; if(!next(m)) return false; *size=m->observed.flash_bytes; return true;
}
static bool partition(void *ctx, qual_partition_identity_t *out)
{
    mock_t *m=ctx; if(!next(m)) return false; *out=m->observed.partition; return true;
}
static bool app_hash(void *ctx, uint8_t hash[32])
{
    mock_t *m=ctx; if(!next(m)) return false; memcpy(hash,m->observed.application_sha256,32); return true;
}
static qual_identity_provider_t provider(mock_t *m)
{
    qual_identity_provider_t p={m,silicon,flash,partition,app_hash}; return p;
}
/* app_main is linked unchanged against this mock; no IDF hardware code runs. */
qual_identity_provider_t qual_platform_identity_provider(void) { return provider(&app_mock); }
void app_main(void);

static qual_approved_fixture_t approved(void)
{
    qual_approved_fixture_t f={0};
    memcpy(f.fixture_id,QUAL_FIXTURE_ID,sizeof(QUAL_FIXTURE_ID));
    memcpy(f.external_approval_ref,"HOST_SYNTHETIC_ONLY",20);
    const uint8_t mac[6]={2,0,0,0,0,1}; memcpy(f.silicon_mac,mac,6);
    f.target=QUAL_TARGET_ESP32S3; f.flash_bytes=UINT32_C(0x200000);
    memcpy(f.partition.label,"nvs",4); f.partition.type=QUAL_PARTITION_DATA;
    f.partition.subtype=QUAL_PARTITION_NVS; f.partition.internal_flash=true;
    f.partition.offset=QUAL_PARTITION_OFFSET; f.partition.size=QUAL_PARTITION_SIZE;
    memset(f.application_sha256,0x5a,sizeof(f.application_sha256));
    f.capabilities=QUAL_CAP_PREFLIGHT; return f;
}
static void expect_blocked(qual_guard_result_t result, qual_guard_reason_t reason)
{
    CHECK(result.reason==reason && !result.identity_verified);
    char out[QUAL_RESULT_CAP]; size_t n=0;
    CHECK(qual_result_format(result,out,sizeof(out),&n));
    CHECK(n==strlen(out) && n<QUAL_RESULT_CAP);
    CHECK(strstr(out,"QUALIFICATION_STATE: LOCKED\n"));
    CHECK(strstr(out,"RESULT: BLOCKED\n"));
    CHECK(strstr(out,"STORAGE_ADMISSION: DENIED\n"));
    CHECK(strstr(out,"NVS_INITIALIZATION: DISABLED\n"));
    CHECK(strstr(out,"NVS_INITIALIZED: NO\n"));
    CHECK(strstr(out,"STORAGE_MUTATION: NO\n"));
    CHECK(strstr(out,"DESTRUCTIVE_CASES: DISABLED\n"));
    CHECK(!strstr(out,"HOST_SYNTHETIC_ONLY"));
}
static void missing_and_untrusted(void)
{
    mock_t m={0}; m.observed=approved();
    expect_blocked(qual_fixture_guard(NULL,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_FIXTURE_NOT_ENROLLED);
    expect_blocked(qual_fixture_guard(&qual_approved_fixtures,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_FIXTURE_NOT_ENROLLED);
    CHECK(m.calls==0 && qual_approved_fixtures.count==0 && !qual_approved_fixtures.entries);
    /* A completely formed mutable device-side manifest/observation, including
     * an approval-looking string, has no route into the trusted allowlist. */
    memcpy(m.observed.external_approval_ref,"SELF_ASSERTED_APPROVAL",23);
    expect_blocked(qual_fixture_guard(&qual_approved_fixtures,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_FIXTURE_NOT_ENROLLED);
    CHECK(m.calls==0);
    puts("missing enrollment and correctly formed unauthorized manifest: PASS");
}
static void mismatches(void)
{
    qual_approved_fixture_t entry=approved(); qual_fixture_allowlist_t list={&entry,1};
    const qual_guard_reason_t reasons[]={QUAL_REASON_HARDWARE_MISMATCH,QUAL_REASON_TARGET_MISMATCH,
        QUAL_REASON_FLASH_CAPACITY_MISMATCH,QUAL_REASON_PARTITION_LABEL_MISMATCH,
        QUAL_REASON_PARTITION_TYPE_MISMATCH,QUAL_REASON_PARTITION_SUBTYPE_MISMATCH,
        QUAL_REASON_PARTITION_OFFSET_MISMATCH,QUAL_REASON_PARTITION_SIZE_MISMATCH,
        QUAL_REASON_PARTITION_DEVICE_MISMATCH,QUAL_REASON_APPLICATION_MISMATCH};
    for(unsigned i=0;i<sizeof(reasons)/sizeof(reasons[0]);i++) {
        mock_t m={0}; m.observed=entry;
        switch(i) {
        case 0: m.observed.silicon_mac[5]^=1; break;
        case 1: m.observed.target++; break;
        case 2: m.observed.flash_bytes*=2; break;
        case 3: memcpy(m.observed.partition.label,"other",6); break;
        case 4: m.observed.partition.type++; break;
        case 5: m.observed.partition.subtype++; break;
        case 6: m.observed.partition.offset+=0x1000; break;
        case 7: m.observed.partition.size+=0x1000; break;
        case 8: m.observed.partition.internal_flash=false; break;
        default: m.observed.application_sha256[0]^=1; break;
        }
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),reasons[i]);
    }
    puts("silicon, target, capacity, label, type/subtype, offset/size, flash device and application mismatches: PASS");
}
static void missing_values_and_capabilities(void)
{
    for(unsigned i=0;i<13;i++) {
        qual_approved_fixture_t entry=approved(); mock_t m={0}; m.observed=entry;
        switch(i) {
        case 0: entry.fixture_id[0]=0; break;
        case 1: entry.external_approval_ref[0]=0; break;
        case 2: memset(entry.silicon_mac,0,6); break;
        case 3: entry.target=0; break;
        case 4: entry.flash_bytes=0; break;
        case 5: entry.partition.label[0]=0; break;
        case 6: entry.partition.type=0; break;
        case 7: entry.partition.subtype=0; break;
        case 8: entry.partition.offset=0; break;
        case 9: entry.partition.size=0; break;
        case 10: memset(entry.application_sha256,0,32); break;
        case 11: entry.capabilities=0; break;
        default: entry.capabilities|=2; break;
        }
        qual_fixture_allowlist_t list={&entry,1};
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_ALLOWLIST_INVALID);
        CHECK(!m.calls);
    }
    qual_approved_fixture_t entry=approved(); qual_fixture_allowlist_t list={&entry,1};
    mock_t m={0}; m.observed=entry;
    const uint32_t denied[]={0,2,4,8,QUAL_CAP_PREFLIGHT|2,UINT32_MAX};
    for(size_t i=0;i<sizeof(denied)/sizeof(denied[0]);i++)
        expect_blocked(qual_fixture_guard(&list,provider(&m),denied[i]),QUAL_REASON_UNAUTHORIZED_CAPABILITY);
    CHECK(!m.calls);
    list.entries=NULL;
    expect_blocked(qual_fixture_guard(&list,provider(&m),1),QUAL_REASON_ALLOWLIST_INVALID);
    list.entries=&entry; list.count=QUAL_ALLOWLIST_MAX+1;
    expect_blocked(qual_fixture_guard(&list,provider(&m),1),QUAL_REASON_ALLOWLIST_INVALID);
    qual_approved_fixture_t duplicate[2]={entry,entry}; list.entries=duplicate; list.count=2;
    expect_blocked(qual_fixture_guard(&list,provider(&m),1),QUAL_REASON_ALLOWLIST_INVALID);
    memset(entry.partition.label,'n',sizeof(entry.partition.label)); list.entries=&entry; list.count=1;
    expect_blocked(qual_fixture_guard(&list,provider(&m),1),QUAL_REASON_ALLOWLIST_INVALID);
    puts("mandatory values, invalid/duplicate allowlist and unauthorized capabilities: PASS");
}
static void provider_failures(void)
{
    qual_approved_fixture_t entry=approved(); qual_fixture_allowlist_t list={&entry,1};
    for(unsigned i=1;i<=4;i++) {
        mock_t m={0}; m.observed=entry; m.fail_at=i;
        expect_blocked(qual_fixture_guard(&list,provider(&m),1),QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE);
        CHECK(m.calls==i);
    }
    mock_t m={0}; m.observed=entry; qual_identity_provider_t p=provider(&m); p.read_silicon=NULL;
    expect_blocked(qual_fixture_guard(&list,p,1),QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE);
    CHECK(!m.calls);
    puts("required identity acquisition failures: PASS");
}
static void positive_preflight_and_protocol(void)
{
    qual_approved_fixture_t entry=approved(); qual_fixture_allowlist_t list={&entry,1};
    mock_t m={0}; m.observed=entry;
    qual_guard_result_t result=qual_fixture_guard(&list,provider(&m),1);
    CHECK(result.identity_verified && result.enrolled && m.calls==4);
    char out[QUAL_RESULT_CAP]; size_t n=0;
    CHECK(qual_result_format(result,out,sizeof(out),&n));
    CHECK(strstr(out,"STORAGE_ADMISSION: DENIED") && strstr(out,"NVS_INITIALIZED: NO"));
    CHECK(strstr(out,"RESULT: PREFLIGHT_ONLY") && strstr(out,"QUALIFICATION_STATE: LOCKED"));
    for(size_t cap=0;cap<=n;cap++) {
        char bounded[QUAL_RESULT_CAP]; memset(bounded,'!',sizeof(bounded)); size_t size=99;
        CHECK(!qual_result_format(result,bounded,cap,&size)); CHECK(!size);
        CHECK(bounded[cap]=='!');
        if(cap) CHECK(bounded[0]=='\0');
    }
    for(unsigned i=0;i<QUAL_REASON_COUNT;i++) {
        qual_guard_result_t blocked={(qual_guard_reason_t)i,false,false};
        if(i==QUAL_REASON_IDENTITY_VERIFIED_PREFLIGHT_ONLY) continue;
        CHECK(qual_result_format(blocked,out,sizeof(out),&n));
    }
    result.reason=QUAL_REASON_COUNT; CHECK(!qual_result_format(result,out,sizeof(out),&n));
    result.reason=QUAL_REASON_HARDWARE_MISMATCH; CHECK(!qual_result_format(result,out,sizeof(out),&n));
    app_main(); CHECK(app_mock.calls==0);
    puts("positive identity still denies storage; bounded fixed diagnostic protocol; default app locked: PASS");
}
int main(void)
{
    missing_and_untrusted(); mismatches(); missing_values_and_capabilities(); provider_failures();
    positive_preflight_and_protocol();
    printf("HOST_FIXTURE_GUARD: %u checks PASS; no hardware provider or NVS implementation linked\n",checks);
    return 0;
}
