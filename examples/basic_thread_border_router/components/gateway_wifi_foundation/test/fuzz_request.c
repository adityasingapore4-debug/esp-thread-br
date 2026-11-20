#include "wifi_codec.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define ITERATIONS 100000u
#define INITIAL_SEED 0x09102a01u
static uint32_t rng=INITIAL_SEED;
static uint32_t next(void) { rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng; }
static int reference(const uint8_t *p,size_t n) {
 if(n<4 || p[0]!=1 || p[1]<1 || p[1]>5 || p[2] || p[3]) return 0;
 if(p[1]<=2) return n==4;
 if(n<20) return 0;
 int nonzero=0; for(size_t i=4;i<20;i++) nonzero|=p[i]; if(!nonzero) return 0;
 if(p[1]>=4) return n==20;
 if(n<24 || p[20]<1 || p[20]>2 || p[21]<1 || p[21]>32 || p[22]<8 || p[22]>63 || p[23] || n!=24u+p[21]+p[22]) return 0;
 for(size_t i=0;i<p[21];i++) if(!p[24+i]) return 0;
 for(size_t i=0;i<p[22];i++) if(p[24+p[21]+i]<32 || p[24+p[21]+i]>126) return 0;
 return 1;
}
int main(void) {
 unsigned accepted=0;
 for(unsigned it=0;it<ITERATIONS;it++) {
  uint8_t p[160]={1,0}; uint8_t op=(uint8_t)(1+next()%5); p[1]=op; p[4]=1;
  size_t n=op<=2?4:op>=4?20:33;
  if(op==3) { p[20]=1; p[21]=1; p[22]=8; p[24]='X'; memset(p+25,'a',8); if(next()&1) { p[21]=32; p[22]=63; memset(p+24,'X',95); n=119; } }
  unsigned changes=next()%5; for(unsigned j=0;j<changes;j++) { size_t k=next()%128; p[k]=(uint8_t)next(); }
  if((next()&3)==0) n=next()%161;
  wifi_request_t out; memset(&out,0xa5,sizeof out); bool actual=wifi_request_decode(p,n,&out); assert(actual==(reference(p,n)!=0));
  if(actual) { accepted++; assert(out.op==op || out.op==p[1]); } else { for(size_t i=0;i<sizeof out;i++) assert(((uint8_t *)&out)[i]==0); }
  wifi_zero(&out,sizeof out);
 }
 printf("deterministic mutation seed=0x%08x iterations=%u accepted=%u passed\n",INITIAL_SEED,ITERATIONS,accepted); return 0;
}
