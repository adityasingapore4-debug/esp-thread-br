#define main adr074_unused_main
#include "local_access_session_test.c"
#undef main
#include "universal_node_gateway_p4_auth.h"
#include "p4_access_session_test.h"
static gateway_p4_auth_client_t foreign_client;
static bool random_token(uint8_t *out,size_t n,void *ctx){static uint8_t next;(void)ctx;memset(out,0,n);out[0]=++next;return true;}
static p4_access_token_t adapter_authenticate(gateway_p4_auth_client_t client,adr073_auth_transaction_table_t *table,
                                                 adr073_auth_scope_t scope, auth_domain_t domain,
                                                 const char *password, uint64_t now,
                                                 uint8_t transaction_id[16])
{
    static const uint8_t zero[16] = {0};
    static const uint8_t present[16] = {1u};
    auth_domain_scram_verifier_t verifier;
    adr073_auth_start_request_t start = { .scope = scope };
    adr073_auth_start_response_t started;
    adr073_auth_proof_request_t proof = {0};
    p4_access_token_t issued;
    uint8_t request[SCRAM_MESSAGE_MAX + 87u], response[SCRAM_MESSAGE_MAX + 19u], binding[81] = {0};
    char un_op[45], client_first[SCRAM_MESSAGE_MAX + 1u], client_first_bare[SCRAM_MESSAGE_MAX + 1u];
    char without_proof[SCRAM_MESSAGE_MAX + 1u], client_final[SCRAM_MESSAGE_MAX + 1u];
    char nonce[8];
    size_t request_length, response_length;
    uint32_t generation;
    scram_server_first_t server_first;

    assert(auth_domain_verifier_load(domain, &verifier));
    assert(auth_domain_generation_get(domain, &generation));
    if (scope == ADR073_AUTH_SCOPE_LOCAL_SESSION) {
        strcpy(client_first, "n,,n=u,r=n"); strcpy(client_first_bare, "n=u,r=n");
    } else {
        descriptor(domain, generation, domain == AUTH_DOMAIN_ADMIN ? zero : present, binding, un_op);
        assert(snprintf(client_first, sizeof(client_first), "n,,n=u,r=n,un-op=%s", un_op) > 0);
        assert(snprintf(client_first_bare, sizeof(client_first_bare), "n=u,r=n,un-op=%s", un_op) > 0);
        memcpy(start.binding, binding, sizeof(binding));
    }
    start.client_first_length = strlen(client_first);
    memcpy(start.client_first, client_first, start.client_first_length);
    assert(adr073_auth_start_request_encode(&start, request, sizeof(request), &request_length));
    assert(snprintf(nonce, sizeof(nonce), "s%u", (unsigned)(now % 100u)) > 0);
    assert(gateway_p4_auth_process(client, 0x3e, request, request_length, now,
                                     response, sizeof(response), &response_length, &issued));
    assert(adr073_auth_start_response_parse(response, response_length, &started));
    assert(scram_server_first_parse(started.server_first, started.server_first_length, &server_first));
    assert(snprintf(without_proof, sizeof(without_proof), "c=biws,r=%s", server_first.nonce) > 0);
    assert(scram_test_client_final(password, &verifier, client_first_bare,
                                   (const char *)started.server_first, without_proof, client_final));
    memcpy(proof.transaction_id, started.transaction_id, sizeof(proof.transaction_id));
    proof.client_final_length = strlen(client_final);
    memcpy(proof.client_final, client_final, proof.client_final_length);
    assert(adr073_auth_proof_request_encode(&proof, request, sizeof(request), &request_length));
    if(foreign_client && foreign_client!=client) {
      p4_access_token_t denied;size_t denied_length;
      assert(!gateway_p4_auth_process(foreign_client,0x3f,request,request_length,now+1u,response,sizeof(response),&denied_length,&denied));
    }
    assert(gateway_p4_auth_process(client, 0x3f, request, request_length, now + 1u,
                                     response, sizeof(response), &response_length, &issued));
    assert(!gateway_p4_auth_process(client,0x3f,request,request_length,now+2u,response,sizeof(response),&response_length,&(p4_access_token_t){0}));
    (void)table; (void)transaction_id;
    return issued;
}

#ifndef P4_ADAPTER_TEST_MAIN
#define P4_ADAPTER_TEST_MAIN main
#endif
int P4_ADAPTER_TEST_MAIN(void){
 setup();enroll(AUTH_DOMAIN_LOCAL,"local password");
 adr073_auth_transaction_table_t transactions;local_access_session_service_t sessions;
 rng_t random={0};uint8_t id[16];
 adr073_auth_transaction_table_init(&transactions,rng,&random);local_access_session_init(&sessions);
 p4_access_session_init(&sessions);p4_access_session_test_rng(random_token,NULL);gateway_p4_auth_init(&transactions);
 gateway_p4_auth_client_t a=gateway_p4_auth_open(),b=gateway_p4_auth_open();assert(a&&b&&a!=b);foreign_client=b;
 p4_access_token_t first=adapter_authenticate(a,&transactions,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local password",10,id);
 p4_access_token_t second=adapter_authenticate(b,&transactions,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local password",20,id);
 assert(memcmp(&first,&second,sizeof(first)));assert(gateway_p4_auth_validate(&first,25));assert(gateway_p4_auth_validate(&second,25));
 uint8_t payload[32]={0},response[600];size_t n=1;p4_access_token_t token;
 assert(!gateway_p4_auth_process(a,0x01,payload,1,30,response,sizeof(response),&n,&token)&&n==0);
 assert(!gateway_p4_auth_process(b,0x3f,payload,sizeof(payload),30,response,sizeof(response),&n,&token));
 gateway_p4_auth_close(a);assert(gateway_p4_auth_validate(&first,31));
 gateway_p4_auth_client_t reconnected=gateway_p4_auth_open();assert(reconnected&&reconnected!=a);
 assert(!gateway_p4_auth_process(a,0x3e,payload,1,32,response,sizeof(response),&n,&token));
 p4_access_session_revoke(&first);assert(!gateway_p4_auth_validate(&first,33));assert(gateway_p4_auth_validate(&second,33));
 gateway_p4_auth_init(NULL);p4_access_session_reset();return 0;
}
