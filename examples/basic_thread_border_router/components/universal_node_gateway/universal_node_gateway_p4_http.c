#include "universal_node_gateway_p4_http.h"
#include "adr073_auth_payload_codec.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
static uint64_t global_starts[8];
static unsigned global_count;
static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
static void wipe(void *p,size_t n){volatile uint8_t *b=p;while(n--)*b++=0;}
void gateway_p4_http_token_encode(const p4_access_token_t *token,char text[44]){
 unsigned acc=0,bits=0;size_t n=0;
 for(size_t i=0;i<32;i++){acc=(acc<<8)|token->bytes[i];bits+=8;while(bits>=6){bits-=6;text[n++]=alphabet[(acc>>bits)&63];}}
 text[n++]=alphabet[(acc<<2)&63];text[n]=0;
}
bool gateway_p4_http_token_decode(const char *text,size_t length,p4_access_token_t *token){
 if(!token)return false;
 wipe(token,sizeof(*token));if(!text||length!=43)return false;
 unsigned acc=0,bits=0;size_t n=0;
 for(size_t i=0;i<length;i++){
  const char *p=text[i]?strchr(alphabet,text[i]):NULL;
  if(!p){wipe(token,sizeof(*token));return false;}
  acc=(acc<<6)|(unsigned)(p-alphabet);bits+=6;
  if(bits>=8){bits-=8;if(n>=32){wipe(token,sizeof(*token));return false;}token->bytes[n++]=(uint8_t)(acc>>bits);}
 }
 char canonical[44];gateway_p4_http_token_encode(token,canonical);
 bool ok=n==32&&!memcmp(canonical,text,43);wipe(canonical,sizeof(canonical));
 if(!ok)wipe(token,sizeof(*token));
 return ok;
}
void gateway_p4_http_reset(void){wipe(global_starts,sizeof(global_starts));global_count=0;}
bool gateway_p4_http_open(gateway_p4_http_connection_t *c){memset(c,0,sizeof(*c));c->client=gateway_p4_auth_open();return c->client!=0;}
void gateway_p4_http_close(gateway_p4_http_connection_t *c){gateway_p4_auth_close(c->client);wipe(c,sizeof(*c));}
static bool name_is(const char *p,size_t n,const char *name){return strlen(name)==n&&!strncasecmp(p,name,n);}
static bool field_char(unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("!#$%&'*+-.^_`|~",c)!=NULL;}
int gateway_p4_http_headers(const uint8_t *raw,size_t length,gateway_p4_http_request_t *r){
 memset(r,0,sizeof(*r));
 if(!raw||length<4||length>P4_HTTP_HEADER_MAX||memcmp(raw+length-4,"\r\n\r\n",4))return 400;
 char text[P4_HTTP_HEADER_MAX+1];memcpy(text,raw,length);text[length]=0;
 for(size_t i=0;i<length;i++)if(raw[i]==0||raw[i]>126||(raw[i]<32&&raw[i]!='\r'&&raw[i]!='\n'))return 400;
 char *line=strstr(text,"\r\n");if(!line)return 400;*line=0;
 for(char *q=text;*q;q++)if((unsigned char)*q<32)return 400;
 char *path=strchr(text,' ');if(!path)return 400;*path++=0;
 for(char *q=text;*q;q++)if(!field_char((unsigned char)*q))return 400;
 char *version=strchr(path,' ');if(!version)return 400;*version++=0;
 if(strcmp(version,"HTTP/1.1")||strchr(path,'?'))return 400;
 int status=strcmp(path,"/un/p4/auth")?404:strcmp(text,"POST")?405:200;
 unsigned cl=0,ct=0,host=0;bool origin=false,bad_type=false;
 for(char *p=line+2;p<text+length-2;){
  char *end=strstr(p,"\r\n");if(!end)return 400;*end=0;
  for(char *q=p;q<end;q++)if((unsigned char)*q<32)return 400;
  char *colon=strchr(p,':');if(!colon||colon==p)return 400;
  for(char *q=p;q<colon;q++)if(!field_char((unsigned char)*q))return 400;
  size_t namelen=(size_t)(colon-p);char *value=colon+1;
  /* One conventional separator space is permitted. Value whitespace is kept. */
  if(*value==' ')value++;
  if(name_is(p,namelen,"Content-Length")){
   if(++cl!=1||!*value)return 400;
   size_t n=0;for(char *v=value;*v;v++){if(*v<'0'||*v>'9')return 400;if(n<=P4_HTTP_BODY_MAX)n=n*10+(unsigned)(*v-'0');}r->body_length=n;
  }else if(name_is(p,namelen,"Content-Type")){if(++ct!=1)return 400;bad_type=strcmp(value,"application/octet-stream")!=0;
  }else if(name_is(p,namelen,"Host")){if(++host!=1||!*value)return 400;
  }else if(name_is(p,namelen,"Origin")){origin=true;
  }else if(name_is(p,namelen,"Transfer-Encoding")||name_is(p,namelen,"Content-Encoding")||name_is(p,namelen,"Expect")||name_is(p,namelen,"Upgrade")||name_is(p,namelen,"P4-Access-Token")){return 400;
  }else if(name_is(p,namelen,"Authorization")){
   if(r->has_authorization||strlen(value)!=50||memcmp(value,"Bearer ",7))return 400;
   p4_access_token_t token;if(!gateway_p4_http_token_decode(value+7,43,&token))return 400;
   wipe(&token,sizeof(token));memcpy(r->authorization,value,51);r->has_authorization=true;
  }
  p=end+2;
 }
 r->header_length=length;
 if(cl!=1||host!=1)return 400;
 if(r->body_length>P4_HTTP_BODY_MAX)return 413;
 if(origin)return 403;
 if(status!=200)return status;
 if(ct!=1||bad_type)return 415;
 return 200;
}
static void prune(uint64_t *times,unsigned *n,uint64_t now){unsigned j=0;for(unsigned i=0;i<*n;i++)if(now<times[i]||now-times[i]<60000)times[j++]=times[i];*n=j;}
void gateway_p4_http_auth(gateway_p4_http_connection_t *c,const gateway_p4_http_request_t *r,const uint8_t *body,size_t n,uint64_t now,gateway_p4_http_response_t *out){
 memset(out,0,sizeof(*out));out->status=400;
 if(!c||!c->client){out->status=503;return;}
 if(!r||!body||n!=r->body_length||n<1||n>P4_HTTP_BODY_MAX)goto failed;
 if(body[0]==0x3e){
  adr073_auth_start_request_t parsed;
  if(n>519||!adr073_auth_start_request_parse(body+1,n-1,&parsed)||parsed.scope!=ADR073_AUTH_SCOPE_LOCAL_SESSION)goto failed;
  prune(global_starts,&global_count,now);prune(c->starts,&c->start_count,now);
  if(global_count>=8||c->start_count>=4){out->status=429;goto failed;}
  global_starts[global_count++]=now;c->starts[c->start_count++]=now;
 }else if(body[0]==0x3f){
  uint8_t id[16];const uint8_t *final;size_t len;
  if(!adr073_auth_proof_request_parts_parse(body+1,n-1,id,&final,&len))goto failed;
 }else goto failed;
 out->status=gateway_p4_auth_process_status(c->client,body[0],body+1,n-1,now,out->body+1,sizeof(out->body)-1,&out->body_length,&out->issued);
 if(out->status!=200)goto failed;
 out->body[0]=body[0];out->body_length++;
 if(body[0]==0x3f)gateway_p4_http_token_encode(&out->issued,out->token);
 return;
failed:
 gateway_p4_auth_abort(c->client);out->body_length=0;wipe(out->body,sizeof(out->body));wipe(&out->issued,sizeof(out->issued));wipe(out->token,sizeof(out->token));
}
int gateway_p4_http_validate(const gateway_p4_http_request_t *r,uint64_t now){
 p4_access_token_t token;bool ok=r&&r->has_authorization&&gateway_p4_http_token_decode(r->authorization+7,43,&token);
 if(ok)ok=gateway_p4_auth_validate(&token,now);
 wipe(&token,sizeof(token));return ok?200:401;
}
size_t gateway_p4_http_serialize(const gateway_p4_http_response_t *r,uint8_t *out,size_t cap){
 const char *reason=r->status==200?"OK":r->status==400?"Bad Request":r->status==401?"Unauthorized":r->status==403?"Forbidden":r->status==404?"Not Found":r->status==405?"Method Not Allowed":r->status==413?"Content Too Large":r->status==415?"Unsupported Media Type":r->status==429?"Too Many Requests":"Service Unavailable";
 bool success=r->status==200;size_t body=success?r->body_length:0;
 int n=snprintf((char*)out,cap,"HTTP/1.1 %d %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n%s%s%s%s%s\r\n",r->status,reason,body,
 success?"Content-Type: application/octet-stream\r\n":"Connection: close\r\n",
 r->status==405?"Allow: POST\r\n":r->status==401?"WWW-Authenticate: Bearer realm=\"p4\"\r\n":"",
 success&&r->token[0]?"P4-Access-Token: ":"",success&&r->token[0]?r->token:"",success&&r->token[0]?"\r\n":"");
 if(n<0||(size_t)n>=cap||body>cap-(size_t)n)return 0;
 memcpy(out+n,r->body,body);return (size_t)n+body;
}
void gateway_p4_http_delivery(gateway_p4_http_response_t *r,bool delivered){if(!delivered&&r->token[0])p4_access_session_revoke(&r->issued);wipe(r,sizeof(*r));}

void gateway_p4_http_hide_authorization(uint8_t *raw,size_t length){
 size_t start=0;
 while(start+2<length){
  size_t end=start;while(end+1<length&&(raw[end]!='\r'||raw[end+1]!='\n'))end++;
  size_t colon=start;while(colon<end&&raw[colon]!=':')colon++;
  if(colon<end&&name_is((const char*)raw+start,colon-start,"Authorization")){
   for(size_t i=colon+1;i<end;i++)raw[i]='X';
  }
  start=end+2;
 }
}
