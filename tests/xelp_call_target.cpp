#include "kern_xelp_native_placement.hpp"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>

int main() {
 uint8_t accessor[]={0x55,0x48,0x89,0xe5,0x48,0xb8,0,0,0,0,0,0x80,0xff,0xff,0x48,0x01,0xf0,
  0x48,0xc1,0xe8,0x30,0x3d,0xff,0xff,0,0,0x0f,0x82,0xc1,0,0,0,0x48,0x8b,0x47,0x50};
 assert(XELPNative::rootAccessor(accessor,sizeof(accessor)));
 for(unsigned i=0;i<sizeof(accessor);++i) {
  accessor[i]^=1;
  assert(XELPNative::rootAccessor(accessor,sizeof(accessor))==(i>=28 && i<32));
  accessor[i]^=1;
 }
 assert(!XELPNative::rootAccessor(accessor,35));
 uint8_t bytes[] {0xE8,0,0,0,0};
 // Independent relocation oracle: move caller and callee independently.
 // Known build identifiers and their particular displacements are not inputs.
 uint64_t random=0x9e3779b97f4a7c15ULL;
 for(unsigned n=0;n<10000;++n) {
  random=random*6364136223846793005ULL+1;
  uintptr_t caller=0xffffff8000000000ULL+(random&0xffffff);
  random=random*6364136223846793005ULL+1;
  uintptr_t callee=0xffffff8000000000ULL+(random&0xffffff);
  int32_t relative=static_cast<int32_t>(callee-(caller+5));
  for(unsigned i=0;i<4;++i)bytes[i+1]=static_cast<uint32_t>(relative)>>(8*i);
  assert(XELPCall::targets(bytes,5,caller,callee));
  assert(!XELPCall::targets(bytes,5,caller,callee+1));
 }
 const uintptr_t site=0xFFFFFF800039B4DCULL;
 unsigned cases=0;
 for(uint32_t raw: {0U,1U,0x2F32FU,0x2F35FU,0x7FFFFFFFU,0x80000000U,0xFFFFFFFFU}) {
  for(unsigned i=0;i<4;++i)bytes[i+1]=raw>>(8*i);
  const auto target=raw&0x80000000U ? site+5-(0x100000000ULL-raw) : site+5+raw;
  assert(XELPCall::targets(bytes,5,site,target));
  assert(!XELPCall::targets(bytes,5,site,target+1));
  for(unsigned n=0;n<5;++n)assert(!XELPCall::targets(bytes,n,site,target));
  for(unsigned opcode=0;opcode<256;++opcode) {
   bytes[0]=opcode;
   assert(XELPCall::targets(bytes,5,site,target)==(opcode==0xE8));++cases;
  }
  bytes[0]=0xE8;
 }
 assert(!XELPCall::targets(nullptr,5,site,site+5));
 assert(!XELPCall::targets(bytes,5,0,1));
 assert(!XELPCall::targets(bytes,5,site,0));
 bytes[1]=1;bytes[2]=bytes[3]=bytes[4]=0;
 assert(!XELPCall::targets(bytes,5,UINTPTR_MAX-4,1));
 assert(!XELPCall::targets(bytes,5,UINTPTR_MAX-5,1));
 bytes[1]=bytes[2]=bytes[3]=bytes[4]=0xFF;
 assert(!XELPCall::targets(bytes,5,1,UINTPTR_MAX));
 bytes[1]=bytes[2]=bytes[3]=0;bytes[4]=0x80;
 assert(!XELPCall::targets(bytes,5,1,1));
 printf("PASS: %u direct call opcode cases; relative targets, truncation and overflow guards\n",cases);
}
