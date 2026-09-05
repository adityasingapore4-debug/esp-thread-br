#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "mesh_auth.h"
#include "universal_node_gateway_rx.h"

static mesh_auth_verify_status_t s_auth_status = MESH_AUTH_VERIFY_AUTHENTICATED;
static bool s_consistent = true;
static unsigned s_auth_calls;
static unsigned s_consistency_calls;

mesh_auth_verify_status_t mesh_auth_verify_inbound_result(
    const uint8_t *frame, size_t length, mesh_auth_verification_t *out)
{
    (void)frame;
    ++s_auth_calls;
    if (out != NULL) {
        *out = (mesh_auth_verification_t){
            .status = s_auth_status,
            .protected_payload_length = length,
            .cryptographically_authenticated =
                s_auth_status == MESH_AUTH_VERIFY_AUTHENTICATED,
            .authenticated_sender_node_id = 0x0405u,
            .authenticated_recipient_node_id = 0x0102u,
        };
    }
    return s_auth_status;
}

bool mesh_auth_control_frame_consistent(const uint8_t *frame, size_t length,
                                        const mesh_auth_verification_t *verification)
{
    (void)frame;
    (void)length;
    (void)verification;
    ++s_consistency_calls;
    return s_consistent;
}

static void reset_security(void)
{
    s_auth_status = MESH_AUTH_VERIFY_AUTHENTICATED;
    s_consistent = true;
    s_auth_calls = 0u;
    s_consistency_calls = 0u;
}

static void make_control_frame(uint8_t frame[CONTROL_FRAME_HEADER_SIZE], bool response)
{
    memset(frame, 0, CONTROL_FRAME_HEADER_SIZE);
    frame[CF_OFF_VERSION] = CONTROL_FRAME_VERSION;
    frame[CF_OFF_FLAGS] = response ? CONTROL_FLAG_RESPONSE : 0u;
    frame[CF_OFF_ORIGIN] = 0x04u;
    frame[CF_OFF_ORIGIN + 1u] = 0x05u;
    frame[CF_OFF_TARGET] = 0x01u;
    frame[CF_OFF_TARGET + 1u] = 0x02u;
    frame[CF_OFF_REQUEST_ID] = 0x12u;
    frame[CF_OFF_REQUEST_ID + 1u] = 0x34u;
}

static void test_udev(void)
{
    uint8_t relay[] = {
        'U','D','E','V', 1u,1u, 28u,0u,
        1u,1u,0u,1u,0x04u,0x05u,1u,1u,1u,1u,
        0u,0u,0u,0u,0u,0u,0u,1u,0u,8u,
        1u,1u,1u,0u,3u,0u,1u,1u,
    };
    uint8_t metadata[] = {
        'U','D','E','V', 1u,1u,20u,0u,
        1u,2u,0u,1u,0x04u,0x05u,0u,0u,0u,0u,
        0u,0u,0u,0u,0u,0u,0u,1u,0u,0u,
    };
    universal_node_gateway_rx_event_t event;

    reset_security();
    assert(universal_node_gateway_classify_rx(relay, sizeof(relay), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_DEVICE_EVENT && event.supported_profile);
    assert(event.claimed_source_valid && event.claimed_source_node_id == 0x0405u);
    assert(!event.authenticated_sender_valid && s_auth_calls == 0u);
    assert(universal_node_gateway_classify_rx(metadata, sizeof(metadata), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_DEVICE_EVENT && event.supported_profile);

    relay[9] = 3u;
    assert(universal_node_gateway_classify_rx(relay, sizeof(relay), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_DEVICE_EVENT && !event.supported_profile);
    relay[9] = 1u;
    relay[35] = 2u;
    assert(universal_node_gateway_classify_rx(relay, sizeof(relay), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_INVALID && s_auth_calls == 0u);
}

static void test_usyn(void)
{
    uint8_t usyn[] = {
        'U','S','Y','N',1u,1u,1u, 1u,0u,0x34u,0x12u,5u,0x05u,0x04u,3u,2u,
        0xefu,0xbeu,
    };
    universal_node_gateway_rx_event_t event;

    reset_security();
    assert(universal_node_gateway_classify_rx(usyn, sizeof(usyn), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_SYNC && event.claimed_source_valid);
    assert(event.claimed_source_node_id == 0x0405u && !event.authenticated_sender_valid);
    assert(event.sync_update.value.u16 == 0xbeefu && s_auth_calls == 0u);
    usyn[15] = 3u;
    assert(universal_node_gateway_classify_rx(usyn, sizeof(usyn), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_INVALID && s_auth_calls == 0u);
}

static void test_control_frames(void)
{
    uint8_t frame[CONTROL_FRAME_HEADER_SIZE];
    universal_node_gateway_rx_event_t event;

    reset_security();
    make_control_frame(frame, true);
    assert(universal_node_gateway_classify_rx(frame, sizeof(frame), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_RESPONSE);
    assert(event.authenticated_sender_valid && event.authenticated_sender_node_id == 0x0405u);
    assert(event.request_id == 0x1234u && event.canonical_payload.data == frame);
    assert(event.canonical_payload.length == sizeof(frame));

    make_control_frame(frame, false);
    assert(universal_node_gateway_classify_rx(frame, sizeof(frame), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED);
    assert(event.classification == GATEWAY_RX_CONTROL_FRAME_UNSUPPORTED);
    s_auth_status = MESH_AUTH_VERIFY_TAG_REJECTED;
    assert(universal_node_gateway_classify_rx(frame, sizeof(frame), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED && event.classification == GATEWAY_RX_INVALID);
    s_auth_status = MESH_AUTH_VERIFY_REPLAY_REJECTED;
    assert(universal_node_gateway_classify_rx(frame, sizeof(frame), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED && event.classification == GATEWAY_RX_INVALID);
    s_auth_status = MESH_AUTH_VERIFY_AUTHENTICATED;
    s_consistent = false;
    assert(universal_node_gateway_classify_rx(frame, sizeof(frame), &event) ==
           GATEWAY_RX_RESULT_CLASSIFIED && event.classification == GATEWAY_RX_INVALID);
}

int main(void)
{
    universal_node_gateway_rx_event_t event = { .classification = GATEWAY_RX_RESPONSE };
    uint8_t byte = 0u;
    uint8_t oversized[PROTECTED_WIRE_FRAME_CAPACITY + 1u] = {0};

    assert(universal_node_gateway_classify_rx(NULL, 1u, &event) ==
           GATEWAY_RX_RESULT_INVALID_ARGUMENT && event.classification == GATEWAY_RX_INVALID);
    assert(universal_node_gateway_classify_rx(&byte, 0u, &event) ==
           GATEWAY_RX_RESULT_INVALID_ARGUMENT);
    assert(universal_node_gateway_classify_rx(&byte, 1u, NULL) ==
           GATEWAY_RX_RESULT_INVALID_ARGUMENT);
    assert(universal_node_gateway_classify_rx(oversized, sizeof(oversized), &event) ==
           GATEWAY_RX_RESULT_INVALID_ARGUMENT);
    test_udev();
    test_usyn();
    test_control_frames();
    return 0;
}
