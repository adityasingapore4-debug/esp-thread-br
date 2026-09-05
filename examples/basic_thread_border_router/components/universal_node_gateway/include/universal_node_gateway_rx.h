#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "control_frame.h"
#include "control_frame_parse.h"
#include "device_event_delivery.h"
#include "runtime_state_update.h"

typedef enum {
    GATEWAY_RX_INVALID = 0,
    GATEWAY_RX_RESPONSE,
    GATEWAY_RX_DEVICE_EVENT,
    GATEWAY_RX_SYNC,
    GATEWAY_RX_CONTROL_FRAME_UNSUPPORTED,
} universal_node_gateway_rx_class_t;

typedef enum {
    GATEWAY_RX_RESULT_INVALID_ARGUMENT = 0,
    GATEWAY_RX_RESULT_CLASSIFIED,
} gateway_rx_result_t;

typedef enum {
    GATEWAY_RX_INGRESS_INVALID = 0,
    GATEWAY_RX_INGRESS_CONSUMED,
    GATEWAY_RX_INGRESS_RESOLVER_OBSERVATION_REJECTED,
} gateway_rx_ingress_result_t;

typedef struct {
    const uint8_t *data;
    uint16_t length;
} universal_node_gateway_rx_borrowed_view_t;

/* Classifier output is synchronous and non-retainable. UDEV payload and
 * verified-CF views borrow the caller's inbound buffer and expire when the
 * classifier/consumer call returns. USYN owns its decoded value bytes.
 *
 * A future M7 correlation owner may retain request_id,
 * authenticated_sender_node_id, and bounded timeout/deadline or other
 * correlation metadata. It must never retain canonical_payload.data or
 * device_event.payload. */
typedef struct {
    universal_node_gateway_rx_class_t classification;
    bool authenticated_sender_valid;
    uint16_t authenticated_sender_node_id;
    bool claimed_source_valid;
    uint16_t claimed_source_node_id;
    bool supported_profile;
    uint16_t request_id;
    uint8_t response_command;
    universal_node_gateway_rx_borrowed_view_t canonical_payload;
    control_frame_parsed_t control_frame;
    device_event_delivery_envelope_t device_event;
    runtime_state_update_t sync_update;
} universal_node_gateway_rx_event_t;

/* Pure synchronous classifier. Borrowed views in `out` expire when this
 * classifier's caller finishes consuming the event. */
gateway_rx_result_t universal_node_gateway_classify_rx(
    const uint8_t *payload, uint16_t payload_len,
    universal_node_gateway_rx_event_t *out);

typedef void (*universal_node_gateway_rx_consumer_t)(
    const universal_node_gateway_rx_event_t *event, void *context);

typedef struct {
    uint16_t request_id;
    uint16_t authenticated_remote_node_id;
    uint8_t response_command;
    control_frame_parsed_t response;
    const uint8_t *frame;
    uint16_t frame_len;
} universal_node_gateway_completed_response_t;

typedef void (*universal_node_gateway_response_consumer_t)(
    const universal_node_gateway_completed_response_t *response,
    void *context);

/* Completion views are synchronous and must not be retained. */
bool universal_node_gateway_register_response_consumer(
    universal_node_gateway_response_consumer_t consumer, void *context);

/* Registers at most one synchronous consumer. The consumer must not retain
 * borrowed views from the event after this call returns. */
bool universal_node_gateway_register_rx_consumer(
    universal_node_gateway_rx_consumer_t consumer, void *context);

/* Plain Thread-carrier boundary. `peer_ipv6` is used only for an already
 * authenticated Control Frame endpoint observation. */
gateway_rx_ingress_result_t universal_node_gateway_handle_thread_rx(
    const uint8_t *payload, uint16_t payload_len, const uint8_t peer_ipv6[16]);
