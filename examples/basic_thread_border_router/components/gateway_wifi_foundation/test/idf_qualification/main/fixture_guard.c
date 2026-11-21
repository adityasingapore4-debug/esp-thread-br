#include "fixture_guard.h"
#include <string.h>

static bool bounded_string(const char *s, size_t cap)
{
    return s[0] && memchr(s,'\0',cap)!=NULL;
}
static bool populated_bytes(const uint8_t *p, size_t n)
{
    uint8_t any=0, not_ff=0;
    for(size_t i=0;i<n;i++) { any|=p[i]; not_ff|=(uint8_t)~p[i]; }
    return any && not_ff;
}
static bool valid_entry(const qual_approved_fixture_t *entry)
{
    const qual_partition_identity_t *p=&entry->partition;
    return bounded_string(entry->fixture_id,sizeof(entry->fixture_id)) &&
        !strcmp(entry->fixture_id,QUAL_FIXTURE_ID) &&
        bounded_string(entry->external_approval_ref,sizeof(entry->external_approval_ref)) &&
        populated_bytes(entry->silicon_mac,sizeof(entry->silicon_mac)) &&
        entry->target==QUAL_TARGET_ESP32S3 && entry->flash_bytes &&
        bounded_string(p->label,sizeof(p->label)) && !strcmp(p->label,QUAL_PARTITION_LABEL) &&
        p->type==QUAL_PARTITION_DATA && p->subtype==QUAL_PARTITION_NVS &&
        p->offset==QUAL_PARTITION_OFFSET && p->size==QUAL_PARTITION_SIZE && p->internal_flash &&
        entry->flash_bytes>=UINT32_C(0x110000) &&
        p->offset<=entry->flash_bytes && p->size<=entry->flash_bytes-p->offset &&
        populated_bytes(entry->application_sha256,sizeof(entry->application_sha256)) &&
        entry->capabilities==QUAL_CAP_PREFLIGHT;
}
qual_guard_result_t qual_fixture_guard(const qual_fixture_allowlist_t *allowlist,
                                      qual_identity_provider_t provider, uint32_t requested)
{
    qual_guard_result_t result={QUAL_REASON_FIXTURE_NOT_ENROLLED,false,false};
    if(!allowlist || !allowlist->count) return result;
    result.reason=QUAL_REASON_ALLOWLIST_INVALID;
    if(!allowlist->entries || allowlist->count>QUAL_ALLOWLIST_MAX) return result;
    for(size_t i=0;i<allowlist->count;i++) {
        if(!valid_entry(&allowlist->entries[i])) return result;
        for(size_t j=0;j<i;j++)
            if(!memcmp(allowlist->entries[i].silicon_mac,allowlist->entries[j].silicon_mac,6)) return result;
    }
    result.reason=QUAL_REASON_UNAUTHORIZED_CAPABILITY;
    if(requested!=QUAL_CAP_PREFLIGHT) return result;
    result.reason=QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE;
    if(!provider.read_silicon || !provider.read_flash_size || !provider.find_partition ||
       !provider.read_application_hash) return result;
    uint8_t mac[6]={0}, hash[32]={0}; uint32_t target=0, flash=0;
    qual_partition_identity_t partition={0};
    if(!provider.read_silicon(provider.ctx,mac,&target)) return result;
    const qual_approved_fixture_t *entry=NULL;
    for(size_t i=0;i<allowlist->count;i++)
        if(!memcmp(allowlist->entries[i].silicon_mac,mac,sizeof(mac))) entry=&allowlist->entries[i];
    result.reason=QUAL_REASON_HARDWARE_MISMATCH;
    if(!entry) return result;
    result.enrolled=true;
    result.reason=QUAL_REASON_TARGET_MISMATCH;
    if(target!=entry->target) return result;
    result.reason=QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE;
    if(!provider.read_flash_size(provider.ctx,&flash)) return result;
    result.reason=QUAL_REASON_FLASH_CAPACITY_MISMATCH;
    if(flash!=entry->flash_bytes) return result;
    result.reason=QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE;
    if(!provider.find_partition(provider.ctx,&partition)) return result;
    result.reason=QUAL_REASON_PARTITION_LABEL_MISMATCH;
    if(!bounded_string(partition.label,sizeof(partition.label)) || strcmp(partition.label,entry->partition.label)) return result;
    result.reason=QUAL_REASON_PARTITION_TYPE_MISMATCH;
    if(partition.type!=entry->partition.type) return result;
    result.reason=QUAL_REASON_PARTITION_SUBTYPE_MISMATCH;
    if(partition.subtype!=entry->partition.subtype) return result;
    result.reason=QUAL_REASON_PARTITION_OFFSET_MISMATCH;
    if(partition.offset!=entry->partition.offset) return result;
    result.reason=QUAL_REASON_PARTITION_SIZE_MISMATCH;
    if(partition.size!=entry->partition.size) return result;
    result.reason=QUAL_REASON_PARTITION_DEVICE_MISMATCH;
    if(!partition.internal_flash) return result;
    result.reason=QUAL_REASON_REQUIRED_IDENTITY_UNAVAILABLE;
    if(!provider.read_application_hash(provider.ctx,hash)) return result;
    result.reason=QUAL_REASON_APPLICATION_MISMATCH;
    if(memcmp(hash,entry->application_sha256,sizeof(hash))) return result;
    result.reason=QUAL_REASON_IDENTITY_VERIFIED_PREFLIGHT_ONLY;
    result.identity_verified=true;
    return result;
}
