// Validate a direct x86 call by its resolved target, retaining opcode checks.
#ifndef KERN_XELP_CALL_TARGET_HPP
#define KERN_XELP_CALL_TARGET_HPP
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace XELPCall {
inline uintptr_t relativeTarget(const uint8_t *relative,uintptr_t next) {
 const uint32_t displacement=static_cast<uint32_t>(relative[0]) |
  static_cast<uint32_t>(relative[1])<<8 | static_cast<uint32_t>(relative[2])<<16 |
  static_cast<uint32_t>(relative[3])<<24;
 if(displacement&0x80000000U) {
  const uint64_t distance=0x100000000ULL-displacement;
  return next>=distance ? next-distance : 0;
 }
 return next<=UINTPTR_MAX-displacement ? next+displacement : 0;
}
inline bool targets(const uint8_t *bytes,size_t length,uintptr_t site,uintptr_t target) {
 if(!bytes || length<5 || !site || !target || bytes[0]!=0xE8 || site>UINTPTR_MAX-5)return false;
 return relativeTarget(bytes+1,site+5)==target;
}
// A KC may place branch stubs and GOT slots outside the calling kext.
// Read through the caller's nofault accessor; validate the destination identity.
template<class Read> uintptr_t importedTarget(uintptr_t entry,uintptr_t expected,Read read) {
 if(!entry || !expected)return 0;
 if(entry==expected)return entry;
 uint8_t stub[6] {};uintptr_t target=0;
 if(entry>UINTPTR_MAX-sizeof(stub) || !read(entry,stub,sizeof(stub)) ||
    stub[0]!=0xFF || stub[1]!=0x25)return 0;
 const auto slot=relativeTarget(stub+2,entry+sizeof(stub));
 if(!slot || slot>UINTPTR_MAX-sizeof(target) || !read(slot,&target,sizeof(target)))return 0;
 return target==expected ? target : 0;
}
}
#endif
