#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "universal_node_gateway.h"

#define UNIVERSAL_NODE_GATEWAY_ENVELOPE_VERSION 1u
#define UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE 4u
#define UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD 512u
#define UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE 9u

typedef enum {
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS = 1,
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_REQUEST,
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_RESPONSE,
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_DEVICE_EVENT,
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_DIAGNOSTIC,
    UNIVERSAL_NODE_GATEWAY_ENVELOPE_ERROR,
} universal_node_gateway_envelope_type_t;

typedef struct {
    universal_node_gateway_envelope_type_t type;
    const uint8_t *payload;
    uint16_t payload_length;
} universal_node_gateway_envelope_view_t;

bool universal_node_gateway_envelope_encode(
    universal_node_gateway_envelope_type_t type, const uint8_t *payload,
    uint16_t payload_length, uint8_t *out, uint16_t out_capacity,
    uint16_t *out_length);
bool universal_node_gateway_envelope_decode(
    const uint8_t *frame, uint16_t frame_length,
    universal_node_gateway_envelope_view_t *out);
bool universal_node_gateway_status_encode(
    const universal_node_gateway_status_t *status, uint8_t out[UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE]);
bool universal_node_gateway_status_decode(
    const uint8_t *payload, uint16_t payload_length,
    universal_node_gateway_status_t *out_status);
