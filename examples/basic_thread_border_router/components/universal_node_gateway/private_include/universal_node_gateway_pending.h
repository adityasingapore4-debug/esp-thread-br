#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GATEWAY_PENDING_CAPACITY 4u
#define GATEWAY_REQUEST_TIMEOUT_MS 30000u

typedef struct {
    bool in_use;
    uint16_t request_id;
    uint8_t expected_response_command;
    uint16_t remote_node_id;
    uint64_t deadline_ms;
} gateway_pending_entry_t;

typedef enum {
    GATEWAY_PENDING_REGISTERED = 0,
    GATEWAY_PENDING_INVALID,
    GATEWAY_PENDING_DUPLICATE_KEY,
    GATEWAY_PENDING_FULL,
} gateway_pending_result_t;

typedef enum {
    GATEWAY_PENDING_MATCHED = 0,
    GATEWAY_PENDING_NOT_FOUND,
    GATEWAY_PENDING_EXPIRED,
} gateway_pending_claim_result_t;

typedef enum {
    GATEWAY_PENDING_MATCH_VALID = 0,
    GATEWAY_PENDING_MATCH_NOT_FOUND,
    GATEWAY_PENDING_MATCH_EXPIRED,
} gateway_pending_match_result_t;

/* This registry owns logical request-correlation metadata only.
 * Response Transfer remains the exclusive owner of fragment/reassembly state. */

gateway_pending_result_t gateway_pending_register(
    uint16_t request_id, uint8_t expected_response_command,
    uint16_t remote_node_id, uint64_t now_ms);
gateway_pending_claim_result_t gateway_pending_claim(
    uint16_t request_id, uint8_t response_command,
    uint16_t authenticated_remote_node_id, uint64_t now_ms,
    gateway_pending_entry_t *out);
gateway_pending_match_result_t gateway_pending_match(
    uint16_t request_id, uint8_t response_command,
    uint16_t authenticated_remote_node_id, uint64_t now_ms,
    gateway_pending_entry_t *out);
bool gateway_pending_cancel(uint16_t request_id,
                            uint8_t expected_response_command,
                            uint16_t remote_node_id);
size_t gateway_pending_expire(uint64_t now_ms);
void gateway_pending_reset(void);
size_t gateway_pending_active_count(void);
