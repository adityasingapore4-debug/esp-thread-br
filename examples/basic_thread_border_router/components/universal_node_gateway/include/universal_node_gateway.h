/*
 * Universal Node Gateway lifecycle boundary.
 *
 * This component intentionally exposes no transport, OpenThread, protocol,
 * or business-service interfaces. Those concerns are introduced by later
 * ADR-066 milestones behind this project-owned boundary.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "address_resolver.h"
#include "esp_err.h"

typedef enum {
    UNIVERSAL_NODE_GATEWAY_SUBMIT_ADMITTED = 0,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_NOT_READY,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_INVALID,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_SECURITY_UNAVAILABLE,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_RESOLVER_UNAVAILABLE,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_DESTINATION_UNRESOLVED,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_UNAVAILABLE,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_BACKEND_REJECTED,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_FULL,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_CONFLICT,
    UNIVERSAL_NODE_GATEWAY_SUBMIT_CORRELATION_INVALID,
} universal_node_gateway_submit_result_t;

typedef enum {
    UNIVERSAL_NODE_GATEWAY_THREAD_DISABLED = 0,
    UNIVERSAL_NODE_GATEWAY_THREAD_DETACHED,
    UNIVERSAL_NODE_GATEWAY_THREAD_CHILD,
    UNIVERSAL_NODE_GATEWAY_THREAD_ROUTER,
    UNIVERSAL_NODE_GATEWAY_THREAD_LEADER,
} universal_node_gateway_thread_role_t;

typedef enum {
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NONE = 0,
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NOT_INITIALIZED,
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_INITIALIZATION_FAILED,
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_NOT_READY,
    UNIVERSAL_NODE_GATEWAY_STATUS_ERROR_SUBMIT_REJECTED,
} universal_node_gateway_status_error_t;

typedef struct {
    bool ready;
    bool wifi_connected;
    bool thread_enabled;
    bool thread_attached;
    universal_node_gateway_thread_role_t thread_role;
    bool rloc16_valid;
    uint16_t rloc16;
    universal_node_gateway_status_error_t last_error;
} universal_node_gateway_status_t;

typedef struct {
    bool last_submit_valid;
    universal_node_gateway_submit_result_t last_submit_result;
    bool endpoint_observed;
    resolved_transport_type_t endpoint_transport;
    resolved_destination_type_t endpoint_type;
    uint8_t endpoint_thread_ipv6[THREAD_ENDPOINT_IPV6_SIZE];
} universal_node_gateway_diagnostics_t;

/* Bounded opaque observation seam; this does not define a diagnostic
 * transport or remote logging protocol. */
#define UNIVERSAL_NODE_GATEWAY_DIAGNOSTIC_SOURCE_SIZE 2u
#define UNIVERSAL_NODE_GATEWAY_MAX_DIAGNOSTIC_PAYLOAD 510u

typedef void (*universal_node_gateway_diagnostic_consumer_t)(
    uint16_t source_node_id, const uint8_t *payload, uint16_t payload_length,
    void *context);

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t universal_node_gateway_init(void);
esp_err_t universal_node_gateway_start(void);
esp_err_t universal_node_gateway_stop(void);
bool universal_node_gateway_is_ready(void);
bool universal_node_gateway_get_status(universal_node_gateway_status_t *out_status);
bool universal_node_gateway_get_diagnostics(
    uint16_t node_id, universal_node_gateway_diagnostics_t *out_diagnostics);
universal_node_gateway_submit_result_t universal_node_gateway_submit_frame(
    uint16_t target_node_id, const uint8_t *frame, uint16_t frame_len);
bool universal_node_gateway_register_diagnostic_consumer(
    universal_node_gateway_diagnostic_consumer_t consumer, void *context);
bool universal_node_gateway_diagnostic_receive(uint16_t source_node_id,
                                                const uint8_t *payload,
                                                uint16_t payload_length);

#ifdef __cplusplus
}
#endif
