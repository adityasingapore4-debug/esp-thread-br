#define main fixture_regression_main
#include "test_fixture_guard.c"
#undef main

static void fixture_case(const char *id)
{
    qual_approved_fixture_t entry=approved(); qual_fixture_allowlist_t list={&entry,1};
    mock_t m={0}; m.observed=entry;
    if(!strcmp(id,"NO_ENROLLMENT")) {
        expect_blocked(qual_fixture_guard(&qual_approved_fixtures,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_FIXTURE_NOT_ENROLLED);
        CHECK(qual_approved_fixtures.count==0 && !qual_approved_fixtures.entries && !m.calls);
    } else if(!strcmp(id,"FIXTURE_MISMATCH")) {
        m.observed.silicon_mac[5]^=1;
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_HARDWARE_MISMATCH);
    } else if(!strcmp(id,"WRONG_PARTITION")) {
        m.observed.partition.offset+=0x1000;
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_PARTITION_OFFSET_MISMATCH);
    } else if(!strcmp(id,"WRONG_APP")) {
        m.observed.application_sha256[0]^=1;
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_APPLICATION_MISMATCH);
    } else if(!strcmp(id,"NO_AUTH")) {
        entry.external_approval_ref[0]=0;
        expect_blocked(qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_ALLOWLIST_INVALID);
        CHECK(!m.calls);
    } else if(!strcmp(id,"PRODUCTION_DENY")) {
        qual_guard_result_t result=qual_fixture_guard(&list,provider(&m),QUAL_CAP_PREFLIGHT);
        CHECK(result.identity_verified && result.enrolled && m.calls==4);
        char out[QUAL_RESULT_CAP]; size_t n=0;
        CHECK(qual_result_format(result,out,sizeof(out),&n));
        CHECK(strstr(out,"RESULT: PREFLIGHT_ONLY") && strstr(out,"STORAGE_ADMISSION: DENIED"));
        CHECK(strstr(out,"NVS_INITIALIZED: NO") && strstr(out,"STORAGE_MUTATION: NO"));
        expect_blocked(qual_fixture_guard(&qual_approved_fixtures,provider(&m),QUAL_CAP_PREFLIGHT),QUAL_REASON_FIXTURE_NOT_ENROLLED);
        CHECK(qual_approved_fixtures.count==0 && !qual_approved_fixtures.entries);
    }
}
int main(int argc, char **argv)
{
    if(argc!=2) return 2;
    const char *ids[]={"FIXTURE_MISMATCH","NO_ENROLLMENT","WRONG_PARTITION","WRONG_APP","NO_AUTH","PRODUCTION_DENY"};
    bool found=false; for(size_t i=0;i<sizeof(ids)/sizeof(ids[0]);i++) if(!strcmp(argv[1],ids[i])) found=true;
    if(!found) return 2;
    fixture_case(argv[1]); printf("CASE_ASSERTIONS:%s:%u\n",argv[1],checks);
    return checks ? 0 : 3;
}
