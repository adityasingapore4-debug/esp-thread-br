#include "wifi_store_nvs.h"
#include <string.h>

static const char *const keys[2]={"slot_a","slot_b"};
/* One serialized storage worker. This latch belongs to the NVS lifetime, not
 * an adapter object. Do not clear it on handle close or adapter replacement. */
static bool subsystem_uncertain;
#ifdef WIFI_NVS_ISOLATED_SYNTHETIC_TEST
static const bool synthetic_writes=true;
#else
static const bool synthetic_writes=false;
#endif

void wifi_nvs_close(wifi_nvs_adapter_t *a)
{
    if(a->opened) nvs_close(a->handle);
    a->handle=0; a->opened=false; a->writable=false;
}
static bool reopen(void *ctx)
{
    wifi_nvs_adapter_t *a=ctx;
    wifi_nvs_close(a);
    /* REMOVE_FAILED can require NVS reinitialization, not just handle reopen.
     * This component cannot deinitialize shared NVS safely. Keep UNKNOWN until
     * an external controlled real restart reinitializes the partition. */
    if(wifi_nvs_restart_required(a)) return false;
    esp_err_t err=nvs_open_from_partition(WIFI_STORE_PARTITION,WIFI_STORE_NAMESPACE,
                                        NVS_READONLY,&a->handle);
    if(err==ESP_ERR_NVS_NOT_FOUND) return true;
    a->opened=err==ESP_OK;
    return a->opened;
}
static wifi_io_result_t read_slot(void *ctx, unsigned slot, uint8_t *p, size_t cap, size_t *n)
{
    wifi_nvs_adapter_t *a=ctx;
    *n=0;
    if(wifi_nvs_restart_required(a)) return WIFI_IO_ERROR;
    if(slot>1 || cap>WIFI_STORE_RECORD_MAX) return WIFI_IO_ERROR;
    if(!a->opened) return WIFI_IO_MISSING;
    size_t size=0;
    esp_err_t err=nvs_get_blob(a->handle,keys[slot],NULL,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND) return WIFI_IO_MISSING;
    if(err==ESP_ERR_NVS_TYPE_MISMATCH) return WIFI_IO_INVALID;
    if(err!=ESP_OK) return WIFI_IO_ERROR;
    if(size>cap || size<16) return WIFI_IO_INVALID;
    err=nvs_get_blob(a->handle,keys[slot],p,&size);
    if(err!=ESP_OK) return WIFI_IO_ERROR;
    *n=size; return WIFI_IO_OK;
}
static bool write_slot(void *ctx, unsigned slot, const uint8_t *p, size_t n)
{
    wifi_nvs_adapter_t *a=ctx;
    if(!synthetic_writes || wifi_nvs_restart_required(a) || slot>1 || n<16 || n>WIFI_STORE_RECORD_MAX) return false;
    /* Best-effort capacity guard; shared users may allocate after this check.
     * nvs_set_blob errors remain UNKNOWN, including REMOVE_FAILED. */
    nvs_stats_t stats={0};
    if(nvs_get_stats(WIFI_STORE_PARTITION,&stats)!=ESP_OK ||
       stats.available_entries<3u+(n+31u)/32u) return false;
    if(!a->writable) {
        wifi_nvs_close(a);
        if(nvs_open_from_partition(WIFI_STORE_PARTITION,WIFI_STORE_NAMESPACE,
                                  NVS_READWRITE,&a->handle)!=ESP_OK) return false;
        a->opened=true; a->writable=true;
    }
    bool ok=nvs_set_blob(a->handle,keys[slot],p,n)==ESP_OK;
    if(!ok) a->uncertain=subsystem_uncertain=true;
    return ok;
}
static bool commit(void *ctx)
{
    wifi_nvs_adapter_t *a=ctx;
    if(!synthetic_writes || wifi_nvs_restart_required(a) || !a->opened || !a->writable) return false;
    bool ok=nvs_commit(a->handle)==ESP_OK;
    if(!ok) a->uncertain=subsystem_uncertain=true;
    return ok;
}
wifi_store_io_t wifi_nvs_boundary(wifi_nvs_adapter_t *a)
{
    /* Fresh adapter only. No NVS API is called until the owner initializes. */
    memset(a,0,sizeof(*a));
    a->uncertain=subsystem_uncertain;
    wifi_store_io_t io={a,read_slot,write_slot,commit,reopen}; return io;
}
bool wifi_nvs_restart_required(void *ctx)
{
    const wifi_nvs_adapter_t *a=ctx;
    return subsystem_uncertain || a->uncertain;
}
