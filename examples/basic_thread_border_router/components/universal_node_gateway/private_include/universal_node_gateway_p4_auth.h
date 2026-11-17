#pragma once
#include "adr073_auth_transaction.h"
#include "p4_access_session.h"
/* INTERNAL secure-carrier integration only, NOT a wire API. Only a future
 * qualified HTTPS ingress may open contexts. No caller authentication flags.
 * All calls serialized; canonical services must outlive this adapter. */
typedef uint64_t gateway_p4_auth_client_t;
void gateway_p4_auth_init(adr073_auth_transaction_table_t *transactions);
gateway_p4_auth_client_t gateway_p4_auth_open(void);
void gateway_p4_auth_close(gateway_p4_auth_client_t client);
bool gateway_p4_auth_process(gateway_p4_auth_client_t client,uint8_t command,
    const uint8_t *request,size_t request_length,uint64_t now_ms,
    uint8_t *response,size_t response_capacity,size_t *response_length,
    p4_access_token_t *issued_token);
bool gateway_p4_auth_validate(const p4_access_token_t *token,uint64_t now_ms);

/* 200 success; 403 canonical authentication rejection; 503 token/session resource failure. */
int gateway_p4_auth_process_status(gateway_p4_auth_client_t client,uint8_t command,
    const uint8_t *request,size_t request_length,uint64_t now_ms,
    uint8_t *response,size_t response_capacity,size_t *response_length,
    p4_access_token_t *issued_token);
void gateway_p4_auth_abort(gateway_p4_auth_client_t client);
