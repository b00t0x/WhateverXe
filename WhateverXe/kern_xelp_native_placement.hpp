// Native instruction contracts: instruction-boundary matching, no build/VAs.
#ifndef KERN_XELP_NATIVE_PLACEMENT_HPP
#define KERN_XELP_NATIVE_PLACEMENT_HPP
#include "kern_xelp_call_target.hpp"
namespace XELPNative {
// Only canonical, side-effect-free alignment NOPs may separate the expected
// instructions. Never scan past an unrecognised instruction to find a suffix.
struct Cursor {
 const uint8_t *bytes;size_t length,pos;
 bool take(const char *value,size_t count) {
  while(pos<length) {
   if(bytes[pos]==0x90) {++pos;continue;}
   if(length-pos>=2 && bytes[pos]==0x66 && bytes[pos+1]==0x90) {pos+=2;continue;}
   break;
  }
  if(count>length-pos || memcmp(bytes+pos,value,count))return false;
  pos+=count;return true;
 }
 bool skip(size_t count) {if(count>length-pos)return false;pos+=count;return true;}
};
// Decode the verifier's first call only after proving both argument sources:
// original VA in RSI and symbol-resolved kernel_pmap_store in RDI.
inline uintptr_t rootCall(const uint8_t *bytes,size_t length,uintptr_t base,uintptr_t store) {
 if(!bytes || !base || !store || length>UINTPTR_MAX-base)return 0;
 Cursor c {bytes,length,0};
 if(!c.take("\x55",1) || !c.take("\x48\x89\xe5",3) || !c.take("\x53",1) ||
    !c.take("\x50",1) || !c.take("\x48\x89\xfb",3) || !c.take("\x48\x8d\x3d",3) ||
    !c.skip(4) || XELPCall::relativeTarget(bytes+c.pos-4,base+c.pos)!=store ||
    !c.take("\x48\x89\xde",3) || !c.take("\xe8",1) || !c.skip(4))return 0;
 const auto target=XELPCall::relativeTarget(bytes+c.pos-4,base+c.pos);
 // The caller consumes the returned PTE pointer; an unrelated call is rejected.
 return c.take("\x48\x85\xc0",3) ? target : 0;
}
inline bool rootAccessor(const uint8_t *bytes,size_t length) {
 if(!bytes)return false;
 Cursor c {bytes,length,0};
 return c.take("\x55",1) && c.take("\x48\x89\xe5",3) &&
  c.take("\x48\xb8\0\0\0\0\0\x80\xff\xff",10) && c.take("\x48\x01\xf0",3) &&
  c.take("\x48\xc1\xe8\x30",4) && c.take("\x3d\xff\xff\0\0",5) &&
  c.take("\x0f\x82",2) && c.skip(4) && c.take("\x48\x8b\x47\x50",4);
}
// Scan a bounded native function on decoded instruction boundaries. Require
// exactly one call to pmap, its complete register/stack argument setup, stack
// cleanup, and the function's terminating RET. Embedded bytes are not calls.
template<class Decode> bool protectCall(const uint8_t *bytes,size_t length,uintptr_t base,
 uintptr_t pmap,bool tahoe,Decode decode) {
 if(!bytes || !base || !pmap || length>UINTPTR_MAX-base)return false;
 unsigned found=0;
 for(size_t pos=0;pos<length;) {
  const size_t size=decode(bytes+pos,length-pos);
  if(!size || size>15 || size>length-pos)return false;
  if(size==1 && bytes[pos]==0xc3)return found==1;
  Cursor c {bytes,length,pos};
  bool args=(!tahoe || c.take("\x48\x83\xec\x08",4)) &&
   c.take("\x4c\x89\xff",3) && c.take("\x4c\x89\xee",3) && c.take("\x89\xc2",2) &&
   c.take("\x8b\x4d\xd4",3) && c.take("\x44\x8b\x4d\xd0",4) &&
   (tahoe || c.take("\x6a\0",2)) && c.take("\x48\x8d\x45\xb8",4) &&
   c.take("\x50",1) && c.take("\x6a\x20",2) && c.take("\x6a\x01",2) && c.take("\xe8",1);
  if(args && c.skip(4) && XELPCall::relativeTarget(bytes+c.pos-4,base+c.pos)==pmap &&
     c.take("\x48\x83\xc4\x20",4)) {
   if(++found!=1)return false;
   // Skip the proven call sequence, including inter-instruction NOPs, to
   // avoid counting the same match twice when it begins with alignment.
   pos=c.pos;continue;
  }
  // A pmap call with a different argument contract is never admitted.
  if(size==5 && XELPCall::targets(bytes+pos,size,base+pos,pmap))return false;
  pos+=size;
 }
 return false;
}
}
#endif
