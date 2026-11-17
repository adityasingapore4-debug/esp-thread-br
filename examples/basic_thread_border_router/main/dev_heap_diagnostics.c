#include "dev_heap_diagnostics.h"

#if CONFIG_UN_GATEWAY_DEV_P4_TLS_SMOKE
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if CONFIG_UN_GATEWAY_DEV_PSRAM
#include "esp_event.h"
#include "esp_https_server.h"
#include "esp_psram.h"
#endif

/* Runs in the allocating context, potentially under memory pressure. ROM
 * output avoids the normal logger, allocation, task enumeration and heap locks.
 * Each line describes an unsuccessful allocation, not necessarily a fatal
 * failure: a caller may have a supported fallback. No payload is inspected. */
static void allocation_failed(size_t size, uint32_t caps, const char *function)
{
    esp_rom_printf("DEV_HEAP alloc_failed bytes=%u caps=0x%x function=%s\n",
                   (unsigned)size, (unsigned)caps, function);
}

void dev_heap_diagnostics_init(void)
{
    ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(allocation_failed));
#if CONFIG_UN_GATEWAY_DEV_PSRAM
    /* IDF startup owns initialization, testing and heap integration. Verify
     * those results before any commissioned application service starts. */
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t expected = 2u * 1024u * 1024u;
    const bool initialized = esp_psram_is_initialized();
    const size_t physical = esp_psram_get_size();
    const size_t heap_total = heap_caps_get_total_size(external);
    ESP_LOGI("dev_heap", "PSRAM initialized=%u physical_bytes=%u heap_bytes=%u "
             "clock_mhz=%d malloc_internal_threshold=%d internal_reserve=%d",
             initialized, (unsigned)physical, (unsigned)heap_total,
             CONFIG_SPIRAM_SPEED, CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL,
             CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL);
    if (!initialized || physical != expected || heap_total == 0) {
        ESP_LOGE("dev_heap", "FC61 PSRAM profile requires initialized 2 MiB heap-backed PSRAM");
        ESP_ERROR_CHECK(ESP_ERR_INVALID_STATE);
    }
#endif
}

void dev_heap_snapshot(const char *boundary)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t dma = internal | MALLOC_CAP_DMA;
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    /* Independent samples, not an atomic heap image. Free general heap may
     * overlap internal/PSRAM; never sum these measurements. HWM is for the
     * calling task only, in ESP-IDF bytes. No diagnostic task/buffer is added. */
    ESP_LOGI("dev_heap", "%s internal_free=%u internal_largest=%u internal_min=%u "
             "dma_free=%u dma_largest=%u "
             "general_free=%u psram_total=%u psram_free=%u psram_largest=%u "
             "current_stack_hwm=%u tasks=%u",
             boundary,
             (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_minimum_free_size(internal),
             (unsigned)heap_caps_get_free_size(dma),
             (unsigned)heap_caps_get_largest_free_block(dma),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (unsigned)heap_caps_get_total_size(external),
             (unsigned)heap_caps_get_free_size(external),
             (unsigned)heap_caps_get_largest_free_block(external),
             (unsigned)uxTaskGetStackHighWaterMark(NULL),
             (unsigned)uxTaskGetNumberOfTasks());
}

#if CONFIG_UN_GATEWAY_DEV_PSRAM
static void https_event(void *argument, esp_event_base_t base, int32_t event_id, void *data)
{
    (void)argument;
    (void)base;
    (void)data;
    /* IDF emits CONNECTED after its genuine TLS handshake. These samples run
     * later on the event-loop task; minimum heap also covers intervening lows.
     * Do not inspect TLS, HTTP data, credentials or connection identifiers. */
    if (event_id == HTTPS_SERVER_EVENT_ON_CONNECTED) dev_heap_snapshot("after_tls_handshake_event");
    else if (event_id == HTTPS_SERVER_EVENT_DISCONNECTED) dev_heap_snapshot("after_tls_disconnect_event");
    else if (event_id == HTTPS_SERVER_EVENT_ERROR) dev_heap_snapshot("https_error_event");
}

void dev_heap_watch_https(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(ESP_HTTPS_SERVER_EVENT, ESP_EVENT_ANY_ID, https_event, NULL));
}
#endif

/* Development-only link wrapping preserves the existing helper and its
 * credential/storage contract. It observes the exact public SDK boundary,
 * including a return before example_wifi_start() would ESP_ERROR_CHECK it.
 * No SDK/private OSI callback or allocator is replaced. */
esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config);
esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config)
{
    ESP_LOGI("dev_heap", "Wi-Fi config static_rx=%d dynamic_rx=%d tx_type=%d "
             "static_tx=%d dynamic_tx=%d psram_build=%d nimble_internal=%d "
             "queue_control_bytes=%u",
             config->static_rx_buf_num, config->dynamic_rx_buf_num,
             config->tx_buf_type, config->static_tx_buf_num,
             config->dynamic_tx_buf_num,
#ifdef CONFIG_SPIRAM
             1,
#else
             0,
#endif
#ifdef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
             1,
#else
             0,
#endif
             (unsigned)sizeof(StaticQueue_t));
    dev_heap_snapshot("before_esp_wifi_init");
    const esp_err_t result = __real_esp_wifi_init(config);
    dev_heap_snapshot(result == ESP_OK ? "after_esp_wifi_init_ok" : "after_esp_wifi_init_error");
    ESP_LOGI("dev_heap", "esp_wifi_init result=%s", esp_err_to_name(result));
    return result;
}
#endif
