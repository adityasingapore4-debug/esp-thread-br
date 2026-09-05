#include "universal_node_gateway_thread_rx_adapter.h"

#include <string.h>

#include "esp_log.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "mesh_auth.h"
#include "openthread/coap.h"
#include "openthread/ip6.h"
#include "universal_node_gateway_rx.h"

#define UNIVERSAL_NODE_GATEWAY_RX_URI "universal-node/rx"
#define UNIVERSAL_NODE_GATEWAY_UDEV_URI "relay/cmd"

static otCoapResource s_gateway_rx_resource;
static otCoapResource s_gateway_udev_resource;
static const char s_gateway_udev_context;

static void universal_node_gateway_coap_rx(void *context, otMessage *message,
                                           const otMessageInfo *message_info)
{
    uint8_t payload[PROTECTED_WIRE_FRAME_CAPACITY];
    uint16_t offset;
    uint16_t message_length;
    uint16_t payload_length;
    const bool is_udev_resource = context == &s_gateway_udev_context;

    if (message == NULL || message_info == NULL) {
        if (is_udev_resource) {
            ESP_LOGW("GATEWAY_UDEV_PAYLOAD", "RESULT=missing_message_info");
        }
        return;
    }
    offset = otMessageGetOffset(message);
    message_length = otMessageGetLength(message);
    payload_length = message_length >= offset ?
        (uint16_t)(message_length - offset) : 0u;
    if (is_udev_resource) {
        ESP_LOGI("GATEWAY_UDEV_COAP_RX", "METHOD=%u TYPE=%u PAYLOAD_LEN=%u",
                 (unsigned)otCoapMessageGetCode(message),
                 (unsigned)otCoapMessageGetType(message), payload_length);
    }
    if (payload_length == 0u ||
        payload_length > sizeof(payload)) {
        if (is_udev_resource) {
            ESP_LOGW("GATEWAY_UDEV_PAYLOAD", "LEN=%u RESULT=%s", payload_length,
                     payload_length == 0u ? "empty" : "too_large");
        }
        return;
    }
    if (otMessageRead(message, offset, payload, payload_length) != payload_length) {
        if (is_udev_resource) {
            ESP_LOGW("GATEWAY_UDEV_PAYLOAD", "LEN=%u RESULT=read_failed",
                     payload_length);
        }
        return;
    }
    if (is_udev_resource) {
        ESP_LOGI("GATEWAY_UDEV_PAYLOAD", "LEN=%u RESULT=ok", payload_length);
    }
    const gateway_rx_ingress_result_t ingress = universal_node_gateway_handle_thread_rx(
        payload, payload_length, message_info->mPeerAddr.mFields.m8);
    if (is_udev_resource) {
        ESP_LOGI("GATEWAY_UDEV_INGRESS", "RESULT=%s",
                 ingress == GATEWAY_RX_INGRESS_CONSUMED ? "consumed" :
                 ingress == GATEWAY_RX_INGRESS_RESOLVER_OBSERVATION_REJECTED ?
                     "resolver_observation_rejected" : "rejected");
    }
}

bool universal_node_gateway_thread_rx_adapter_register(otInstance *instance)
{
    if (instance == NULL) {
        ESP_LOGE("GATEWAY_UDEV_RESOURCE", "URI=relay/cmd RESULT=instance_null");
        return false;
    }

    esp_openthread_lock_acquire(portMAX_DELAY);
    const otError coap_start = otCoapStart(instance, OT_DEFAULT_COAP_PORT);
    if (coap_start != OT_ERROR_NONE) {
        esp_openthread_lock_release();
        ESP_LOGE("GATEWAY_UDEV_RESOURCE", "URI=relay/cmd RESULT=coap_start_failed:%d",
                 (int)coap_start);
        return false;
    }
    s_gateway_rx_resource.mUriPath = UNIVERSAL_NODE_GATEWAY_RX_URI;
    s_gateway_rx_resource.mHandler = universal_node_gateway_coap_rx;
    s_gateway_rx_resource.mContext = NULL;
    otCoapAddResource(instance, &s_gateway_rx_resource);

    s_gateway_udev_resource.mUriPath = UNIVERSAL_NODE_GATEWAY_UDEV_URI;
    s_gateway_udev_resource.mHandler = universal_node_gateway_coap_rx;
    s_gateway_udev_resource.mContext = (void *)&s_gateway_udev_context;
    otCoapAddResource(instance, &s_gateway_udev_resource);
    esp_openthread_lock_release();
    ESP_LOGI("GATEWAY_UDEV_RESOURCE", "URI=relay/cmd RESULT=registered");
    return true;
}
