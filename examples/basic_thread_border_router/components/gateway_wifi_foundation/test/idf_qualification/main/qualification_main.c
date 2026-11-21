#include "fixture_guard.h"
#include "fixture_platform.h"
#include "result_protocol.h"
#include <stdio.h>

void app_main(void)
{
    /* Empty external allowlist returns LOCKED before invoking any platform
     * identity provider. There is deliberately no path from preflight to NVS. */
    qual_guard_result_t result=qual_fixture_guard(&qual_approved_fixtures,
                                                qual_platform_identity_provider(),QUAL_CAP_PREFLIGHT);
    char record[QUAL_RESULT_CAP]; size_t length=0;
    if(qual_result_format(result,record,sizeof(record),&length)) {
        (void)fwrite(record,1,length,stdout);
    } else {
        puts("ADR091_QUAL: RESULT_PROTOCOL_FAILURE; QUALIFICATION_STATE: LOCKED; STORAGE_ADMISSION: DENIED");
    }
}
