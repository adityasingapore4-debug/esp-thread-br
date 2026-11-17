#pragma once

/* Opt-in G1E development preparation only. This does not load P4 identity,
 * configure network transport, start HTTPS, or expose product routes. */
void universal_node_gateway_p4_auth_runtime_prepare(void);

/* TLS-only test: no auth route, no product routes, and no bearer authority.
 * May return ESP_ERR_INVALID_STATE when FC61 lacks manufacturing/installation
 * readiness; no credentials or persistent state are rewritten. */
#include "esp_err.h"
esp_err_t universal_node_gateway_p4_tls_smoke_start(void);
