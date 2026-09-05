#include "universal_node_gateway_thread_tx_adapter.h"
#include "universal_node_gateway_thread_comm_path.h"

#include "coap_transport.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "openthread/ip6.h"
#include "transport.h"

#include <string.h>

static transport_result_t universal_node_gateway_thread_tx_submit(
    const delivery_descriptor_t *descriptor,
    const resolved_destination_t *destination,
    const uint8_t *payload,
    uint16_t payload_len)
{
    if (!descriptor || !destination || !payload || payload_len == 0u) {
        return TRANSPORT_RESULT_INVALID;
    }
    if (destination->transport != ADDRESS_TRANSPORT_THREAD ||
        destination->type != RESOLVED_DESTINATION_THREAD_IPV6) {
        return TRANSPORT_RESULT_BACKEND_REJECTED;
    }
    if (!esp_openthread_lock_acquire(portMAX_DELAY)) {
        return TRANSPORT_RESULT_BACKEND_REJECTED;
    }

    otInstance *instance = esp_openthread_get_instance();
    otIp6Address endpoint = {0};
    memcpy(endpoint.mFields.m8, destination->destination.thread_ipv6,
           sizeof(endpoint.mFields.m8));
    const bool admitted = instance != NULL &&
        coap_transport_send_relay_cmd(instance, &endpoint, payload, payload_len);

    esp_openthread_lock_release();
    return admitted ? TRANSPORT_RESULT_ADMITTED : TRANSPORT_RESULT_BACKEND_REJECTED;
}

static address_resolution_result_t universal_node_gateway_thread_resolve(
    const delivery_descriptor_t *descriptor, resolved_destination_t *out)
{
    if (!descriptor || !out || descriptor->type != DELIVERY_DEVICE_NODE) {
        return ADDRESS_RESOLUTION_INVALID;
    }
    if (!esp_openthread_lock_acquire(portMAX_DELAY)) {
        return ADDRESS_RESOLUTION_UNAVAILABLE;
    }
    otInstance *instance = esp_openthread_get_instance();
    const address_resolution_result_t result =
        universal_node_gateway_thread_select_device_path(
            instance, descriptor->destination.node_id, out);
    esp_openthread_lock_release();
    return result;
}

bool universal_node_gateway_thread_tx_adapter_init(void)
{
    return address_resolver_register(universal_node_gateway_thread_resolve) &&
           transport_backend_register(universal_node_gateway_thread_tx_submit);
}
