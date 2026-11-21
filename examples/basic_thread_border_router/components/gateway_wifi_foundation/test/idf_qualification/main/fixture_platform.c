#include "fixture_platform.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include <string.h>

_Static_assert(CHIP_ESP32S3==QUAL_TARGET_ESP32S3,"Unexpected SDK ESP32-S3 identity");
static bool read_silicon(void *ctx, uint8_t mac[6], uint32_t *target)
{
    (void)ctx;
    esp_chip_info_t chip={0}; esp_chip_info(&chip); *target=(uint32_t)chip.model;
    return esp_efuse_mac_get_default(mac)==ESP_OK;
}
static bool read_flash_size(void *ctx, uint32_t *size)
{
    (void)ctx; return esp_flash_get_size(NULL,size)==ESP_OK;
}
static bool find_partition(void *ctx, qual_partition_identity_t *identity)
{
    (void)ctx;
    /* Find by label across all types so wrong type/subtype is a mismatch,
     * rather than silently accepting another partition or external flash. */
    esp_partition_iterator_t it=esp_partition_find(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,QUAL_PARTITION_LABEL);
    if(!it) return false;
    const esp_partition_t *partition=esp_partition_get(it);
    if(!partition) { esp_partition_iterator_release(it); return false; }
    memcpy(identity->label,partition->label,sizeof(identity->label));
    identity->type=(uint8_t)partition->type; identity->subtype=(uint8_t)partition->subtype;
    identity->offset=partition->address; identity->size=partition->size;
    identity->internal_flash=partition->flash_chip==esp_flash_default_chip;
    /* next releases an exhausted iterator; duplicates make lookup ambiguous. */
    it=esp_partition_next(it);
    if(it) { esp_partition_iterator_release(it); return false; }
    return true;
}
static bool read_application_hash(void *ctx, uint8_t digest[32])
{
    (void)ctx;
    const esp_partition_t *running=esp_ota_get_running_partition();
    if(!running || running->flash_chip!=esp_flash_default_chip || running->type!=ESP_PARTITION_TYPE_APP ||
       running->subtype!=ESP_PARTITION_SUBTYPE_APP_FACTORY || strcmp(running->label,"qual_app") ||
       running->address!=UINT32_C(0x10000) || running->size!=UINT32_C(0x100000)) return false;
    return esp_partition_get_sha256(running,digest)==ESP_OK;
}
qual_identity_provider_t qual_platform_identity_provider(void)
{
    qual_identity_provider_t provider={NULL,read_silicon,read_flash_size,find_partition,read_application_hash};
    return provider;
}
