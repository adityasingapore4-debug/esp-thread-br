#include "result_protocol.h"

bool qual_result_format(qual_guard_result_t result, char *out, size_t cap, size_t *written)
{
    static const char *const reasons[QUAL_REASON_COUNT]={
        "FIXTURE_NOT_ENROLLED", "ALLOWLIST_INVALID", "REQUIRED_IDENTITY_UNAVAILABLE",
        "HARDWARE_MISMATCH", "TARGET_MISMATCH", "FLASH_CAPACITY_MISMATCH",
        "PARTITION_LABEL_MISMATCH", "PARTITION_TYPE_MISMATCH", "PARTITION_SUBTYPE_MISMATCH",
        "PARTITION_OFFSET_MISMATCH", "PARTITION_SIZE_MISMATCH", "PARTITION_DEVICE_MISMATCH",
        "APPLICATION_MISMATCH", "UNAUTHORIZED_CAPABILITY", "IDENTITY_VERIFIED_PREFLIGHT_ONLY"
    };
    if(written) *written=0;
    if(!out || !written || !cap) return false;
    out[0]='\0';
    if((unsigned)result.reason>=QUAL_REASON_COUNT ||
       result.identity_verified!=(result.reason==QUAL_REASON_IDENTITY_VERIFIED_PREFLIGHT_ONLY) ||
       (result.identity_verified && !result.enrolled)) return false;
    const char *const parts[]={
        "ADR091_QUAL:\nCASE: FIXTURE_PREFLIGHT\nQUALIFICATION_STATE: LOCKED\n"
        "FIXTURE_ENROLLED: ",result.enrolled?"YES":"NO",
        "\nRESULT: ",result.identity_verified?"PREFLIGHT_ONLY":"BLOCKED",
        "\nREASON: ",reasons[result.reason],"\n"
        "STORAGE_ADMISSION: DENIED\nNVS_INITIALIZATION: DISABLED\nNVS_INITIALIZED: NO\n"
        "NVS_WRITES: DISABLED\nSTORAGE_MUTATION: NO\nDESTRUCTIVE_CASES: DISABLED\n"
    };
    char record[QUAL_RESULT_CAP]; size_t n=0;
    for(size_t i=0;i<sizeof(parts)/sizeof(parts[0]);i++) {
        for(const char *p=parts[i];*p;p++) {
            if(n+1>=sizeof(record)) return false;
            record[n++]=*p;
        }
    }
    record[n]='\0';
    if(n>=cap) return false;
    for(size_t i=0;i<=n;i++) out[i]=record[i];
    *written=n; return true;
}
