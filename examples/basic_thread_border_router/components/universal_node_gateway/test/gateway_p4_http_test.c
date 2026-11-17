#define P4_ADAPTER_TEST_MAIN p4_adapter_regressions
#include "gateway_p4_auth_adapter_test.c"
#include "universal_node_gateway_p4_http.h"
static bool fail_rng(uint8_t *out,size_t n,void *ctx){(void)out;(void)n;(void)ctx;return false;}
static gateway_p4_http_request_t bearer(const p4_access_token_t *token){
 gateway_p4_http_request_t r={.has_authorization=true};memcpy(r.authorization,"Bearer ",7);gateway_p4_http_token_encode(token,r.authorization+7);return r;
}
static void response_contract(void){
 const int statuses[]={200,400,401,403,404,405,413,415,429,503};
 for(size_t i=0;i<sizeof(statuses)/sizeof(statuses[0]);i++){
  int status=statuses[i];
  gateway_p4_http_response_t r={.status=status};uint8_t wire[1024];size_t n=gateway_p4_http_serialize(&r,wire,sizeof(wire));assert(n>0);wire[n]=0;
  assert(strstr((char*)wire,"Cache-Control: no-store\r\n"));assert(strstr((char*)wire,"Content-Length: 0\r\n"));assert(!strstr((char*)wire,"P4-Access-Token"));
  if(status==401)assert(strstr((char*)wire,"WWW-Authenticate: Bearer realm=\"p4\""));
  if(status==405)assert(strstr((char*)wire,"Allow: POST"));
 }
}
int main(void){
 assert(p4_adapter_regressions()==0);response_contract();
 /* All final sextets are exercised; only four unused-bit variants are canonical. */
 p4_access_token_t token,decoded;
 for(unsigned v=0;v<256;v++){
  memset(&token,(int)v,sizeof(token));char text[44];gateway_p4_http_token_encode(&token,text);
  assert(strlen(text)==43&&gateway_p4_http_token_decode(text,43,&decoded)&&!memcmp(&token,&decoded,sizeof(token)));
  char last=text[42];text[42]=(last=='_'?'A':(char)(last+1));
  assert(!gateway_p4_http_token_decode(text,43,&decoded));
 }
 const char *headers[]={
  "POST /un/p4/auth HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Type: application/octet-stream\r\nX: a\nInjected: value\r\n\r\n",
  "POST /un/p4/auth HTTP/1.1\r\nHost: x\r\nContent-Length: +1\r\nContent-Type: application/octet-stream\r\n\r\n",
  "POST /un/p4/auth HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Type: application/octet-stream\r\n\r\nX: after-end\r\n\r\n"
 };
 for(size_t i=0;i<sizeof(headers)/sizeof(headers[0]);i++){gateway_p4_http_request_t r;assert(gateway_p4_http_headers((const uint8_t*)headers[i],strlen(headers[i]),&r)==400);}
 char raw_bearer[]="POST /un/p4/auth HTTP/1.1\r\nAuthorization: Bearer AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\r\n\r\n";
 gateway_p4_http_hide_authorization((uint8_t*)raw_bearer,strlen(raw_bearer));assert(!strstr(raw_bearer,"Bearer"));
 setup();enroll(AUTH_DOMAIN_LOCAL,"local password");
 adr073_auth_transaction_table_t table;local_access_session_service_t sessions;rng_t random={0};uint8_t id[16];
 adr073_auth_transaction_table_init(&table,rng,&random);local_access_session_init(&sessions);p4_access_session_init(&sessions);p4_access_session_test_rng(random_token,NULL);gateway_p4_auth_init(&table);gateway_p4_http_reset();foreign_client=0;
 gateway_p4_http_connection_t c;assert(gateway_p4_http_open(&c));
 token=adapter_authenticate(c.client,&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local password",100,id);
 gateway_p4_http_request_t r=bearer(&token);assert(gateway_p4_http_validate(&r,103)==200);
 /* Equivalent deterministic fd reuse: same connection storage, new generation. */
 gateway_p4_auth_client_t stale=c.client;gateway_p4_http_close(&c);assert(gateway_p4_http_open(&c));assert(c.client!=stale);
 assert(gateway_p4_http_validate(&r,104)==200);
 assert(gateway_p4_http_validate(&r,102+LOCAL_ACCESS_SESSION_TIMEOUT_MS)==401);
 token=adapter_authenticate(c.client,&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local password",400000,id);r=bearer(&token);
 enroll(AUTH_DOMAIN_LOCAL,"local replacement");assert(gateway_p4_http_validate(&r,400003)==401);
 token=adapter_authenticate(c.client,&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local replacement",400010,id);r=bearer(&token);
 local_access_session_reset(&sessions);assert(gateway_p4_http_validate(&r,400013)==401);
 token=adapter_authenticate(c.client,&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local replacement",400020,id);r=bearer(&token);
 gateway_p4_http_response_t delivery={.status=200,.issued=token};gateway_p4_http_token_encode(&token,delivery.token);
 gateway_p4_http_delivery(&delivery,false);assert(gateway_p4_http_validate(&r,400023)==401);
 token=adapter_authenticate(c.client,&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local replacement",400030,id);r=bearer(&token);
 p4_access_session_reset();assert(gateway_p4_http_validate(&r,400033)==401);
 /* Canonical proof success followed by token RNG failure returns no authority. */
 authenticate(&table,ADR073_AUTH_SCOPE_LOCAL_SESSION,AUTH_DOMAIN_LOCAL,"local replacement",400040,id);
 p4_access_session_test_rng(fail_rng,NULL);assert(!p4_access_session_issue(&table,id,400042,&token));
 p4_access_session_test_rng(random_token,NULL);adr073_auth_transaction_table_reset(&table);
 /* Allocation RNG failure is projected as a resource failure, not proof failure. */
 adr073_auth_start_request_t start={.scope=ADR073_AUTH_SCOPE_LOCAL_SESSION,.client_first_length=10};memcpy(start.client_first,"n,,n=u,r=n",10);
 uint8_t request[533],response[533];size_t n,len;
 assert(adr073_auth_start_request_encode(&start,request+1,532,&n));request[0]=0x3e;
 table.rng=fail_rng;gateway_p4_http_request_t auth={.body_length=n+1};gateway_p4_http_response_t result;
 gateway_p4_http_auth(&c,&auth,request,n+1,500000,&result);assert(result.status==503&&result.body_length==0&&!result.token[0]);table.rng=rng;
 /* A correctly framed proof cannot revive an expired owned transaction. */
 gateway_p4_http_auth(&c,&auth,request,n+1,500010,&result);assert(result.status==200);
 uint8_t expired[64]={0x3f,1};memcpy(expired+2,result.body+2,16);
 const char *fake_final="c=biws,r=n,p=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
 size_t final_n=strlen(fake_final);expired[18]=(uint8_t)(final_n>>8);expired[19]=(uint8_t)final_n;
 /* Use a full-sized buffer: the maximum codec bound is checked before service dispatch. */
 uint8_t expired_request[128];memcpy(expired_request,expired,20);memcpy(expired_request+20,fake_final,final_n);
 gateway_p4_http_request_t expired_header={.body_length=20+final_n};
 gateway_p4_http_auth(&c,&expired_header,expired_request,20+final_n,530011,&result);assert(result.status==403&&!result.token[0]);
 /* Rolling-window expiry restores budget; a rejected start does not bypass it. */
 for(unsigned i=0;i<4;i++){gateway_p4_http_auth(&c,&auth,request,n+1,600000+i,&result);assert(result.status==200);}
 gateway_p4_http_auth(&c,&auth,request,n+1,600005,&result);assert(result.status==429);
 gateway_p4_http_auth(&c,&auth,request,n+1,660000,&result);assert(result.status==200);
 /* Same bounded connection object cannot revive a retired client reference. */
 assert(!gateway_p4_auth_process(stale,0x3e,request+1,n,500001,response,sizeof(response),&len,&token));
 gateway_p4_http_close(&c);gateway_p4_auth_init(NULL);p4_access_session_reset();
 puts("PASS raw headers, canonical tokens, expiry/rotation/revocation/reset, failed delivery, allocation RNG, stale context");return 0;
}
