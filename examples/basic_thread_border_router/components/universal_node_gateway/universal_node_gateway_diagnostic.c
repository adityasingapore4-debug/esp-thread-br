#include "universal_node_gateway.h"

static universal_node_gateway_diagnostic_consumer_t s_consumer;
static void *s_context;

bool universal_node_gateway_register_diagnostic_consumer(
    universal_node_gateway_diagnostic_consumer_t consumer, void *context)
{
    if (consumer == NULL || s_consumer != NULL) return false;
    s_consumer = consumer;
    s_context = context;
    return true;
}

bool universal_node_gateway_diagnostic_receive(uint16_t source_node_id,
                                                const uint8_t *payload,
                                                uint16_t payload_length)
{
    if (source_node_id == 0u || payload == NULL || payload_length == 0u ||
        payload_length > UNIVERSAL_NODE_GATEWAY_MAX_DIAGNOSTIC_PAYLOAD ||
        s_consumer == NULL) {
        return false;
    }
    s_consumer(source_node_id, payload, payload_length, s_context);
    return true;
}
