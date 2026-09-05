#include "universal_node_gateway_envelope.h"

#include <string.h>

static bool envelope_type_valid(universal_node_gateway_envelope_type_t type)
{
    return type >= UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS &&
           type <= UNIVERSAL_NODE_GATEWAY_ENVELOPE_ERROR;
}

bool universal_node_gateway_envelope_encode(
    universal_node_gateway_envelope_type_t type, const uint8_t *payload,
    uint16_t payload_length, uint8_t *out, uint16_t out_capacity,
    uint16_t *out_length)
{
    const uint16_t total = (uint16_t)(UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE +
                                      payload_length);
    if (out_length != NULL) *out_length = 0u;
    if (!envelope_type_valid(type) || (payload == NULL && payload_length != 0u) ||
        payload_length > UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD ||
        out == NULL || out_length == NULL || out_capacity < total) {
        return false;
    }
    out[0] = UNIVERSAL_NODE_GATEWAY_ENVELOPE_VERSION;
    out[1] = (uint8_t)type;
    out[2] = (uint8_t)payload_length;
    out[3] = (uint8_t)(payload_length >> 8u);
    if (payload_length != 0u) memcpy(&out[UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE],
                                     payload, payload_length);
    *out_length = total;
    return true;
}

bool universal_node_gateway_envelope_decode(
    const uint8_t *frame, uint16_t frame_length,
    universal_node_gateway_envelope_view_t *out)
{
    uint16_t payload_length;
    universal_node_gateway_envelope_type_t type;
    if (frame == NULL || out == NULL ||
        frame_length < UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE ||
        frame[0] != UNIVERSAL_NODE_GATEWAY_ENVELOPE_VERSION) {
        return false;
    }
    type = (universal_node_gateway_envelope_type_t)frame[1];
    payload_length = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8u);
    if (!envelope_type_valid(type) ||
        payload_length > UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD ||
        frame_length != (uint16_t)(UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE +
                                   payload_length)) {
        return false;
    }
    *out = (universal_node_gateway_envelope_view_t){
        .type = type,
        .payload = &frame[UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE],
        .payload_length = payload_length,
    };
    return true;
}

bool universal_node_gateway_status_encode(
    const universal_node_gateway_status_t *status, uint8_t out[UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE])
{
    if (status == NULL || out == NULL) return false;
    out[0] = status->ready;
    out[1] = status->wifi_connected;
    out[2] = status->thread_enabled;
    out[3] = status->thread_attached;
    out[4] = (uint8_t)status->thread_role;
    out[5] = status->rloc16_valid;
    out[6] = (uint8_t)status->rloc16;
    out[7] = (uint8_t)(status->rloc16 >> 8u);
    out[8] = (uint8_t)status->last_error;
    return true;
}

bool universal_node_gateway_status_decode(
    const uint8_t *payload, uint16_t payload_length,
    universal_node_gateway_status_t *out_status)
{
    if (payload == NULL || out_status == NULL ||
        payload_length != UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE ||
        payload[4] > UNIVERSAL_NODE_GATEWAY_THREAD_LEADER ||
        payload[8] > UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_SUBMIT_REJECTED) {
        return false;
    }
    *out_status = (universal_node_gateway_status_t){
        .ready = payload[0] != 0u,
        .wifi_connected = payload[1] != 0u,
        .thread_enabled = payload[2] != 0u,
        .thread_attached = payload[3] != 0u,
        .thread_role = (universal_node_gateway_thread_role_t)payload[4],
        .rloc16_valid = payload[5] != 0u,
        .rloc16 = (uint16_t)payload[6] | ((uint16_t)payload[7] << 8u),
        .last_error = (universal_node_gateway_status_error_t)payload[8],
    };
    return true;
}
