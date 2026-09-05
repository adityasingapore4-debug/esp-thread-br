#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "control_frame.h"
#include "mesh_auth.h"
#include "mesh_session.h"
#include "node_identity.h"
#include "security_root.h"
#include "transport.h"
#include "universal_node_gateway.h"
#include "universal_node_gateway_pending.h"

static transport_result_t s_transport_result;
static unsigned s_transport_calls;
static bool s_expect_pending;

int64_t esp_timer_get_time(void) { return 1000000; }
void node_identity_init(void) {}
uint16_t node_identity_get(void) { return 0x0102u; }
void security_root_init(void) {}
bool security_root_has_root(void) { return true; }
bool mesh_auth_init(uint16_t local_node_id) { return local_node_id == 0x0102u; }
bool mesh_session_init(uint16_t local_node_id) { return local_node_id == 0x0102u; }
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
    frame[frame_len] = 0xa5u;
    *protected_len = frame_len + 1u;
    return true;
}
transport_result_t transport_send_result(const delivery_descriptor_t *delivery,
                                         const uint8_t *frame, uint16_t frame_len)
{
    (void)frame;
    (void)frame_len;
    assert(delivery->type == DELIVERY_DEVICE_NODE);
    ++s_transport_calls;
    if (s_expect_pending && s_transport_result == TRANSPORT_RESULT_ADMITTED) {
        assert(gateway_pending_active_count() > 0u);
    }
    return s_transport_result;
}

static void frame(uint8_t out[CONTROL_FRAME_HEADER_SIZE], uint16_t target,
                  uint16_t request_id, uint8_t command, uint8_t flags)
{
    memset(out, 0, CONTROL_FRAME_HEADER_SIZE);
    out[CF_OFF_VERSION] = CONTROL_FRAME_VERSION;
    out[CF_OFF_FLAGS] = flags;
    out[CF_OFF_ORIGIN] = 0x01u; out[CF_OFF_ORIGIN + 1u] = 0x02u;
    out[CF_OFF_TARGET] = (uint8_t)(target >> 8u);
    out[CF_OFF_TARGET + 1u] = (uint8_t)target;
    out[CF_OFF_COMMAND] = command;
    out[CF_OFF_REQUEST_ID] = (uint8_t)(request_id >> 8u);
    out[CF_OFF_REQUEST_ID + 1u] = (uint8_t)request_id;
}

int main(void)
{
    uint8_t request[CONTROL_FRAME_HEADER_SIZE];

    assert(universal_node_gateway_init() == 0);
    assert(universal_node_gateway_start() == 0);
    gateway_pending_reset();

    s_transport_result = TRANSPORT_RESULT_ADMITTED;
    s_transport_calls = 0u;
    s_expect_pending = true;
    frame(request, 0x1001u, 1u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1001u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    assert(s_transport_calls == 1u && gateway_pending_active_count() == 1u);
    gateway_pending_reset();

    s_transport_result = TRANSPORT_RESULT_UNRESOLVED;
    s_expect_pending = true;
    frame(request, 0x1001u, 2u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1001u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_DESTINATION_UNRESOLVED);
    assert(gateway_pending_active_count() == 0u);
    s_transport_result = TRANSPORT_RESULT_BACKEND_REJECTED;
    frame(request, 0x1001u, 3u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1001u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_REJECTED);
    assert(gateway_pending_active_count() == 0u);

    gateway_pending_reset();
    s_transport_result = TRANSPORT_RESULT_ADMITTED;
    s_expect_pending = true;
    for (uint16_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        frame(request, (uint16_t)(0x1100u + i), (uint16_t)(10u + i),
              CMD_QUERY_RESOURCE, 0u);
        assert(universal_node_gateway_submit_frame((uint16_t)(0x1100u + i), request,
                                                    sizeof(request)) ==
               UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    }
    s_transport_calls = 0u;
    frame(request, 0x1200u, 20u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1200u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_FULL);
    assert(s_transport_calls == 0u && gateway_pending_active_count() == 4u);
    assert(gateway_pending_cancel(10u, CMD_QUERY_RESOURCE, 0x1100u));

    frame(request, 0x1200u, 20u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1200u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    s_transport_calls = 0u;
    assert(universal_node_gateway_submit_frame(0x1200u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_CONFLICT);
    assert(s_transport_calls == 0u);

    gateway_pending_reset();
    s_transport_calls = 0u;
    s_expect_pending = false;
    frame(request, 0x1300u, 30u, CMD_SENSOR_REPORT, 0u);
    assert(universal_node_gateway_submit_frame(0x1300u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    assert(s_transport_calls == 1u && gateway_pending_active_count() == 0u);
    frame(request, 0x1300u, 31u, CMD_QUERY_RESOURCE, CONTROL_FLAG_RESPONSE);
    assert(universal_node_gateway_submit_frame(0x1300u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED);
    assert(gateway_pending_active_count() == 0u);
    s_transport_calls = 0u;
    frame(request, 0x1300u, 0u, CMD_QUERY_RESOURCE, 0u);
    assert(universal_node_gateway_submit_frame(0x1300u, request, sizeof(request)) ==
           UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_INVALID);
    assert(s_transport_calls == 0u && gateway_pending_active_count() == 0u);
    return 0;
}
