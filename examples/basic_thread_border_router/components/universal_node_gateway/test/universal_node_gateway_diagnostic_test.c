#include <assert.h>
#include <string.h>

#include "universal_node_gateway.h"
#include "universal_node_gateway_envelope.h"

static unsigned s_calls;
static uint16_t s_source;
static uint8_t s_payload[UNIVERSAL_NODE_GATEWAY_MAX_DIAGNOSTIC_PAYLOAD];
static uint16_t s_payload_length;

static void consumer(uint16_t source_node_id, const uint8_t *payload,
                     uint16_t payload_length, void *context)
{
    (void)context;
    ++s_calls;
    s_source = source_node_id;
    s_payload_length = payload_length;
    memcpy(s_payload, payload, payload_length);
}

int main(void)
{
    const uint8_t payload[] = {0xa5u, 0x5au};
    uint8_t oversized[UNIVERSAL_NODE_GATEWAY_MAX_DIAGNOSTIC_PAYLOAD + 1u] = {0};
    uint8_t diagnostic[UNIVERSAL_NODE_GATEWAY_DIAGNOSTIC_SOURCE_SIZE + sizeof(payload)] = {
        0x34u, 0x12u, 0xa5u, 0x5au};
    uint8_t frame[UNIVERSAL_NODE_GATEWAY_ENVELOPE_HEADER_SIZE + sizeof(diagnostic)];
    uint16_t frame_length;
    universal_node_gateway_envelope_view_t decoded;

    assert(!universal_node_gateway_diagnostic_receive(0x1234u, payload, sizeof(payload)));
    assert(!universal_node_gateway_register_diagnostic_consumer(NULL, NULL));
    assert(universal_node_gateway_register_diagnostic_consumer(consumer, NULL));
    assert(!universal_node_gateway_diagnostic_receive(0u, payload, sizeof(payload)));
    assert(!universal_node_gateway_diagnostic_receive(0x1234u, NULL, sizeof(payload)));
    assert(!universal_node_gateway_diagnostic_receive(0x1234u, payload, 0u));
    assert(!universal_node_gateway_diagnostic_receive(0x1234u, oversized, sizeof(oversized)));
    assert(universal_node_gateway_diagnostic_receive(0x1234u, payload, sizeof(payload)));
    assert(s_calls == 1u && s_source == 0x1234u && s_payload_length == sizeof(payload) &&
           memcmp(s_payload, payload, sizeof(payload)) == 0);
    assert(universal_node_gateway_envelope_encode(UNIVERSAL_NODE_GATEWAY_ENVELOPE_DIAGNOSTIC,
                                                   diagnostic, sizeof(diagnostic), frame,
                                                   sizeof(frame), &frame_length));
    assert(universal_node_gateway_envelope_decode(frame, frame_length, &decoded));
    assert(decoded.type == UNIVERSAL_NODE_GATEWAY_ENVELOPE_DIAGNOSTIC &&
           decoded.payload_length == sizeof(diagnostic) &&
           memcmp(decoded.payload, diagnostic, sizeof(diagnostic)) == 0);
    return 0;
}
