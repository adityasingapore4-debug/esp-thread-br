#include <assert.h>
#include <stdio.h>

#include "universal_node_gateway_pending.h"

int main(void)
{
    gateway_pending_entry_t entry;
    unsigned terminal_completions = 0u;

    gateway_pending_reset();
    assert(gateway_pending_register(1u, 0x10u, 0x1001u, 100u) ==
           GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_register(1u, 0x10u, 0x1001u, 101u) ==
           GATEWAY_PENDING_DUPLICATE_KEY);
    assert(gateway_pending_active_count() == 1u);
    assert(gateway_pending_match(1u, 0x10u, 0x1001u, 102u, &entry) ==
           GATEWAY_PENDING_MATCH_VALID);
    assert(entry.request_id == 1u && gateway_pending_active_count() == 1u);
    assert(gateway_pending_match(1u, 0x11u, 0x1001u, 102u, NULL) ==
           GATEWAY_PENDING_MATCH_NOT_FOUND);
    assert(gateway_pending_claim(2u, 0x10u, 0x1001u, 102u, NULL) ==
           GATEWAY_PENDING_NOT_FOUND);
    assert(gateway_pending_claim(1u, 0x11u, 0x1001u, 102u, NULL) ==
           GATEWAY_PENDING_NOT_FOUND);
    assert(gateway_pending_claim(1u, 0x10u, 0x1002u, 102u, NULL) ==
           GATEWAY_PENDING_NOT_FOUND);
    assert(gateway_pending_claim(1u, 0x10u, 0x1001u, 102u, &entry) ==
           GATEWAY_PENDING_MATCHED);
    ++terminal_completions;
    assert(entry.request_id == 1u && entry.expected_response_command == 0x10u &&
           entry.remote_node_id == 0x1001u);
    assert(gateway_pending_claim(1u, 0x10u, 0x1001u, 102u, NULL) ==
           GATEWAY_PENDING_NOT_FOUND);
    assert(terminal_completions == 1u);

    assert(gateway_pending_register(2u, 0x10u, 0x1001u, 200u) ==
           GATEWAY_PENDING_REGISTERED);
    assert(!gateway_pending_cancel(2u, 0x11u, 0x1001u));
    assert(gateway_pending_cancel(2u, 0x10u, 0x1001u));
    assert(gateway_pending_active_count() == 0u);

    assert(gateway_pending_register(3u, 0x10u, 0x1001u, 300u) ==
           GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_claim(3u, 0x10u, 0x1001u,
                                 300u + GATEWAY_REQUEST_TIMEOUT_MS, NULL) ==
           GATEWAY_PENDING_EXPIRED);
    assert(gateway_pending_register(4u, 0x10u, 0x1001u, 400u) ==
           GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_expire(400u + GATEWAY_REQUEST_TIMEOUT_MS) == 1u);
    assert(gateway_pending_active_count() == 0u);

    assert(gateway_pending_register(10u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_register(11u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_register(12u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_register(13u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_REGISTERED);
    assert(gateway_pending_register(14u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_FULL);
    assert(gateway_pending_cancel(10u, 0x10u, 0x1001u));
    assert(gateway_pending_register(14u, 0x10u, 0x1001u, 500u) == GATEWAY_PENDING_REGISTERED);
    gateway_pending_reset();
    assert(gateway_pending_active_count() == 0u);
    printf("sizeof(gateway_pending_entry_t)=%zu table_bytes=%zu\n",
           sizeof(gateway_pending_entry_t),
           sizeof(gateway_pending_entry_t) * GATEWAY_PENDING_CAPACITY);
    return 0;
}
