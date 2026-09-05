#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "address_resolver.h"
#include "mesh_auth.h"
#include "universal_node_gateway_rx.h"

static mesh_auth_verify_status_t s_auth_status = MESH_AUTH_VERIFY_AUTHENTICATED;
static bool s_consistent = true;
static bool s_resolver_accepts = true;
static unsigned s_resolver_count;
static unsigned s_consumer_count;
static universal_node_gateway_rx_class_t s_last_classification;
static const uint8_t *s_last_borrowed_data;

mesh_auth_verify_status_t mesh_auth_verify_inbound_result(
    const uint8_t *frame, size_t length, mesh_auth_verification_t *out)
{
    (void)frame;
    *out = (mesh_auth_verification_t){
        .status = s_auth_status,
        .protected_payload_length = length,
        .authenticated_sender_node_id = 0x0405u,
        .authenticated_recipient_node_id = 0x0102u,
    };
    return s_auth_status;
}

bool mesh_auth_control_frame_consistent(const uint8_t *frame, size_t length,
                                        const mesh_auth_verification_t *verification)
{
    (void)frame;
    (void)length;
    (void)verification;
    return s_consistent;
}

bool address_resolver_observe_authenticated_thread_endpoint(
    uint16_t node_id, const uint8_t endpoint[THREAD_ENDPOINT_IPV6_SIZE])
{
    assert(node_id == 0x0405u && endpoint != NULL);
    ++s_resolver_count;
    return s_resolver_accepts;
}

static void consumer(const universal_node_gateway_rx_event_t *event, void *context)
{
    unsigned *synchronous = context;
    assert(*synchronous == 1u);
    ++s_consumer_count;
    s_last_classification = event->classification;
    s_last_borrowed_data = event->canonical_payload.data;
}

static void reset_counts(void)
{
    s_auth_status = MESH_AUTH_VERIFY_AUTHENTICATED;
    s_consistent = true;
    s_resolver_accepts = true;
    s_resolver_count = 0u;
    s_consumer_count = 0u;
    s_last_classification = GATEWAY_RX_INVALID;
    s_last_borrowed_data = NULL;
}

static void make_control_frame(uint8_t frame[CONTROL_FRAME_HEADER_SIZE], bool response)
{
    memset(frame, 0, CONTROL_FRAME_HEADER_SIZE);
    frame[CF_OFF_VERSION] = CONTROL_FRAME_VERSION;
    frame[CF_OFF_FLAGS] = response ? CONTROL_FLAG_RESPONSE : 0u;
    frame[CF_OFF_ORIGIN] = 0x04u; frame[CF_OFF_ORIGIN + 1u] = 0x05u;
    frame[CF_OFF_TARGET] = 0x01u; frame[CF_OFF_TARGET + 1u] = 0x02u;
    frame[CF_OFF_REQUEST_ID] = 0x12u; frame[CF_OFF_REQUEST_ID + 1u] = 0x34u;
}

int main(void)
{
    uint8_t peer[THREAD_ENDPOINT_IPV6_SIZE] = { 0xfd, 0u, 0u, 1u };
    uint8_t control_frame[CONTROL_FRAME_HEADER_SIZE];
    uint8_t relay[] = {
        'U','D','E','V',1u,1u,28u,0u,
        1u,1u,0u,1u,0x04u,0x05u,1u,1u,1u,1u,
        0u,0u,0u,0u,0u,0u,0u,1u,0u,8u, 1u,1u,1u,0u,3u,0u,1u,1u,
    };
    uint8_t usyn[] = {
        'U','S','Y','N',1u,1u,1u,1u,0u,0x34u,0x12u,5u,0x05u,0x04u,3u,2u,
        0xefu,0xbeu,
    };
    unsigned synchronous = 1u;

    assert(universal_node_gateway_register_rx_consumer(consumer, &synchronous));

    reset_counts();
    make_control_frame(control_frame, true);
    assert(universal_node_gateway_handle_thread_rx(control_frame, sizeof(control_frame), peer) ==
           GATEWAY_RX_INGRESS_CONSUMED);
    assert(s_consumer_count == 1u && s_resolver_count == 1u);
    assert(s_last_classification == GATEWAY_RX_RESPONSE && s_last_borrowed_data == control_frame);

    reset_counts();
    assert(universal_node_gateway_handle_thread_rx(relay, sizeof(relay), peer) ==
           GATEWAY_RX_INGRESS_CONSUMED);
    assert(s_consumer_count == 1u && s_resolver_count == 0u &&
           s_last_classification == GATEWAY_RX_DEVICE_EVENT);
    relay[9] = 3u;
    reset_counts();
    assert(universal_node_gateway_handle_thread_rx(relay, sizeof(relay), peer) ==
           GATEWAY_RX_INGRESS_CONSUMED);
    assert(s_consumer_count == 1u && s_resolver_count == 0u &&
           s_last_classification == GATEWAY_RX_DEVICE_EVENT);

    reset_counts();
    relay[4] = 2u;
    assert(universal_node_gateway_handle_thread_rx(relay, sizeof(relay), peer) ==
           GATEWAY_RX_INGRESS_INVALID);
    assert(s_consumer_count == 0u && s_resolver_count == 0u);

    reset_counts();
    assert(universal_node_gateway_handle_thread_rx(usyn, sizeof(usyn), peer) ==
           GATEWAY_RX_INGRESS_CONSUMED);
    assert(s_consumer_count == 1u && s_resolver_count == 0u &&
           s_last_classification == GATEWAY_RX_SYNC);

    reset_counts();
    s_auth_status = MESH_AUTH_VERIFY_TAG_REJECTED;
    make_control_frame(control_frame, true);
    assert(universal_node_gateway_handle_thread_rx(control_frame, sizeof(control_frame), peer) ==
           GATEWAY_RX_INGRESS_INVALID);
    assert(s_consumer_count == 0u && s_resolver_count == 0u);
    s_auth_status = MESH_AUTH_VERIFY_REPLAY_REJECTED;
    assert(universal_node_gateway_handle_thread_rx(control_frame, sizeof(control_frame), peer) ==
           GATEWAY_RX_INGRESS_INVALID);
    assert(s_consumer_count == 0u && s_resolver_count == 0u);

    reset_counts();
    make_control_frame(control_frame, false);
    assert(universal_node_gateway_handle_thread_rx(control_frame, sizeof(control_frame), peer) ==
           GATEWAY_RX_INGRESS_CONSUMED);
    assert(s_last_classification == GATEWAY_RX_CONTROL_FRAME_UNSUPPORTED &&
           s_consumer_count == 1u && s_resolver_count == 1u);
    s_resolver_accepts = false;
    assert(universal_node_gateway_handle_thread_rx(control_frame, sizeof(control_frame), peer) ==
           GATEWAY_RX_INGRESS_RESOLVER_OBSERVATION_REJECTED);
    assert(s_consumer_count == 2u && s_resolver_count == 2u);
    return 0;
}
