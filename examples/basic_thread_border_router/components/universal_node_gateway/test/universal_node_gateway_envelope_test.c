#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "universal_node_gateway_envelope.h"

static void round_trip(universal_node_gateway_envelope_type_t type)
{
    static const uint8_t payload[] = {0x10, 0x20, 0x30};
    uint8_t frame[UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE + sizeof(payload)];
    uint16_t length;
    universal_node_gateway_envelope_view_t decoded;

    assert(universal_node_gateway_envelope_encode(type, payload, sizeof(payload),
                                                   frame, sizeof(frame), &length));
    assert(universal_node_gateway_envelope_decode(frame, length, &decoded));
    assert(decoded.type == type && decoded.payload_length == sizeof(payload));
    assert(memcmp(decoded.payload, payload, sizeof(payload)) == 0);
}

int main(void)
{
    uint8_t frame[UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE +
                  UNIVERSAL_NODE_GATEWAY_ENVELOPE_MAX_PAYLOAD];
    uint8_t status_payload[UNIVERSAL_NODE_GATEWAY_STATUS_PAYLOAD_SIZE];
    uint16_t length;
    universal_node_gateway_envelope_view_t decoded;
    universal_node_gateway_status_t status = {
        .ready = true, .wifi_connected = true, .thread_enabled = true,
        .thread_attached = true, .thread_role = UNIVERSAL_NODE_GATEWAY_THREAD_ROUTER,
        .rloc16_valid = true, .rloc16 = 0x1234u,
        .last_error = UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NONE,
    }, decoded_status;

    round_trip(UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_REQUEST);
    round_trip(UNIVERSAL_NODE_GATEWAY_ENVELOPE_UN_RESPONSE);
    round_trip(UNIVERSAL_NODE_GATEWAY_ENVELOPE_DEVICE_EVENT);
    assert(universal_node_gateway_status_encode(&status, status_payload));
    assert(universal_node_gateway_status_decode(status_payload, sizeof(status_payload),
                                                 &decoded_status));
    assert(decoded_status.ready == status.ready &&
           decoded_status.wifi_connected == status.wifi_connected &&
           decoded_status.thread_enabled == status.thread_enabled &&
           decoded_status.thread_attached == status.thread_attached &&
           decoded_status.thread_role == status.thread_role &&
           decoded_status.rloc16_valid == status.rloc16_valid &&
           decoded_status.rloc16 == status.rloc16 &&
           decoded_status.last_error == status.last_error);
    assert(universal_node_gateway_envelope_encode(UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS,
                                                   status_payload, sizeof(status_payload),
                                                   frame, sizeof(frame), &length));
    assert(universal_node_gateway_envelope_decode(frame, length, &decoded));
    assert(decoded.type == UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS &&
           decoded.payload_length == sizeof(status_payload));
    frame[0] = 2u;
    assert(!universal_node_gateway_envelope_decode(frame, length, &decoded));
    frame[0] = UNIVERSAL_NODE_GATEWAY_ENVELOPE_VERSION;
    frame[1] = 0xffu;
    assert(!universal_node_gateway_envelope_decode(frame, length, &decoded));
    assert(!universal_node_gateway_envelope_encode(UNIVERSAL_NODE_GATEWAY_ENVELOPE_ERROR,
                                                    frame, 513u, frame, sizeof(frame), &length));
    frame[1] = UNIVERSAL_NODE_GATEWAY_ENVELOPE_STATUS;
    frame[2] = 10u;
    frame[3] = 0u;
    assert(!universal_node_gateway_envelope_decode(frame,
                                                    UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE + 9u,
                                                    &decoded));
    return 0;
}
