#include "kern_xelp_native_placement.hpp"
#include <cassert>
#include <vector>
#include <map>
#include <initializer_list>
#include <cstdio>
using Bytes=std::vector<uint8_t>;
struct Program {
 Bytes bytes;std::map<size_t,size_t> boundaries;
 size_t add(std::initializer_list<uint8_t> instruction) {
  auto start=bytes.size();boundaries[start]=instruction.size();
  bytes.insert(bytes.end(),instruction);return start;
 }
 void nop(unsigned n) {while(n--)add({0x90});}
 void relative(size_t at,uintptr_t base,uintptr_t target) {
  int64_t delta=static_cast<int64_t>(target)-static_cast<int64_t>(base+at+4);
  assert(delta>=INT32_MIN && delta<=INT32_MAX);
  int32_t d=static_cast<int32_t>(delta);memcpy(bytes.data()+at,&d,4);
 }
 // Independent instruction-boundary oracle for the deliberately assembled
 // fixtures. Native replay separately exercises Lilu's actual HDE decoder.
 size_t decode(const uint8_t *p,size_t) const {
  auto entry=boundaries.find(p-bytes.data());return entry==boundaries.end()?0:entry->second;
 }
 bool protect(uintptr_t base,uintptr_t target,bool tahoe) const {
  return XELPNative::protectCall(bytes.data(),bytes.size(),base,target,tahoe,
   [this](const uint8_t *p,size_t n){return decode(p,n);});
 }
 void arguments(uintptr_t base,uintptr_t target,bool tahoe,unsigned padding=0) {
  if(tahoe)add({0x48,0x83,0xec,8});
  add({0x4c,0x89,0xff});nop(padding);add({0x4c,0x89,0xee});add({0x89,0xc2});
  add({0x8b,0x4d,0xd4});add({0x44,0x8b,0x4d,0xd0});
  if(!tahoe)add({0x6a,0});
  add({0x48,0x8d,0x45,0xb8});add({0x50});add({0x6a,0x20});nop(padding);add({0x6a,1});
  auto call=add({0xe8,0,0,0,0});relative(call+1,base,target);
  add({0x48,0x83,0xc4,0x20});
 }
};
int main() {
 unsigned cases=0;
 for(unsigned n=0;n<256;++n) {
  uintptr_t base=0xffffff8000000000ULL+n*0x10000,store=base+0x678900-n*13,target=base-0x456780+n*17;
  Program p;p.nop(n%7);p.add({0x55});p.nop(n%5);p.add({0x48,0x89,0xe5});p.add({0x53});p.add({0x50});
  p.add({0x48,0x89,0xfb});p.nop(n%3);auto lea=p.add({0x48,0x8d,0x3d,0,0,0,0});p.relative(lea+3,base,store);
  p.nop(n%4);p.add({0x48,0x89,0xde});auto call=p.add({0xe8,0,0,0,0});p.relative(call+1,base,target);
  p.add({0x48,0x85,0xc0});
  assert(XELPNative::rootCall(p.bytes.data(),p.bytes.size(),base,store)==target);
  assert(!XELPNative::rootCall(p.bytes.data(),p.bytes.size(),base,store+1));
  assert(!XELPNative::rootCall(p.bytes.data(),call+4,base,store));
  p.bytes[lea+2]=0x35;assert(!XELPNative::rootCall(p.bytes.data(),p.bytes.size(),base,store));
  for(bool tahoe:{false,true}) {
   Program q;q.nop(n);q.arguments(base,target,tahoe,n%3);q.add({0xc3});
   assert(q.protect(base,target,tahoe));assert(!q.protect(base,target+1,tahoe));
   assert(!q.protect(base,target,!tahoe));
   // A second valid call before RET is ambiguous, not a second opportunity.
   Program duplicate;duplicate.arguments(base,target,tahoe);duplicate.arguments(base,target,tahoe);duplicate.add({0xc3});
   assert(!duplicate.protect(base,target,tahoe));
   q.bytes.back()=0x90;assert(!q.protect(base,target,tahoe));
   ++cases;
  }
 }
 // Instruction-looking data in an opaque instruction is never a call site.
 for(bool tahoe:{false,true}) {
  Program p;p.arguments(0x10000,0x20000,tahoe);p.boundaries.clear();p.boundaries[0]=p.bytes.size();p.add({0xc3});
  assert(!p.protect(0x10000,0x20000,tahoe));
  Program immediate;immediate.add({0x48,0xb8,0xe8,0,0,0,0,0x4c,0x89,0xff});immediate.add({0xc3});
  assert(!immediate.protect(0x10000,0x10007,tahoe));
  Program early;early.add({0xc3});early.arguments(0x10000,0x20000,tahoe);early.add({0xc3});
  assert(!early.protect(0x10000,0x20000,tahoe));
 }
 Program field;field.add({0x55});field.nop(3);field.add({0x48,0x89,0xe5});
 field.add({0x48,0xb8,0,0,0,0,0,0x80,0xff,0xff});field.nop(2);field.add({0x48,0x01,0xf0});
 field.add({0x48,0xc1,0xe8,0x30});field.add({0x3d,0xff,0xff,0,0});
 field.add({0x0f,0x82,0x12,0x34,0x56,0x78});field.nop(4);field.add({0x48,0x8b,0x47,0x50});
 assert(XELPNative::rootAccessor(field.bytes.data(),field.bytes.size()));
 field.bytes.back()=0x58;assert(!XELPNative::rootAccessor(field.bytes.data(),field.bytes.size()));
 assert(!XELPNative::rootCall(field.bytes.data(),field.bytes.size(),UINTPTR_MAX-8,1));
 puts("PASS: moved verifier arguments/calls/accessor field; wrong source, ABI, bounds and overflow rejected");
 printf("PASS: %u independently relocated protect call sites; duplicate, wrong-family/target and embedded bytes rejected\n",cases);
}
