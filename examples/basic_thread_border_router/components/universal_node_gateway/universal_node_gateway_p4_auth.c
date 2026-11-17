#include "universal_node_gateway_p4_auth.h"
#include "adr073_auth_start_service.h"
#include "adr073_auth_proof_service.h"
#include <string.h>
#include "provisioning_v2.h"
#define CLIENT_CAPACITY ADR073_AUTH_TRANSACTION_CAPACITY
#define AUTH_START_COMMAND 0x3eu
#define AUTH_PROOF_COMMAND 0x3fu
#define MAX_AUTH_REQUEST (6u+SCRAM_MESSAGE_MAX+ADR073_AUTH_BINDING_SIZE)
typedef struct {gateway_p4_auth_client_t ref;uint8_t transaction_id[16];} client_t;
static client_t clients[CLIENT_CAPACITY];
static gateway_p4_auth_client_t next_ref;
static adr073_auth_transaction_table_t *table;
static void wipe(void*p,size_t n){volatile uint8_t*b=p;while(n--)*b++=0;}
static client_t *find(gateway_p4_auth_client_t ref){if(!ref)return NULL;for(size_t i=0;i<CLIENT_CAPACITY;i++)if(clients[i].ref==ref)return &clients[i];return NULL;}
static void abort_pending(client_t *client){
 if(table){adr073_auth_transaction_t *t=adr073_auth_transaction_lookup(table,client->transaction_id,0);if(t)adr073_auth_transaction_abort(t);}
 wipe(client->transaction_id,sizeof(client->transaction_id));
}
void gateway_p4_auth_close(gateway_p4_auth_client_t ref){client_t*c=find(ref);if(c){abort_pending(c);c->ref=0;}}
void gateway_p4_auth_init(adr073_auth_transaction_table_t *transactions){
 for(size_t i=0;i<CLIENT_CAPACITY;i++){abort_pending(&clients[i]);clients[i].ref=0;}
 table=transactions;
}
gateway_p4_auth_client_t gateway_p4_auth_open(void){
 if(!table||next_ref==UINT64_MAX)return 0;
 for(size_t i=0;i<CLIENT_CAPACITY;i++)if(!clients[i].ref){clients[i].ref=++next_ref;return clients[i].ref;}
 return 0;
}
/* Observe allocation RNG failure without changing the canonical service or its
 * transaction allocation rules. Table calls are serialized by this owner. */
typedef struct {adr073_auth_rng_t rng;void *context;bool called,viable;} rng_probe_t;
static bool allocation_rng(uint8_t *out,size_t n,void *context){
 rng_probe_t *probe=context;probe->called=true;
 bool ok=probe->rng?probe->rng(out,n,probe->context):provisioning_v2_generate_secret(out,n);
 uint8_t nonzero=0;if(ok)for(size_t i=0;i<n;i++)nonzero|=out[i];
 bool viable=ok&&nonzero;
 for(size_t i=0;i<ADR073_AUTH_TRANSACTION_CAPACITY;i++)if(ok&&table->entries[i].state&&n==16&&!memcmp(out,table->entries[i].id,16))viable=false;
 probe->viable|=viable;return ok;
}
int gateway_p4_auth_process_status(gateway_p4_auth_client_t ref,uint8_t command,
 const uint8_t *request,size_t request_length,uint64_t now,uint8_t *response,
 size_t capacity,size_t *length,p4_access_token_t *token){
 if(length)*length=0;
 if(token)wipe(token,sizeof(*token));
 client_t *client=find(ref);
 if(!client||!table||!request||!response||!length||!token||request_length>MAX_AUTH_REQUEST||now>UINT64_MAX-ADR073_AUTH_TRANSACTION_TIMEOUT_MS)return 403;
 if(command==AUTH_START_COMMAND){
  adr073_auth_start_request_t parsed;adr073_auth_start_response_t started;
  if(!adr073_auth_start_request_parse(request,request_length,&parsed)||parsed.scope!=ADR073_AUTH_SCOPE_LOCAL_SESSION)return 403;
  abort_pending(client);
  adr073_auth_transaction_cleanup_expired(table,now);
  bool available=false;
  for(size_t i=0;i<ADR073_AUTH_TRANSACTION_CAPACITY;i++)available|=table->entries[i].state==ADR073_AUTH_TRANSACTION_FREE;
  if(!available)return 503;
  rng_probe_t probe={.rng=table->rng,.context=table->rng_context};
  table->rng=allocation_rng;table->rng_context=&probe;
  bool started_ok=adr073_auth_start_process(table,request,request_length,now,NULL,response,capacity,length);
  table->rng=probe.rng;table->rng_context=probe.context;
  if(!started_ok)return probe.called&&!probe.viable?503:403;
  if(!adr073_auth_start_response_parse(response,*length,&started)){*length=0;return 403;}
  memcpy(client->transaction_id,started.transaction_id,16);return 200;
 }
 if(command==AUTH_PROOF_COMMAND){
  uint8_t id[16];const uint8_t *final;size_t final_length;
  if(!adr073_auth_proof_request_parts_parse(request,request_length,id,&final,&final_length)||memcmp(id,client->transaction_id,16))return 403;
  adr073_auth_transaction_t *transaction=adr073_auth_transaction_lookup(table,id,now);
  if(!transaction||transaction->scope!=ADR073_AUTH_SCOPE_LOCAL_SESSION||transaction->domain!=AUTH_DOMAIN_LOCAL){abort_pending(client);return 403;}
  if(!adr073_auth_proof_process(table,request,request_length,now,response,capacity,length)){
   abort_pending(client);*length=0;wipe(response,capacity);return 403;
  }
  if(!p4_access_session_issue(table,id,now,token)){
   abort_pending(client);*length=0;wipe(response,capacity);return 503;
  }
  wipe(client->transaction_id,16);return 200;
 }
 /* No path to a command router, transport, or global response queue. */
 return 403;
}
bool gateway_p4_auth_validate(const p4_access_token_t *token,uint64_t now){return p4_access_session_validate(token,now);}

bool gateway_p4_auth_process(gateway_p4_auth_client_t ref,uint8_t command,
 const uint8_t *request,size_t request_length,uint64_t now,uint8_t *response,
 size_t capacity,size_t *length,p4_access_token_t *token){
 return gateway_p4_auth_process_status(ref,command,request,request_length,now,response,capacity,length,token)==200;
}
void gateway_p4_auth_abort(gateway_p4_auth_client_t ref){client_t *c=find(ref);if(c)abort_pending(c);}
