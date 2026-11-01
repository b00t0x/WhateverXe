// Validate a direct x86 call by its resolved target, retaining opcode checks.
#ifndef KERN_XELP_CALL_TARGET_HPP
#define KERN_XELP_CALL_TARGET_HPP
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace XELPCall {
inline bool targets(const uint8_t *bytes,size_t length,uintptr_t site,uintptr_t target) {
 if(!bytes || length<5 || !site || !target || bytes[0]!=0xE8 || site>UINTPTR_MAX-5)return false;
 const uint32_t displacement=static_cast<uint32_t>(bytes[1]) |
  static_cast<uint32_t>(bytes[2])<<8 | static_cast<uint32_t>(bytes[3])<<16 |
  static_cast<uint32_t>(bytes[4])<<24;
 const uintptr_t next=site+5;
 if(displacement&0x80000000U) {
  const uint64_t distance=0x100000000ULL-displacement;
  return next>=distance && next-distance==target;
 }
 return next<=UINTPTR_MAX-displacement && next+displacement==target;
}
// The private pmap accessor's field load is the ABI evidence. Its distance
// from the caller and its conditional branch displacement are not ABI.
inline bool rootAccessor(const uint8_t *bytes,size_t length) {
 const uint8_t prefix[]={0x55,0x48,0x89,0xe5,0x48,0xb8,0,0,0,0,0,0x80,0xff,0xff,0x48,0x01,0xf0,
  0x48,0xc1,0xe8,0x30,0x3d,0xff,0xff,0,0,0x0f,0x82};
 const uint8_t field[]={0x48,0x8b,0x47,0x50};
 return bytes && length>=36 && !memcmp(bytes,prefix,sizeof(prefix)) && !memcmp(bytes+32,field,sizeof(field));
}
}
#endif
