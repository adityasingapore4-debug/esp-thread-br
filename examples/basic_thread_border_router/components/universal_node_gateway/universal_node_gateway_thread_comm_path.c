#include "universal_node_gateway_thread_comm_path.h"

#include "thread_runtime_resolution.h"

#include <openthread/ip6.h>
#include <openthread/thread.h>

#include <string.h>

address_resolution_result_t universal_node_gateway_thread_select_device_path(
    otInstance *instance, uint16_t node_id, resolved_destination_t *out)
{
    if (instance == NULL || out == NULL || node_id == 0u || node_id == UINT16_MAX) {
        return ADDRESS_RESOLUTION_INVALID;
    }

    memset(out, 0, sizeof(*out));
    if (thread_runtime_resolve_device_node(instance, node_id, out)) {
        return ADDRESS_RESOLUTION_RESOLVED;
    }

    /* This is CommPath policy, deliberately outside shared runtime resolution.
     * A Leader has no distinct rendezvous peer and must fail rather than send
     * relay/cmd to itself. */
    const otDeviceRole role = otThreadGetDeviceRole(instance);
    if (role == OT_DEVICE_ROLE_LEADER ||
        (role != OT_DEVICE_ROLE_ROUTER && role != OT_DEVICE_ROLE_CHILD)) {
        return ADDRESS_RESOLUTION_UNRESOLVED;
    }

    otIp6Address leader;
    if (otThreadGetLeaderRloc(instance, &leader) != OT_ERROR_NONE) {
        return ADDRESS_RESOLUTION_UNRESOLVED;
    }

    out->transport = ADDRESS_TRANSPORT_THREAD;
    out->type = RESOLVED_DESTINATION_THREAD_IPV6;
    memcpy(out->destination.thread_ipv6, leader.mFields.m8,
           THREAD_ENDPOINT_IPV6_SIZE);
    return ADDRESS_RESOLUTION_RESOLVED;
}
