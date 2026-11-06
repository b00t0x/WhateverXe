// Native instruction contracts: instruction-boundary matching, no build/VAs.
#ifndef KERN_XELP_NATIVE_PLACEMENT_HPP
#define KERN_XELP_NATIVE_PLACEMENT_HPP
#include "kern_xelp_call_target.hpp"
namespace XELPNative {
// Lilu supplies instruction decoding and masked search. This small walk adds
// the property a byte search cannot establish: the site belongs to reachable
// code in this function, including blocks placed after an early return.
// Scratch storage is caller-owned (never a large kernel-stack allocation).
template<class Decode> size_t reachable(const uint8_t *bytes,size_t length,uint8_t *marks,
 uint16_t *pending,uintptr_t address,uintptr_t panic,Decode decode) {
 if(!bytes || !length || length>0x4000)return 0;
 memset(marks,0,length);size_t queued=1,end=0;pending[0]=0;
 while(queued) {
  size_t pos=pending[--queued];
  while(pos<length) {
   if(marks[pos]==2)return 0; // a branch into an instruction operand
   if(marks[pos]==1)break;
   // HDE predates Clang's UD1 trap emission. A guaranteed invalid-opcode
   // trap has no successor and is never a patch candidate.
   size_t trap=pos;
   while(trap<length && (bytes[trap]==0x66 || bytes[trap]==0x67 || (bytes[trap]&0xF0)==0x40))++trap;
   if(length-trap>=2 && bytes[trap]==0x0F && (bytes[trap+1]==0xB9 || bytes[trap+1]==0x0B)) {
    if(trap+2>end)end=trap+2;
    break;
   }
   const auto n=decode(bytes+pos,length-pos);
   if(!n || n>15 || n>length-pos)return 0;
   for(size_t k=1;k<n;++k){if(marks[pos+k])return 0;marks[pos+k]=2;}
   marks[pos]=1;if(pos+n>end)end=pos+n;
   const auto op=bytes[pos];
   if(n==5 && op==0xE8 && XELPCall::targets(bytes+pos,n,address+pos,panic))break;
   if(op==0xC3 || (op==0xC2 && n==3) || (n==2 && op==0x0F && bytes[pos+1]==0x0B))break;
   bool branch=false,conditional=false;int32_t delta=0;
   if(n==2 && (op==0xEB || (op>=0x70 && op<=0x7F))) {
    branch=true;conditional=op!=0xEB;delta=int8_t(bytes[pos+1]);
   } else if(n==5 && op==0xE9) {
    branch=true;memcpy(&delta,bytes+pos+1,4);
   } else if(n==6 && op==0x0F && (bytes[pos+1]&0xF0)==0x80) {
    branch=conditional=true;memcpy(&delta,bytes+pos+2,4);
   }
   if(branch) {
    const auto target=int64_t(pos+n)+delta;
    if(target<0 || target>=int64_t(length)) {
     if(conditional)return 0;
     break; // external tail call; do not scan the following function
    }
    if(queued>=length)return 0;
    pending[queued++]=uint16_t(target);
    if(!conditional)break;
   }
   // Do not infer jump-table targets or mark the following bytes as code.
   // A patch site must be reachable through resolved direct edges.
   // Indirect CALLs do have a fallthrough.
   size_t opcode=pos;
   if((op&0xF0)==0x40)++opcode;
   if(opcode+1<pos+n && bytes[opcode]==0xFF && (bytes[opcode+1]&0x38)==0x20)break;
   pos+=n;
   if(pos==length)return 0;
  }
 }
 return end;
}
template<class Find> bool findInstruction(const uint8_t *bytes,size_t length,const uint8_t *marks,
 const uint8_t *pattern,size_t count,size_t &offset,Find find,bool requireUnique=true) {
 size_t cursor=0,hits=0;
 while(find(pattern,nullptr,count,bytes,length,&cursor)) {
  if(marks[cursor]==1 && (cursor+count==length || marks[cursor+count]==1)) {offset=cursor;++hits;}
  ++cursor;
 }
 return requireUnique ? hits==1 : hits>0;
}
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
enum class SurfaceABI { Unknown, Original, Attachment6A, Attachment6E, RelocatedFields };
// These exported getters are the meanings of the fields inlined by the fixed
// donor. OS family names do not establish the host object layout.
inline bool surfaceGetter(const uint8_t *bytes,size_t length,const char *load,size_t size) {
 if(!bytes)return false;
 Cursor c{bytes,length,0};
 // Frame omission and alignment do not change this leaf accessor's ABI.
 auto framed=c;const bool frame=framed.take("\x55",1) && framed.take("\x48\x89\xe5",3);
 if(frame)c=framed;
 return c.take(load,size) && (!frame || c.take("\x5d",1)) && c.take("\xc3",1);
}
inline SurfaceABI surfaceABI(const uint8_t *const functions[4],const size_t lengths[4],bool framebuffer) {
 const auto getter=[&](unsigned i,const char *load,size_t n){return surfaceGetter(functions[i],lengths[i],load,n);};
 // Qualify only the fields this consumer reads. A matrix/plane change must
 // not disable the framebuffer, nor an ID change disable the accelerator.
 if(framebuffer) {
  if(getter(0,"\x8b\x87\xa4\0\0\0",6) && getter(2,"\x8b\x47\x0c",3))return SurfaceABI::Original;
  if(getter(0,"\x8b\x87\x80\0\0\0",6) && getter(2,"\x8b\x47\x10",3))return SurfaceABI::RelocatedFields;
  return SurfaceABI::Unknown;
 }
 if(getter(0,"\x8b\x87\xa4\0\0\0",6) && getter(1,"\x8b\x87\xb0\0\0\0",6) &&
    getter(3,"\x8b\x87\xb4\0\0\0",6))return SurfaceABI::Original;
 if(!getter(0,"\x8b\x87\x80\0\0\0",6) || !getter(1,"\x8b\x87\x98\0\0\0",6))return SurfaceABI::Unknown;
 if(getter(3,"\x48\x8b\x87\x78\x01\0\0\x0f\xb6\x40\x6a",11))return SurfaceABI::Attachment6A;
 if(getter(3,"\x48\x8b\x87\x78\x01\0\0\x0f\xb6\x40\x6e",11))return SurfaceABI::Attachment6E;
 return SurfaceABI::Unknown;
}
// Decode the verifier's first call only after proving both argument sources:
// original VA in RSI and symbol-resolved kernel_pmap_store in RDI.
inline uintptr_t rootCall(const uint8_t *bytes,size_t length,uintptr_t base,uintptr_t store,uintptr_t global=0) {
 if(!bytes || !base || !store || length>UINTPTR_MAX-base)return 0;
 Cursor c {bytes,length,0};
 if(!c.take("\x55",1) || !c.take("\x48\x89\xe5",3) || !c.take("\x53",1) ||
    !c.take("\x50",1) || !c.take("\x48\x89\xfb",3))return 0;
 auto argument=c;
 if(argument.take("\x48\x8d\x3d",3) && argument.skip(4) &&
    XELPCall::relativeTarget(bytes+argument.pos-4,base+argument.pos)==store)c=argument;
 else {
  argument=c;
  if(!global || !argument.take("\x48\x8b\x3d",3) || !argument.skip(4) ||
     XELPCall::relativeTarget(bytes+argument.pos-4,base+argument.pos)!=global)return 0;
  c=argument;
 }
 if(!c.take("\x48\x89\xde",3) || !c.take("\xe8",1) || !c.skip(4))return 0;
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
enum class ProtectABI { Legacy, Extended, Tahoe };
template<class Decode> bool protectCall(const uint8_t *bytes,size_t length,uintptr_t base,
 uintptr_t pmap,ProtectABI abi,Decode decode) {
 if(!bytes || !base || !pmap || length>UINTPTR_MAX-base)return false;
 const bool legacy=abi==ProtectABI::Legacy, extra=abi==ProtectABI::Extended;
 unsigned found=0;
 for(size_t pos=0;pos<length;) {
  const size_t size=decode(bytes+pos,length-pos);
  if(!size || size>15 || size>length-pos)return false;
  if(size==1 && bytes[pos]==0xc3)return found==1;
  Cursor c {bytes,length,pos};
  bool args=(extra || c.take("\x48\x83\xec\x08",4)) &&
   c.take(legacy?"\x48\x89\xdf":"\x4c\x89\xff",3) && c.take(legacy?"\x4c\x89\xe6":"\x4c\x89\xee",3) && c.take("\x89\xc2",2) &&
   c.take(legacy?"\x8b\x4d\xd0":"\x8b\x4d\xd4",3) && c.take(legacy?"\x44\x8b\x4d\xd4":"\x44\x8b\x4d\xd0",4) &&
   (!extra || c.take("\x6a\0",2)) && c.take("\x48\x8d\x45\xb8",4) &&
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
template<class Decode> bool protectCall(const uint8_t *bytes,size_t length,uintptr_t base,
 uintptr_t pmap,bool tahoe,Decode decode) {
 return protectCall(bytes,length,base,pmap,tahoe?ProtectABI::Tahoe:ProtectABI::Extended,decode);
}

}
#endif
