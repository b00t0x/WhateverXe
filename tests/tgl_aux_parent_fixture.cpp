#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <vector>
#include "kern_tgl_tc_boot.hpp"
struct IOService;
struct XE {
 bool adlsDisplaySelected=false;struct {bool valid=false;unsigned dp=0,panel=0;}adlsWiring;
 // AUX_FIELDS
 uintptr_t tglPortByDDI, tglEnablePG, tglDisablePG;
 uintptr_t orgTGLEnableAUXPower, orgTGLDisableAUXPower;
 static void wrapTGLEnableAUXPower(void *, uint32_t);
 static void wrapTGLDisableAUXPower(void *, uint32_t);
};
XE object, *callbackXE = &object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
void tglAUXRecord(void *,uint32_t,bool,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t=0) {}
// AUX_WRAPPERS
struct Power { alignas(8) uint8_t data[0x90] {}; } primary, other;
uint32_t &native(void *p,unsigned ddi) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+0x54+4*ddi); }
uint32_t &pg(void *p,unsigned i) { return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(p)+0x1c+4*i); }
std::vector<unsigned> calls;
void *portByDDI(unsigned ddi) { assert(ddi<9); return reinterpret_cast<void *>(uintptr_t(ddi+1)); }
void getPG(void *p,unsigned index) { assert(index==3);calls.push_back(1);for(unsigned i=1;i<=3;++i)++pg(p,i); }
void putPG(void *p,unsigned index) { assert(index==3);calls.push_back(4);for(unsigned i=1;i<=3;++i){assert(pg(p,i));--pg(p,i);} }
void enable(void *p,unsigned ddi) { assert(ddi<9);calls.push_back(2);++native(p,ddi); }
void disable(void *p,unsigned ddi) { assert(ddi<9);calls.push_back(3);if(native(p,ddi))--native(p,ddi); }
#if LEGACY
uint32_t owned(unsigned) { return object.tglAUXParentRefs; }
#else
uint32_t owned(unsigned ddi) { return object.tglAUXParentRefs[ddi]; }
void qualify(uint32_t mask){
 object.tglAUXParentMask=mask;
#if WIRING
 object.tglTCWiring=TGLTCWiring::fromMasks(mask,0,0);
#endif
}
void checkSum() {
 unsigned sum=0;for(unsigned d=0;d<9;++d)sum+=owned(d);
 for(unsigned i=1;i<=3;++i)assert(pg(primary.data,i)==sum);
}
#endif
void reset() {
 object=XE{};object.tglAUXParentReady=true;
#if !LEGACY
 qualify(1u<<4); // explicit fixture wiring/backend, independent of product defaults
#endif
 object.tglPortByDDI=(uintptr_t)portByDDI;object.tglEnablePG=(uintptr_t)getPG;object.tglDisablePG=(uintptr_t)putPG;
 object.orgTGLEnableAUXPower=(uintptr_t)enable;object.orgTGLDisableAUXPower=(uintptr_t)disable;
 primary=Power{};other=Power{};calls.clear();
}
void get(unsigned ddi) { calls.clear();XE::wrapTGLEnableAUXPower(primary.data,ddi);assert((calls==std::vector<unsigned>{1,2})); }
void put(unsigned ddi,bool release=true) {
 calls.clear();XE::wrapTGLDisableAUXPower(primary.data,ddi);
 assert(calls==(release?std::vector<unsigned>{3,4}:std::vector<unsigned>{3}));
}
int main() {
 reset();native(primary.data,4)=1; // inherited ref precedes an owned ref
 get(4);assert(native(primary.data,4)==2 && owned(4)==1);
 // Keep PG until the remaining native AUX ref is gone; old scalar logic
 // drops PG here while AUX still has one reference.
 calls.clear();XE::wrapTGLDisableAUXPower(primary.data,4);
 assert(native(primary.data,4)==1);
 assert(pg(primary.data,3)==1 && owned(4)==1);
 put(4);assert(native(primary.data,4)==0 && owned(4)==0);put(4,false);
#if !LEGACY
 unsigned scenarios=0;
 for(unsigned a=3;a<7;++a)for(unsigned b=a+1;b<8;++b)for(unsigned c=b+1;c<9;++c) {
  unsigned ins[]={a,b,c};do {
   unsigned outs[]={a,b,c};do {
    reset();qualify((1u<<a)|(1u<<b)|(1u<<c));
    for(auto d:ins){get(d);get(d);checkSum();}
    for(auto d:outs){put(d);assert(owned(d)==1);checkSum();put(d);assert(owned(d)==0);checkSum();}
    ++scenarios;
   }while(std::next_permutation(outs,outs+3));
  }while(std::next_permutation(ins,ins+3));
 }
 reset();qualify((1u<<3)|(1u<<4)|(1u<<8));
 get(3);get(4);native(primary.data,8)=1;
 put(8,false);assert(owned(3)==1 && owned(4)==1);checkSum();
 // Several inherited refs on a port with owned refs must not consume them.
 native(primary.data,4)+=2;put(4,false);put(4,false);
 assert(owned(4)==1);checkSum();put(4);put(3);checkSum();
 // Other power objects cannot spend the original object's reference.
 get(4);native(other.data,4)=1;calls.clear();XE::wrapTGLDisableAUXPower(other.data,4);
 assert((calls==std::vector<unsigned>{3}) && owned(4)==1);checkSum();put(4);
 // This fixture qualifies only TC2. Other valid DDIs retain native-only calls.
 reset();for(unsigned d=0;d<9;++d)if(d!=4) {
  calls.clear();XE::wrapTGLEnableAUXPower(primary.data,d);XE::wrapTGLDisableAUXPower(primary.data,d);
  assert((calls==std::vector<unsigned>{2,3}) && owned(d)==0);
 }
 assert(!object.tglAUXParentEligible(9) && !object.tglAUXParentEligible(~0u));
#if WIRING
 // Automatic registration starts after the first mode4 probe. A previously
 // inherited AUX ref gets no retrospective PG token; preserve symmetry when
 // it drains before our new reference, including a disconnected port.
 reset();object.tglTCWiring=TGLTCWiring::fromMasks(0,0,0);object.tglTCAutoCandidates=0x10;
 XE::wrapTGLEnableAUXPower(primary.data,4);assert(native(primary.data,4)==1&&!owned(4));
 object.tglTCAutoRegistered=0x10;get(4);put(4,false);checkSum();put(4);checkSum();
 // Neither a backend bit alone nor a wiring bit alone widens admission.
 object.tglAUXParentMask|=1u<<5;assert(!object.tglAUXParentEligible(5));
 object.tglTCWiring=TGLTCWiring::fromMasks((1u<<4)|(1u<<6),0,0);
 assert(!object.tglAUXParentEligible(6)&&object.tglAUXParentEligible(4));
#endif
 object.tglAUXParentReady=false;calls.clear();
 XE::wrapTGLEnableAUXPower(primary.data,4);XE::wrapTGLDisableAUXPower(primary.data,4);
 assert((calls==std::vector<unsigned>{2,3}));
 printf("PASS: %u 3-port nested acquisition/release orders across all 6 TC ports; mixed inherited refs, cross-port/object isolation, call order and unchanged live gate (serial native contract)\n",scenarios);
#endif
}
