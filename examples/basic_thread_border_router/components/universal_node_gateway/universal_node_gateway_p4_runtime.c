#include "universal_node_gateway_p4_runtime.h"

#include "universal_node_gateway_p4_auth.h"
#include "universal_node_gateway_p4_http.h"
#include "adr073_auth_transaction.h"
#include "local_access_session.h"
#include "p4_access_session.h"

#include <stdbool.h>

/* The dedicated P4 adapter owns these RAM-only instances. Neither BLE nor
 * the Gateway's canonical mesh/session services transfer their ownership. */
static adr073_auth_transaction_table_t s_transactions;
static local_access_session_service_t s_local_sessions;
static bool s_prepared;

void universal_node_gateway_p4_auth_runtime_prepare(void)
{
    if (s_prepared) {
        return;
    }

    /* The canonical ADR-073 services retain verifier and transaction rules.
     * A NULL RNG callback uses provisioning_v2_generate_secret() as before. */
    adr073_auth_transaction_table_init(&s_transactions, NULL, NULL);
    local_access_session_init(&s_local_sessions);
    p4_access_session_init(&s_local_sessions);
    gateway_p4_auth_init(&s_transactions);
    gateway_p4_http_reset();
    s_prepared = true;
}
