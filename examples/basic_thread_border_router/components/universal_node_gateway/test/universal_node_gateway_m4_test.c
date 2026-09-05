#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "control_frame.h"
#include "control_frame_build.h"
#include "mesh_auth.h"
#include "mesh_session.h"
#include "node_identity.h"
#include "security_root.h"
#include "transport.h"
#include "universal_node_gateway.h"

static unsigned s_protect_calls;
static transport_result_t s_transport_result;
static delivery_descriptor_t s_delivery;
static uint8_t s_transport_frame[PROTECTED_WIRE_FRAME_CAPACITY];
static uint16_t s_transport_length;

int64_t esp_timer_get_time(void) { return 0; }
void node_identity_init(void) {}
uint16_t node_identity_get(void) { return 0x72efu; }
void security_root_init(void) {}
bool security_root_has_root(void) { return true; }
bool mesh_auth_init(uint16_t local_node_id) { return local_node_id == 0x72efu; }
bool mesh_session_init(uint16_t local_node_id) { return local_node_id == 0x72efu; }
bool mesh_session_is_initialized(void) { return true; }
mesh_auth_verify_status_t mesh_auth_verify_inbound_result(
    const uint8_t *frame, size_t frame_len, mesh_auth_verification_t *result)
{
    (void)frame;
    (void)frame_len;
    (void)result;
    return MESH_AUTH_VERIFY_FORMAT_REJECTED;
}
bool mesh_auth_protect_outbound(uint8_t *frame, size_t frame_len,
                                size_t capacity, size_t *protected_len)
{
    assert(frame_len + 1u <= capacity);
    ++s_protect_calls;
    frame[frame_len] = 0xa5u;
    *protected_len = frame_len + 1u;
    return true;
}
transport_result_t transport_send_result(const delivery_descriptor_t *descriptor,
                                         const uint8_t *frame, uint16_t frame_len)
{
    s_delivery = *descriptor;
    s_transport_length = frame_len;
    memcpy(s_transport_frame, frame, frame_len);
    return s_transport_result;
}

int main(void)
{
    uint8_t frame[CONTROL_FRAME_MAX_SIZE];
    const uint16_t length = control_frame_empty_frame_length();

    control_frame_build_command(frame, 0x72efu, 0x9782u, 1u, 0u, 0u, 0u);
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_NOT_READY);
    assert(universal_node_gateway_init() == 0);
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_NOT_READY);
    assert(universal_node_gateway_start() == 0);
    assert(universal_node_gateway_submit_frame(0u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_INVALID);
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length - 1u) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_INVALID);

    s_transport_result = TRANSPORT_RESULT_RESOLVER_UNAVAILABLE;
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_RESOLVER_UNAVAILABLE);
    assert(s_protect_calls == 1u);
    assert(s_delivery.type == DELIVERY_DEVICE_NODE);
    assert(s_delivery.destination.node_id == 0x9782u);
    assert(s_transport_length == length + 1u);
    assert(memcmp(s_transport_frame, frame, length) == 0);
    assert(s_transport_frame[length] == 0xa5u);

    s_transport_result = TRANSPORT_RESULT_UNRESOLVED;
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_DESTINATION_UNRESOLVED);
    s_transport_result = TRANSPORT_RESULT_BACKEND_UNAVAILABLE;
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_UNAVAILABLE);
    s_transport_result = TRANSPORT_RESULT_BACKEND_REJECTED;
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_REJECTED);
    s_transport_result = TRANSPORT_RESULT_ADMITTED;
    assert(universal_node_gateway_submit_frame(0x9782u, frame, length) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    assert(s_protect_calls == 5u);
    return 0;
}
