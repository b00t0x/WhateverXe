#pragma once
#include <stdint.h>
// Passive copies of accesses the driver already performs. Never reads MMIO,
// changes a returned value, acquires a driver lock, or persists from an accessor.
namespace ShutdownTCObservation {
struct State {
 bool enabled {};
 uint32_t sequence {}, mode0[4] {}, modeResult[4] {};
 uint64_t reads[4] {}, writes[4] {}; // FIA1..3 ownership, FIA1 topology
};
// -fapple-kext does not coalesce inline-function local statics across TUs.
extern State storage;
inline State &state() {return storage;}
inline unsigned index(uint32_t reg) {
 if(reg==0x1638A0)return 3;
 if(reg>=0x163894 && reg<=0x163A94 && !((reg-0x163894)%0x100))return (reg-0x163894)/0x100;
 return 4;
}
inline void record(uint32_t reg,uint32_t value,bool write) {
 auto &s=state();if(!s.enabled)return;
 const auto i=index(reg);if(i==4)return;
 const auto seq=__atomic_add_fetch(&s.sequence,1,__ATOMIC_RELAXED);
 __atomic_store_n((write?s.writes:s.reads)+i,(static_cast<uint64_t>(seq)<<32)|value,__ATOMIC_RELEASE);
}
inline uint64_t sample(uint32_t reg,bool write) {
 const auto i=index(reg);if(i==4)return 0;
 auto &s=state();return __atomic_load_n((write?s.writes:s.reads)+i,__ATOMIC_ACQUIRE);
}
inline void mode(uint32_t ddi,uint32_t wanted,uint32_t result) {
 auto &s=state();if(!s.enabled || ddi<3 || ddi>6)return;
 if(!wanted)__atomic_add_fetch(s.mode0+ddi-3,1,__ATOMIC_RELAXED);
 __atomic_store_n(s.modeResult+ddi-3,result,__ATOMIC_RELEASE);
}
}
