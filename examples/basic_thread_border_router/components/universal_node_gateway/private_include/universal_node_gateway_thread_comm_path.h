#ifndef UNIVERSAL_NODE_GATEWAY_THREAD_COMM_PATH_H
#define UNIVERSAL_NODE_GATEWAY_THREAD_COMM_PATH_H

#include "address_resolver.h"

#include <openthread/instance.h>

/*
 * Gateway-local CommPath policy for a device-node delivery.  Direct runtime
 * resolution is preferred.  The existing Leader rendezvous path is selected
 * only when this Gateway is an attached non-Leader Thread participant.
 */
address_resolution_result_t universal_node_gateway_thread_select_device_path(
    otInstance *instance, uint16_t node_id, resolved_destination_t *out);

#endif
