#pragma once

#include <stdbool.h>

typedef struct otInstance otInstance;

/* Carrier-private registration boundary; the public Gateway RX API is plain C. */
bool universal_node_gateway_thread_rx_adapter_register(otInstance *instance);
