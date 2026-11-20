#include "wifi_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static void request_tests(void) {
 uint8_t b[120]={1,1}; wifi_request_t r;
 for(unsigned op=1;op<=5;op++) {
  memset(b,0,sizeof b); b[0]=1; b[1]=(uint8_t)op; size_t n=op<=2?4:op==3?33:20; b[4]=1;
  if(op==3) { b[20]=1; b[21]=1; b[22]=8; b[24]='a'; memset(b+25,'z',8); }
  CHECK(wifi_request_decode(b,n,&r));
  for(size_t k=0;k<n;k++) CHECK(!wifi_request_decode(b,k,&r));
  CHECK(!wifi_request_decode(b,n+1,&r)); b[0]=2; CHECK(!wifi_request_decode(b,n,&r)); b[0]=1;
  b[2]=1; CHECK(!wifi_request_decode(b,n,&r)); b[2]=0; b[3]=1; CHECK(!wifi_request_decode(b,n,&r));
 }
 memset(b,0,sizeof b); b[0]=1; b[1]=0; CHECK(!wifi_request_decode(b,4,&r)); b[1]=6; CHECK(!wifi_request_decode(b,4,&r));
 b[1]=3; b[4]=1; b[20]=1; b[21]=32; b[22]=63; memset(b+24,'x',95); CHECK(wifi_request_decode(b,119,&r)); CHECK(r.config.ssid_len==32 && r.config.secret_len==63);
 b[20]=0; CHECK(!wifi_request_decode(b,119,&r)); b[20]=3; CHECK(!wifi_request_decode(b,119,&r)); b[20]=2; CHECK(wifi_request_decode(b,119,&r));
 b[21]=0; CHECK(!wifi_request_decode(b,119,&r)); b[21]=33; CHECK(!wifi_request_decode(b,119,&r)); b[21]=32;
 b[22]=7; CHECK(!wifi_request_decode(b,119,&r)); b[22]=64; CHECK(!wifi_request_decode(b,119,&r)); b[22]=63;
 b[23]=1; CHECK(!wifi_request_decode(b,119,&r)); b[23]=0; b[24]=0; CHECK(!wifi_request_decode(b,119,&r)); b[24]='x'; b[56]=0x1f; CHECK(!wifi_request_decode(b,119,&r));
 wifi_zero(&r,sizeof r); for(size_t i=0;i<sizeof r;i++) CHECK(((const uint8_t *)&r)[i]==0);
}
static void response_tests(void) {
 wifi_response_t r={0}; uint8_t b[296]; size_t n;
 r.op=1; CHECK(wifi_response_encode(&r,b,sizeof b,&n) && n==12); r.u.status.flags=4; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.status.flags=0x80; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.status.flags=0; r.u.status.state=7; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.status.state=0; r.phase=1; CHECK(!wifi_response_encode(&r,b,sizeof b,&n));
 memset(&r,0,sizeof r); r.op=2; r.u.scan.count=8; for(size_t i=0;i<8;i++) {r.u.scan.entries[i].ssid_len=32; r.u.scan.entries[i].security=3; memset(r.u.scan.entries[i].ssid,'s',32);}
 CHECK(wifi_response_encode(&r,b,sizeof b,&n) && n==296); CHECK(!wifi_response_encode(&r,b,295,&n) && n==0); r.u.scan.count=9; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.scan.count=1; r.u.scan.entries[0].ssid_len=33; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.scan.entries[0].ssid_len=1; r.u.scan.entries[0].security=4; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.scan.entries[0].security=255; CHECK(wifi_response_encode(&r,b,sizeof b,&n)); CHECK(b[6]==0 && b[7]==0); r.phase=1; CHECK(!wifi_response_encode(&r,b,sizeof b,&n));
 for(unsigned op=3;op<=5;op++) { memset(&r,0,sizeof r); r.op=(uint8_t)op; r.phase=1; r.u.tx.id[0]=1; r.u.tx.state=1; CHECK(wifi_response_encode(&r,b,sizeof b,&n) && n==28); CHECK(b[22]==0 && b[23]==0); CHECK(!wifi_response_encode(&r,b,27,&n)); r.u.tx.state=0; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.tx.state=8; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.tx.state=4; r.phase=0; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); r.u.tx.generation=1; CHECK(wifi_response_encode(&r,b,sizeof b,&n)); r.phase=1; CHECK(!wifi_response_encode(&r,b,sizeof b,&n)); }
 /* Sentinel credential is distinct from all public fields. */
 static const uint8_t secret[8]={'p','a','s','s','w','o','r','d'};
 for(unsigned op=1;op<=5;op++) {
  memset(&r,0,sizeof r); r.op=(uint8_t)op;
  if(op==2) { r.u.scan.count=1; r.u.scan.entries[0].ssid_len=1; r.u.scan.entries[0].ssid[0]='s'; }
  if(op>=3) { r.phase=1; r.u.tx.id[0]=1; r.u.tx.state=1; }
  CHECK(wifi_response_encode(&r,b,sizeof b,&n));
  for(size_t i=0;i+sizeof secret<=n;i++) CHECK(memcmp(b+i,secret,sizeof secret)!=0);
 }
}
int main(void) { request_tests(); response_tests(); printf("codec qualification: %u checks passed\n",checks); }
