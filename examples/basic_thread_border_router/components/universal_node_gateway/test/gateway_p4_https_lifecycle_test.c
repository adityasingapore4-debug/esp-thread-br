/* HOST_UNIT: production owner/handler, substituted HTTP/HTTPS/timer/provider
 * APIs. This is deliberately NOT ESP_IDF_RUNTIME or a network server. */
#define P4_ADAPTER_TEST_MAIN adapter_regression_main
#include "gateway_p4_auth_adapter_test.c"
#include "../universal_node_gateway_p4_https.c"
#include <stdio.h>

typedef struct {int fd;void *ctx;httpd_free_ctx_fn_t release;} mock_session_t;
static mock_session_t sessions_mock[4];
static httpd_ssl_config_t server_config;
static httpd_uri_t registered;
static unsigned starts,stops,releases,prepared,ctx_frees,requests,send_calls;
static bool supported,prepare_ok=true,fail_start,fail_stop,fail_register,fail_timer;
static bool live,tls_copy_live,fail_send;
static uint8_t source_key[64];
static uint64_t fake_now;
static const uint8_t *input;
static size_t input_length,input_offset,chunk_size=532;
static unsigned read_call,fail_at;
static int injected_receive;
static uint64_t read_step,auth_step;
static char output[2048];
static size_t output_length;
static int server_cookie,timer_cookie;
static void (*queued)(void *);
static void *queued_argument;

bool p4_https_test_raw_admission_available(void){return supported;}
int64_t esp_timer_get_time(void){uint64_t result=fake_now;if(auth_step&&input_offset==input_length){fake_now+=auth_step;auth_step=0;}return (int64_t)result*1000;}
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){return s;}
int xSemaphoreTake(SemaphoreHandle_t s,unsigned wait){(void)wait;assert(s);return 1;}
int xSemaphoreGive(SemaphoreHandle_t s){assert(s);return 1;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out){assert(args->callback==tick);if(fail_timer)return ESP_FAIL;*out=&timer_cookie;return ESP_OK;}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer,uint64_t period){assert(timer==&timer_cookie&&period==100000);return ESP_OK;}
esp_err_t esp_timer_stop(esp_timer_handle_t timer){assert(timer==&timer_cookie);return ESP_OK;}
esp_err_t esp_timer_delete(esp_timer_handle_t timer){assert(timer==&timer_cookie);return ESP_OK;}
bool p4_https_identity_prepare(httpd_ssl_config_t *config){
 prepared++;if(!prepare_ok)return false;
 *config=(httpd_ssl_config_t)HTTPD_SSL_CONFIG_DEFAULT();memset(source_key,0xa5,sizeof(source_key));
 config->prvtkey_pem=source_key;config->prvtkey_len=sizeof(source_key);return true;
}
void p4_https_identity_release(void){assert(!live&&!tls_copy_live);memset(source_key,0,sizeof(source_key));releases++;}
esp_err_t httpd_ssl_start(httpd_handle_t *out,httpd_ssl_config_t *config){
 starts++;server_config=*config;assert(!config->httpd.close_fn&&!config->httpd.global_transport_ctx&&!config->httpd.global_transport_ctx_free_fn);
 if(fail_start)return ESP_FAIL;
 live=tls_copy_live=true;
 /* The listener may accept before registration commits. Admission must still fail. */
 assert(config->httpd.open_fn(&server_cookie,77)==ESP_FAIL);
 *out=&server_cookie;return ESP_OK;
}
static mock_session_t *session(int fd){for(size_t i=0;i<4;i++)if(sessions_mock[i].fd==fd)return &sessions_mock[i];return NULL;}
static void clear_session(mock_session_t *s){
 if(s&&s->ctx){void *ctx=s->ctx;httpd_free_ctx_fn_t release=s->release;s->ctx=NULL;s->release=NULL;release(ctx);ctx_frees++;}
 if(s)s->fd=-1;
}
esp_err_t httpd_ssl_stop(httpd_handle_t handle){
 assert(handle==&server_cookie&&live);stops++;
 if(fail_stop)return ESP_FAIL;
 for(size_t i=0;i<4;i++)clear_session(&sessions_mock[i]);
 live=tls_copy_live=false;queued=NULL;return ESP_OK;
}
esp_err_t httpd_register_uri_handler(httpd_handle_t handle,const httpd_uri_t *route){assert(handle==&server_cookie);registered=*route;return fail_register?ESP_FAIL:ESP_OK;}
void *httpd_sess_get_ctx(httpd_handle_t handle,int fd){assert(handle==&server_cookie);mock_session_t *s=session(fd);return s?s->ctx:NULL;}
void httpd_sess_set_ctx(httpd_handle_t handle,int fd,void *ctx,httpd_free_ctx_fn_t release){assert(handle==&server_cookie);mock_session_t *s=session(fd);assert(s&&!s->ctx);s->ctx=ctx;s->release=release;}
esp_err_t httpd_sess_trigger_close(httpd_handle_t handle,int fd){assert(handle==&server_cookie);clear_session(session(fd));return ESP_OK;}
int httpd_req_to_sockfd(httpd_req_t *r){return r->test_fd;}
int httpd_req_recv(httpd_req_t *r,char *out,size_t capacity){
 (void)r;read_call++;fake_now+=read_step;
 if(fail_at&&read_call==fail_at)return injected_receive;
 if(input_offset==input_length)return 0;
 size_t n=input_length-input_offset;if(n>chunk_size)n=chunk_size;if(n>capacity)n=capacity;
 memcpy(out,input+input_offset,n);input_offset+=n;return (int)n;
}
int httpd_socket_send(httpd_handle_t handle,int fd,const char *data,size_t n,int flags){
 (void)fd;(void)flags;assert(handle==&server_cookie);send_calls++;
 if(fail_send)return -1;
 assert(output_length+n<sizeof(output));memcpy(output+output_length,data,n);output_length+=n;output[output_length]=0;return (int)n;
}
esp_err_t httpd_queue_work(httpd_handle_t h,void (*function)(void *),void *argument){assert(h==&server_cookie&&!queued);queued=function;queued_argument=argument;return ESP_OK;}
static void work(void){if(queued){void (*function)(void *)=queued;void *arg=queued_argument;queued=NULL;function(arg);}}
static connection_t *open_connection(int fd){
 for(size_t i=0;i<4;i++)if(sessions_mock[i].fd<0){sessions_mock[i].fd=fd;break;}
 assert(server_config.httpd.open_fn(&server_cookie,fd)==ESP_OK);return httpd_sess_get_ctx(&server_cookie,fd);
}
static connection_t *fresh_connection(int fd){clear_session(session(fd));gateway_p4_http_reset();return open_connection(fd);}
static int dispatch(connection_t *c,const uint8_t *body,size_t n){
 assert(c);c->admitted=true;c->request.body_length=n;c->request_deadline=fake_now+10000;
 input=body;input_length=n;input_offset=read_call=0;output_length=0;output[0]=0;requests++;
 httpd_req_t r={.handle=&server_cookie,.content_len=n,.test_fd=c->fd};
 esp_err_t result=registered.handler(&r);
 assert(result==ESP_OK||result==ESP_FAIL);int status=0;assert(sscanf(output,"HTTP/1.1 %d",&status)==1);
 if(status!=200){assert(!strstr(output,"P4-Access-Token:"));assert(strstr(output,"Content-Length: 0\r\n"));}
 assert(strstr(output,"Cache-Control: no-store\r\n"));return status;
}
static size_t start_payload(uint8_t *out){
 adr073_auth_start_request_t start={.scope=ADR073_AUTH_SCOPE_LOCAL_SESSION};
 const char *first="n,,n=u,r=runtime123";start.client_first_length=strlen(first);memcpy(start.client_first,first,start.client_first_length);
 size_t n;out[0]=0x3e;assert(adr073_auth_start_request_encode(&start,out+1,531,&n));return n+1;
}
static size_t proof_payload(const char *wire,uint8_t *out){
 const char *p=strstr(wire,"\r\n\r\n");assert(p);const uint8_t *response=(const uint8_t*)p+4;
 adr073_auth_start_response_t started;assert(response[0]==0x3e);size_t n=19+((size_t)response[18]<<8)+response[19];assert(adr073_auth_start_response_parse(response+1,n,&started));
 auth_domain_scram_verifier_t verifier;assert(auth_domain_verifier_load(AUTH_DOMAIN_LOCAL,&verifier));
 scram_server_first_t sf;assert(scram_server_first_parse(started.server_first,started.server_first_length,&sf));
 char without[513],final[513];assert(snprintf(without,sizeof(without),"c=biws,r=%s",sf.nonce)>0);
 assert(scram_test_client_final("local password",&verifier,"n=u,r=runtime123",(const char*)started.server_first,without,final));
 adr073_auth_proof_request_t proof={0};memcpy(proof.transaction_id,started.transaction_id,16);proof.client_final_length=strlen(final);memcpy(proof.client_final,final,proof.client_final_length);
 out[0]=0x3f;assert(adr073_auth_proof_request_encode(&proof,out+1,531,&n));return n+1;
}
static void no_receive_fault(void){fail_at=0;read_step=auth_step=0;chunk_size=532;fail_send=false;}
int main(void){
 setup();enroll(AUTH_DOMAIN_LOCAL,"local password");
 adr073_auth_transaction_table_t table;local_access_session_service_t local;rng_t random={0};
 adr073_auth_transaction_table_init(&table,rng,&random);local_access_session_init(&local);p4_access_session_init(&local);p4_access_session_test_rng(random_token,NULL);gateway_p4_auth_init(&table);
 for(size_t i=0;i<4;i++)sessions_mock[i].fd=-1;
 assert(gateway_p4_https_start()==ESP_ERR_NOT_SUPPORTED&&starts==0&&prepared==0);
 supported=true;prepare_ok=false;assert(gateway_p4_https_start()==ESP_ERR_INVALID_STATE&&starts==0&&releases==0);prepare_ok=true;
 fail_start=true;assert(gateway_p4_https_start()==ESP_FAIL&&releases==1&&!owner);fail_start=false;
 fail_register=true;assert(gateway_p4_https_start()==ESP_FAIL&&!owner&&!live);fail_register=false;
 fail_timer=true;assert(gateway_p4_https_start()==ESP_FAIL&&!owner&&!live);fail_timer=false;
 assert(gateway_p4_https_start()==ESP_OK);assert(server_config.httpd.max_open_sockets==4&&server_config.httpd.stack_size==16384);
 assert(!strcmp(registered.uri,"/un/p4/auth")&&registered.method==HTTP_POST);
 uint8_t start[532],proof[532];size_t n=start_payload(start);fake_now=100;
 connection_t *a=open_connection(7);gateway_p4_auth_client_t stale=a->connection.client;
 chunk_size=1;assert(dispatch(a,start,n)==200&&read_call==n);size_t pn=proof_payload(output,proof);
 assert(a->pending_deadline==100+ADR073_AUTH_TRANSACTION_TIMEOUT_MS);
 chunk_size=3;assert(dispatch(a,proof,pn)==200);char *header=strstr(output,"P4-Access-Token: ");assert(header);
 p4_access_token_t issued;assert(gateway_p4_http_token_decode(header+17,43,&issued));
 unsigned freed=ctx_frees;clear_session(session(7));clear_session(session(7));assert(ctx_frees==freed+1&&gateway_p4_auth_validate(&issued,fake_now));
 a=open_connection(7);assert(a->connection.client!=stale&&dispatch(a,proof,pn)==403);
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);
 uint8_t pending[16];memcpy(pending,table.entries[0].id,16);
 clear_session(session(7));assert(!adr073_auth_transaction_lookup(&table,pending,fake_now));a=open_connection(7);assert(dispatch(a,proof,pn)==403);
 /* Timeout, recv error, zero-byte EOF and truncation: no proof dispatch/token. */
 int faults[]={HTTPD_SOCK_ERR_TIMEOUT,-1,0};
 for(size_t i=0;i<3;i++){
  no_receive_fault();a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);
  chunk_size=2;fail_at=2;injected_receive=faults[i];assert(dispatch(a,proof,pn)==400);
  for(size_t j=0;j<4;j++)assert(table.entries[j].state==ADR073_AUTH_TRANSACTION_FREE);
 }
 no_receive_fault();a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);
 /* Truncated input advertises the full body but returns zero at its end. */
 a->admitted=true;a->request.body_length=pn;input=proof;input_length=pn-1;input_offset=read_call=0;output_length=0;
 httpd_req_t truncated={.handle=&server_cookie,.content_len=pn,.test_fd=7};assert(registered.handler(&truncated)==ESP_FAIL&&!strstr(output,"P4-Access-Token:"));
 no_receive_fault();a=fresh_connection(7);chunk_size=1;read_step=1000;assert(dispatch(a,start,n)==400&&read_call<=10);no_receive_fault();
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);fake_now=a->pending_deadline;
 assert(dispatch(a,proof,pn)==403&&!strstr(output,"P4-Access-Token:"));
 /* Complete receives may still cross the deadline before token delivery. */
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);auth_step=10000;
 assert(dispatch(a,proof,pn)==403);auth_step=0;
 for(size_t j=0;j<4;j++)assert(table.entries[j].state==ADR073_AUTH_TRANSACTION_FREE);
 unsigned active_count=0;for(size_t j=0;j<4;j++)active_count+=local.slots[j].active;assert(active_count==1);
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);pn=proof_payload(output,proof);a->admitted=true;a->request.body_length=pn;input=proof;input_length=pn;input_offset=0;fail_send=true;assert(registered.handler(&(httpd_req_t){.handle=&server_cookie,.content_len=pn,.test_fd=7})==ESP_FAIL);no_receive_fault();
 active_count=0;for(size_t j=0;j<4;j++)active_count+=local.slots[j].active;assert(active_count==1);
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);tick(NULL);tick(NULL);assert(queued);fake_now=a->request_deadline;work();assert(!session(7));
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);
 /* Bounded request admission and all four connection/transaction slots. */
 uint8_t too_large[P4_HTTP_BODY_MAX+1]={0};assert(dispatch(a,too_large,sizeof(too_large))==413);
 uint8_t bad_command[]={1};assert(dispatch(a,bad_command,sizeof(bad_command))==400);
 a=fresh_connection(7);assert(dispatch(a,start,n)==200);
 for(int fd=8;fd<=10;fd++){connection_t *other=open_connection(fd);assert(dispatch(other,start,n)==200);}
 assert(opened(&server_cookie,99)==ESP_ERR_NO_MEM&&!httpd_sess_get_ctx(&server_cookie,99));
 unsigned pending_count=0;for(size_t i=0;i<4;i++)pending_count+=table.entries[i].state!=ADR073_AUTH_TRANSACTION_FREE;assert(pending_count==4);
 for(int fd=8;fd<=10;fd++)clear_session(session(fd));
 unsigned prior_releases=releases;fail_stop=true;assert(gateway_p4_https_stop()==ESP_FAIL&&owner&&live&&releases==prior_releases&&source_key[0]==0xa5&&!accepting);
 assert(gateway_p4_https_start()==ESP_ERR_INVALID_STATE);fail_stop=false;assert(gateway_p4_https_stop()==ESP_OK&&!owner&&!live&&releases==prior_releases+1);
 for(size_t i=0;i<sizeof(source_key);i++)assert(source_key[i]==0);
 assert(gateway_p4_https_stop()==ESP_ERR_INVALID_STATE);
 for(unsigned i=0;i<3;i++){assert(gateway_p4_https_start()==ESP_OK);(void)open_connection(7);assert(gateway_p4_https_stop()==ESP_OK);}
 assert(gateway_p4_auth_validate(&issued,fake_now)); /* stop does not reset canonical/P4 authority */
 for(size_t i=0;i<4;i++)assert(table.entries[i].state==ADR073_AUTH_TRANSACTION_FREE);
 p4_access_session_reset();gateway_p4_auth_init(NULL);
 printf("HOST_UNIT PASS: %u dispatched requests, %u context releases; receive/deadline/stop ownership mocks only\n",requests,ctx_frees);
 return 0;
}
