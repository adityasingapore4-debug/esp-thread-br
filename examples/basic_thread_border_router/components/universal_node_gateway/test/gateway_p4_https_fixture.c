/* Synthetic loopback transport for the production raw HTTP/authentication core.
 * OpenSSL replaces ESP-TLS only; no mock authentication-success path. */
#define main adr074_unused_main
#include "local_access_session_test.c"
#undef main
#include "universal_node_gateway_p4_http.h"
#include "p4_access_session_test.h"
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
static bool token_rng(uint8_t *out,size_t n,void *ctx){(void)ctx;return !getenv("P4_FIXTURE_TOKEN_RNG_FAIL")&&RAND_bytes(out,(int)n)==1;}
static uint64_t clock_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static volatile sig_atomic_t running=1;
static void stop(int sig){(void)sig;running=0;}
typedef struct {int fd;SSL *tls;gateway_p4_http_connection_t core;} peer_t;
static void retire(peer_t *p){gateway_p4_http_close(&p->core);SSL_shutdown(p->tls);SSL_free(p->tls);close(p->fd);memset(p,0,sizeof(*p));p->fd=-1;}
static bool exchange(peer_t *p){
 uint8_t raw[P4_HTTP_REQUEST_MAX];size_t n=0,target=0;uint64_t began=clock_ms();int status=200;gateway_p4_http_request_t request={0};
 while(1){
  if(clock_ms()-began>=10000){status=400;break;}
  int k=SSL_read(p->tls,raw+n,(int)(target?target-n:1));if(k<=0)return false;n+=(size_t)k;
  if(!target&&n>=4&&!memcmp(raw+n-4,"\r\n\r\n",4)){
   status=gateway_p4_http_headers(raw,n,&request);if(status!=200)break;target=n+request.body_length;
  }
  if(!target&&n>=P4_HTTP_HEADER_MAX){status=400;break;}
  if(target&&target==n)break;
 }
 gateway_p4_http_response_t response={.status=status};
 if(status==200){
  if(SSL_pending(p->tls)>0)response.status=400;
  else{
   if(request.has_authorization){printf("BEARER_RESULT %d\n",gateway_p4_http_validate(&request,clock_ms()));fflush(stdout);}
   gateway_p4_http_auth(&p->core,&request,raw+request.header_length,request.body_length,clock_ms(),&response);
  }
 }
 uint8_t wire[1024];size_t len=gateway_p4_http_serialize(&response,wire,sizeof(wire));size_t sent=0;
 while(sent<len){int k=SSL_write(p->tls,wire+sent,(int)(len-sent));if(k<=0)break;sent+=(size_t)k;}
 bool delivered=len&&sent==len;bool keep=delivered&&response.status==200;
 gateway_p4_http_delivery(&response,delivered);memset(raw,0,sizeof(raw));memset(wire,0,sizeof(wire));return keep;
}
int main(int argc,char **argv){
 assert(argc==3);signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
 setup();if(!getenv("P4_FIXTURE_NO_VERIFIER"))enroll(AUTH_DOMAIN_LOCAL,"localpassword");
 adr073_auth_transaction_table_t table;local_access_session_service_t sessions;rng_t random={0};
 adr073_auth_transaction_table_init(&table,rng,&random);local_access_session_init(&sessions);p4_access_session_init(&sessions);p4_access_session_test_rng(token_rng,NULL);gateway_p4_auth_init(&table);gateway_p4_http_reset();
 SSL_CTX *ctx=SSL_CTX_new(TLS_server_method());assert(ctx);assert(SSL_CTX_use_certificate_file(ctx,argv[1],SSL_FILETYPE_ASN1)==1);assert(SSL_CTX_use_PrivateKey_file(ctx,argv[2],SSL_FILETYPE_PEM)==1);
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);struct sockaddr_in addr={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
 assert(!bind(listener,(struct sockaddr*)&addr,sizeof(addr)));assert(!listen(listener,4));socklen_t alen=sizeof(addr);assert(!getsockname(listener,(struct sockaddr*)&addr,&alen));
 uint8_t installation[16],principal[8];assert(auth_domain_installation_id_get(installation));assert(node_identity_principal_eui64_get(principal));
 printf("PORT %u INSTALLATION ",ntohs(addr.sin_port));
 for(unsigned i=0;i<16;i++)printf("%02x",installation[i]);
 printf(" PRINCIPAL ");for(unsigned i=0;i<8;i++)printf("%02x",principal[i]);
 printf(" NODE %u\n",node_identity_get());fflush(stdout);
 peer_t peers[4];memset(peers,0,sizeof(peers));for(unsigned i=0;i<4;i++)peers[i].fd=-1;
 while(running){
  fd_set reads;FD_ZERO(&reads);FD_SET(listener,&reads);int maxfd=listener;
  for(unsigned i=0;i<4;i++)if(peers[i].fd>=0){FD_SET(peers[i].fd,&reads);if(peers[i].fd>maxfd)maxfd=peers[i].fd;}
  struct timeval wait={.tv_sec=1};int ready=select(maxfd+1,&reads,NULL,NULL,&wait);if(ready<0)continue;
  if(FD_ISSET(listener,&reads)){
   int fd=accept(listener,NULL,NULL);if(fd>=0){unsigned slot=0;while(slot<4&&peers[slot].fd>=0)slot++;
    struct timeval timeout={.tv_sec=10};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(slot==4)close(fd);else{peer_t *p=&peers[slot];p->fd=fd;p->tls=SSL_new(ctx);SSL_set_fd(p->tls,fd);if(SSL_accept(p->tls)!=1||!gateway_p4_http_open(&p->core))retire(p);}
   }
  }
  for(unsigned i=0;i<4;i++)if(peers[i].fd>=0&&FD_ISSET(peers[i].fd,&reads))if(!exchange(&peers[i]))retire(&peers[i]);
 }
 for(unsigned i=0;i<4;i++)if(peers[i].fd>=0)retire(&peers[i]);
 gateway_p4_auth_init(NULL);p4_access_session_reset();close(listener);SSL_CTX_free(ctx);return 0;
}
