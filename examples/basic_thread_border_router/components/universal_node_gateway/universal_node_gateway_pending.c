#include "universal_node_gateway_pending.h"

#include <limits.h>

#include "freertos/FreeRTOS.h"

static gateway_pending_entry_t s_entries[GATEWAY_PENDING_CAPACITY];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool gateway_pending_key_valid(uint16_t request_id,
                                      uint8_t response_command,
                                      uint16_t remote_node_id)
{
    return request_id != 0u && response_command != 0u &&
           remote_node_id != 0u && remote_node_id != UINT16_MAX;
}

static bool gateway_pending_key_matches(const gateway_pending_entry_t *entry,
                                        uint16_t request_id,
                                        uint8_t response_command,
                                        uint16_t remote_node_id)
{
    return entry->in_use && entry->request_id == request_id &&
           entry->expected_response_command == response_command &&
           entry->remote_node_id == remote_node_id;
}

static bool gateway_pending_expired(const gateway_pending_entry_t *entry,
                                    uint64_t now_ms)
{
    return entry->in_use && now_ms >= entry->deadline_ms;
}

static void gateway_pending_clear(gateway_pending_entry_t *entry)
{
    *entry = (gateway_pending_entry_t){0};
}

gateway_pending_result_t gateway_pending_register(
    uint16_t request_id, uint8_t expected_response_command,
    uint16_t remote_node_id, uint64_t now_ms)
{
    if (!gateway_pending_key_valid(request_id, expected_response_command,
                                   remote_node_id) ||
        now_ms > UINT64_MAX - GATEWAY_REQUEST_TIMEOUT_MS) {
        return GATEWAY_PENDING_INVALID;
    }

    portENTER_CRITICAL(&s_lock);
    size_t available = GATEWAY_PENDING_CAPACITY;
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        if (gateway_pending_expired(&s_entries[i], now_ms)) {
            gateway_pending_clear(&s_entries[i]);
        }
        if (gateway_pending_key_matches(&s_entries[i], request_id,
                                        expected_response_command,
                                        remote_node_id)) {
            portEXIT_CRITICAL(&s_lock);
            return GATEWAY_PENDING_DUPLICATE_KEY;
        }
        if (!s_entries[i].in_use && available == GATEWAY_PENDING_CAPACITY) {
            available = i;
        }
    }
    if (available == GATEWAY_PENDING_CAPACITY) {
        portEXIT_CRITICAL(&s_lock);
        return GATEWAY_PENDING_FULL;
    }
    s_entries[available] = (gateway_pending_entry_t){
        .in_use = true,
        .request_id = request_id,
        .expected_response_command = expected_response_command,
        .remote_node_id = remote_node_id,
        .deadline_ms = now_ms + GATEWAY_REQUEST_TIMEOUT_MS,
    };
    portEXIT_CRITICAL(&s_lock);
    return GATEWAY_PENDING_REGISTERED;
}

gateway_pending_claim_result_t gateway_pending_claim(
    uint16_t request_id, uint8_t response_command,
    uint16_t authenticated_remote_node_id, uint64_t now_ms,
    gateway_pending_entry_t *out)
{
    if (!gateway_pending_key_valid(request_id, response_command,
                                   authenticated_remote_node_id)) {
        return GATEWAY_PENDING_NOT_FOUND;
    }

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        if (!gateway_pending_key_matches(&s_entries[i], request_id,
                                         response_command,
                                         authenticated_remote_node_id)) {
            continue;
        }
        if (gateway_pending_expired(&s_entries[i], now_ms)) {
            gateway_pending_clear(&s_entries[i]);
            portEXIT_CRITICAL(&s_lock);
            return GATEWAY_PENDING_EXPIRED;
        }
        if (out != NULL) *out = s_entries[i];
        gateway_pending_clear(&s_entries[i]);
        portEXIT_CRITICAL(&s_lock);
        return GATEWAY_PENDING_MATCHED;
    }
    portEXIT_CRITICAL(&s_lock);
    return GATEWAY_PENDING_NOT_FOUND;
}

gateway_pending_match_result_t gateway_pending_match(
    uint16_t request_id, uint8_t response_command,
    uint16_t authenticated_remote_node_id, uint64_t now_ms,
    gateway_pending_entry_t *out)
{
    if (!gateway_pending_key_valid(request_id, response_command,
                                   authenticated_remote_node_id)) {
        return GATEWAY_PENDING_MATCH_NOT_FOUND;
    }

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        if (!gateway_pending_key_matches(&s_entries[i], request_id,
                                         response_command,
                                         authenticated_remote_node_id)) {
            continue;
        }
        if (gateway_pending_expired(&s_entries[i], now_ms)) {
            gateway_pending_clear(&s_entries[i]);
            portEXIT_CRITICAL(&s_lock);
            return GATEWAY_PENDING_MATCH_EXPIRED;
        }
        if (out != NULL) {
            *out = s_entries[i];
        }
        portEXIT_CRITICAL(&s_lock);
        return GATEWAY_PENDING_MATCH_VALID;
    }
    portEXIT_CRITICAL(&s_lock);
    return GATEWAY_PENDING_MATCH_NOT_FOUND;
}

bool gateway_pending_cancel(uint16_t request_id,
                            uint8_t expected_response_command,
                            uint16_t remote_node_id)
{
    if (!gateway_pending_key_valid(request_id, expected_response_command,
                                   remote_node_id)) return false;

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        if (gateway_pending_key_matches(&s_entries[i], request_id,
                                        expected_response_command,
                                        remote_node_id)) {
            gateway_pending_clear(&s_entries[i]);
            portEXIT_CRITICAL(&s_lock);
            return true;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return false;
}

size_t gateway_pending_expire(uint64_t now_ms)
{
    size_t expired = 0u;

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        if (gateway_pending_expired(&s_entries[i], now_ms)) {
            gateway_pending_clear(&s_entries[i]);
            ++expired;
        }
    }
    portEXIT_CRITICAL(&s_lock);
    return expired;
}

void gateway_pending_reset(void)
{
    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        gateway_pending_clear(&s_entries[i]);
    }
    portEXIT_CRITICAL(&s_lock);
}

size_t gateway_pending_active_count(void)
{
    size_t active = 0u;

    portENTER_CRITICAL(&s_lock);
    for (size_t i = 0u; i < GATEWAY_PENDING_CAPACITY; ++i) {
        active += s_entries[i].in_use ? 1u : 0u;
    }
    portEXIT_CRITICAL(&s_lock);
    return active;
}
