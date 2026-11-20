#include "wifi_store_nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned opens, closes, writes, commits;
static esp_err_t open_error, read_error, write_error, commit_error;
static size_t blob_size=28, available=100;
static nvs_open_mode_t mode;
esp_err_t nvs_open_from_partition(const char *p, const char *ns, nvs_open_mode_t m, nvs_handle_t *h)
{
    assert(!strcmp(p,"nvs") && !strcmp(ns,"gw_wifi"));
    opens++; mode=m; *h=1; return open_error;
}
void nvs_close(nvs_handle_t h) { assert(h==1); closes++; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *p, size_t *n)
{
    assert(h==1 && (!strcmp(key,"slot_a") || !strcmp(key,"slot_b")));
    if(read_error) return read_error;
    if(p) { assert(*n>=blob_size); memset(p,0,blob_size); }
    *n=blob_size; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *p, size_t n)
{
    assert(h==1 && mode==NVS_READWRITE && p && n==28 && !strcmp(key,"slot_a"));
    writes++; return write_error;
}
esp_err_t nvs_get_stats(const char *p, nvs_stats_t *s)
{
    assert(!strcmp(p,"nvs")); s->available_entries=available; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { assert(h==1); commits++; return commit_error; }
int main(void)
{
    wifi_nvs_adapter_t a; wifi_store_io_t io=wifi_nvs_boundary(&a);
    uint8_t bytes[WIFI_STORE_RECORD_MAX]={0}; size_t n=0;
    assert(!opens && io.reopen(io.ctx)); assert(mode==NVS_READONLY);
    assert(io.read(io.ctx,0,bytes,sizeof(bytes),&n)==WIFI_IO_OK && n==28);
    blob_size=112; assert(io.read(io.ctx,0,bytes,sizeof(bytes),&n)==WIFI_IO_INVALID && !n);
    blob_size=28; read_error=ESP_ERR_NVS_TYPE_MISMATCH;
    assert(io.read(io.ctx,1,bytes,sizeof(bytes),&n)==WIFI_IO_INVALID);
    read_error=ESP_ERR_NVS_NOT_FOUND;
    assert(io.read(io.ctx,1,bytes,sizeof(bytes),&n)==WIFI_IO_MISSING);
    read_error=99; assert(io.read(io.ctx,1,bytes,sizeof(bytes),&n)==WIFI_IO_ERROR);
    read_error=0;
#ifdef WIFI_NVS_ISOLATED_SYNTHETIC_TEST
    available=0; assert(!io.write(io.ctx,0,bytes,28) && !writes);
    available=100; assert(io.write(io.ctx,0,bytes,28)); assert(io.commit(io.ctx));
    assert(writes==1 && commits==1 && mode==NVS_READWRITE);
    write_error=99; assert(!io.write(io.ctx,0,bytes,28));
    assert(a.uncertain && !io.reopen(io.ctx));
    /* Only a fresh adapter after externally controlled actual NVS recovery
     * may clear this latch. Handle reopen cannot repair REMOVE_FAILED. */
    write_error=0; io=wifi_nvs_boundary(&a); assert(io.reopen(io.ctx));
    assert(io.write(io.ctx,0,bytes,28)); commit_error=99;
    assert(!io.commit(io.ctx) && a.uncertain && !io.reopen(io.ctx));
    puts("HOST_MOCK_VALIDATION: synthetic NVS adapter boundary and uncertainty latch PASS");
#else
    assert(!io.write(io.ctx,0,bytes,28)); assert(!io.commit(io.ctx));
    assert(!writes && !commits && mode==NVS_READONLY);
    puts("HOST_MOCK_VALIDATION: default NVS write lock PASS");
#endif
    wifi_nvs_close(&a); assert(closes>0);
    open_error=ESP_ERR_NVS_NOT_FOUND; assert(io.reopen(io.ctx)==!a.uncertain);
    return 0;
}
