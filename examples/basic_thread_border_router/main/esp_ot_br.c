/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_ot_ota_commands.h"
#include "esp_ot_wifi_cmd.h"
#include "esp_spiffs.h"
#include "esp_vfs_eventfd.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#include "border_router_launch.h"
#include "esp_br_web.h"
#include "universal_node_gateway.h"
#include "universal_node_cli.h"
#include "universal_node_gateway_http_transport.h"
#include "universal_node_gateway_thread_rx_adapter.h"
#include "universal_node_gateway_thread_tx_adapter.h"
#include "address_resolver.h"
#include "provisioning_v2.h"
#include "thread_runtime.h"
#include "esp_openthread_lock.h"
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"

#define TAG "esp_ot_br"

extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");

/* G1C/G1D owns no OpenThread instance.  It starts the one Spinel-host
 * instance used later by OTBR, but deliberately does not call the Border
 * Router launcher or esp_openthread_auto_start(): an unprovisioned target
 * must wait for the ADR-077 Joiner/Entrust lifecycle. */
static esp_err_t start_canonical_commissioning_openthread(void)
{
    const esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    const esp_vfs_eventfd_config_t eventfd_config = {.max_fds = 2u};
    uint8_t eui64[8];
    otInstance *instance;
    esp_err_t result;

    ESP_RETURN_ON_ERROR(esp_vfs_eventfd_register(&eventfd_config), TAG,
                        "OpenThread eventfd initialization failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif initialization failed");
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE) return result;
    ESP_RETURN_ON_ERROR(esp_openthread_start(&config), TAG,
                        "Spinel OpenThread initialization failed");

    instance = esp_openthread_get_instance();
    if (instance == NULL) {
        ESP_LOGE(TAG, "canonical OpenThread instance unavailable");
        return ESP_FAIL;
    }

    if (!esp_openthread_lock_acquire(portMAX_DELAY)) {
        ESP_LOGE(TAG, "canonical OpenThread lock unavailable");
        return ESP_FAIL;
    }
    const bool orphan_dataset =
        provisioning_v2_lifecycle() != PROVISIONING_V2_LIFECYCLE_NORMAL &&
        otDatasetIsCommissioned(instance);
    esp_openthread_lock_release();
    if (orphan_dataset) {
        ESP_LOGW(TAG, "clearing orphan OpenThread Dataset before Joiner mode");
        if (!thread_runtime_clear_incomplete_dataset()) {
            ESP_LOGE(TAG, "canonical orphan Dataset cleanup failed");
            return ESP_FAIL;
        }
    }

    if (!thread_runtime_radio_eui64_get(eui64)) {
        ESP_LOGE(TAG, "canonical Thread extended address unavailable");
        return ESP_FAIL;
    }
    if (!esp_openthread_lock_acquire(portMAX_DELAY)) {
        ESP_LOGE(TAG, "canonical OpenThread lock unavailable after Dataset check");
        return ESP_FAIL;
    }
    const otError address_result = otLinkSetExtendedAddress(
        instance, (const otExtAddress *)eui64);
    otError attach_result = OT_ERROR_NONE;
    if (address_result == OT_ERROR_NONE &&
        provisioning_v2_lifecycle() == PROVISIONING_V2_LIFECYCLE_NORMAL &&
        otDatasetIsCommissioned(instance)) {
        attach_result = otIp6SetEnabled(instance, true);
        if (attach_result == OT_ERROR_NONE) {
            attach_result = otThreadSetEnabled(instance, true);
        }
    }
    esp_openthread_lock_release();
    if (address_result != OT_ERROR_NONE || attach_result != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "canonical OpenThread setup failed address=%d attach=%d",
                 address_result, attach_result);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "G1C/G1D Spinel OpenThread instance ready (no auto-network)");
    return ESP_OK;
}

#if CONFIG_UN_GATEWAY_G1A_ENABLE_PRODUCT_TRAFFIC
static esp_err_t init_spiffs(void)
{
#if CONFIG_AUTO_UPDATE_RCP
    esp_vfs_spiffs_conf_t rcp_fw_conf = {.base_path = "/" CONFIG_RCP_PARTITION_NAME,
                                         .partition_label = CONFIG_RCP_PARTITION_NAME,
                                         .max_files = 10,
                                         .format_if_mount_failed = false};
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&rcp_fw_conf), TAG, "Failed to mount rcp firmware storage");
#endif
#if CONFIG_OPENTHREAD_BR_START_WEB
    esp_vfs_spiffs_conf_t web_server_conf = {
        .base_path = "/spiffs", .partition_label = "web_storage", .max_files = 10, .format_if_mount_failed = false};
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&web_server_conf), TAG, "Failed to mount web storage");
#endif
    return ESP_OK;
}
#endif

void app_main(void)
{
#if CONFIG_UN_GATEWAY_G1A_ENABLE_PRODUCT_TRAFFIC
    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    size_t max_eventfd = 3;

#if CONFIG_OPENTHREAD_RADIO_SPINEL_SPI
    // * SpiSpinelInterface (The Spi Spinel Interface needs an eventfd.)
    max_eventfd++;
#endif
#if CONFIG_OPENTHREAD_RADIO_TREL
    // * TREL reception (The Thread Radio Encapsulation Link needs an eventfd for reception.)
    max_eventfd++;
#endif
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
    };

    esp_openthread_config_t openthread_config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config =
            {
                .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
                .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
                .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
            },
    };
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
#endif

    ESP_ERROR_CHECK(nvs_flash_init());

    /* ESP-IDF platform entropy is available after platform/NVS startup.
     * Open the canonical Provisioning V2 gate before commissioning starts. */
    provisioning_v2_entropy_mark_ready();

    /* The canonical foundation owns the only Universal Node identity and
     * persistent security state.  G1A intentionally stops here unless the
     * later Gateway/OTBR product-traffic profile is selected. */
    ESP_ERROR_CHECK(universal_node_gateway_init());

#if CONFIG_UN_CLI
    ESP_ERROR_CHECK(universal_node_console_start());
#endif

#if CONFIG_UN_GATEWAY_G1A_ENABLE_PRODUCT_TRAFFIC
    ESP_ERROR_CHECK(init_spiffs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

#if !CONFIG_OPENTHREAD_BR_AUTO_START && CONFIG_EXAMPLE_CONNECT_ETHERNET
// TODO: Add a mechanism for connecting ETH manually.
#error Currently we do not support a manual way to connect ETH, if you want to use ETH, please enable OPENTHREAD_BR_AUTO_START.
#endif

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
#if CONFIG_OPENTHREAD_CLI_OTA
    esp_set_ota_server_cert((char *)server_cert_pem_start);
#endif

    address_runtime_resolver_init();
    ESP_ERROR_CHECK(universal_node_gateway_thread_tx_adapter_init() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(universal_node_gateway_http_transport_init());
#if CONFIG_OPENTHREAD_BR_START_WEB
    esp_br_web_start("/spiffs");
#endif
    launch_openthread_border_router(&openthread_config, &rcp_update_config);
    ESP_ERROR_CHECK(universal_node_gateway_thread_rx_adapter_register(
        esp_openthread_get_instance()) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(universal_node_gateway_start());
#else
    ESP_ERROR_CHECK(start_canonical_commissioning_openthread());
    ESP_ERROR_CHECK(universal_node_gateway_start_provisioning_carrier());
    ESP_LOGI(TAG, "G1C/G1D provisioning ready; Gateway HTTP and OTBR traffic disabled");
#endif
}
