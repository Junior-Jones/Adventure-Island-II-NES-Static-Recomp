#include "ai2_sha256.h"
#include <string.h>

static uint32_t rotr(uint32_t v, unsigned n){return (v>>n)|(v<<(32u-n));}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put_be64(uint8_t *p,uint64_t v){unsigned i;for(i=0;i<8u;i++)p[7u-i]=(uint8_t)(v>>(i*8u));}
static const uint32_t k[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static void block(Ai2Sha256Ctx *c,const uint8_t *b){
    uint32_t w[64],a,bv,d,e,f,g,h,t1,t2,cc;unsigned i;
    for(i=0;i<16u;i++)w[i]=be32(b+4u*i);
    for(i=16u;i<64u;i++){uint32_t s0=rotr(w[i-15u],7)^rotr(w[i-15u],18)^(w[i-15u]>>3);uint32_t s1=rotr(w[i-2u],17)^rotr(w[i-2u],19)^(w[i-2u]>>10);w[i]=w[i-16u]+s0+w[i-7u]+s1;}
    a=c->state[0];bv=c->state[1];cc=c->state[2];d=c->state[3];e=c->state[4];f=c->state[5];g=c->state[6];h=c->state[7];
    for(i=0;i<64u;i++){uint32_t s1=rotr(e,6)^rotr(e,11)^rotr(e,25);uint32_t ch=(e&f)^((~e)&g);t1=h+s1+ch+k[i]+w[i];uint32_t s0=rotr(a,2)^rotr(a,13)^rotr(a,22);uint32_t maj=(a&bv)^(a&cc)^(bv&cc);t2=s0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=bv;bv=a;a=t1+t2;}
    c->state[0]+=a;c->state[1]+=bv;c->state[2]+=cc;c->state[3]+=d;c->state[4]+=e;c->state[5]+=f;c->state[6]+=g;c->state[7]+=h;
}
void ai2_sha256_init(Ai2Sha256Ctx *c){static const uint32_t iv[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};memcpy(c->state,iv,sizeof(iv));c->bit_count=0;c->buffer_used=0;}
void ai2_sha256_update(Ai2Sha256Ctx *c,const void *data,size_t size){const uint8_t *p=(const uint8_t*)data;c->bit_count+=(uint64_t)size*8u;while(size){size_t n=64u-c->buffer_used;if(n>size)n=size;memcpy(c->buffer+c->buffer_used,p,n);c->buffer_used+=n;p+=n;size-=n;if(c->buffer_used==64u){block(c,c->buffer);c->buffer_used=0;}}}
void ai2_sha256_final(Ai2Sha256Ctx *c,uint8_t digest[32]){uint64_t bits=c->bit_count;unsigned i;c->buffer[c->buffer_used++]=0x80u;if(c->buffer_used>56u){while(c->buffer_used<64u)c->buffer[c->buffer_used++]=0;block(c,c->buffer);c->buffer_used=0;}while(c->buffer_used<56u)c->buffer[c->buffer_used++]=0;put_be64(c->buffer+56u,bits);block(c,c->buffer);for(i=0;i<8u;i++){uint32_t v=c->state[i];digest[i*4u]=(uint8_t)(v>>24);digest[i*4u+1u]=(uint8_t)(v>>16);digest[i*4u+2u]=(uint8_t)(v>>8);digest[i*4u+3u]=(uint8_t)v;}memset(c,0,sizeof(*c));}
void ai2_sha256(const void *data,size_t size,uint8_t digest[32]){Ai2Sha256Ctx c;ai2_sha256_init(&c);ai2_sha256_update(&c,data,size);ai2_sha256_final(&c,digest);}
