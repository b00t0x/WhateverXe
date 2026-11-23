// Gen11+ bank1 shares one DATA_VALID latch between VCS and VECS. A valid
// identity is acknowledged before the next selector, and routed by its own
// class/instance. Linux v6.12 intel_gt_irq.c defines the latch/bank ordering;
// event numbers and mask ownership are from the fixed TGL donor.
#ifndef KERN_XELP_VIDEO_IRQ_HPP
#define KERN_XELP_VIDEO_IRQ_HPP
#include <stdint.h>
namespace XELPVideoIRQ {
constexpr uint32_t Selector=0x190074,Identity=0x190064,Valid=1U<<31;
constexpr uint32_t Bank=0x19001c,Owned=5U|(1U<<31);
constexpr uint32_t bits[3]={1,4,1U<<31};
constexpr uint32_t masks[3]={0x1900a8,0x1900ac,0x1900d0};
struct State {
    uint32_t pending {},saved[3] {},delayMS {1},present {Owned};
    uint64_t timeouts {},foreign {},unknown {},retries {},resolved {},retired {};
    uint32_t lastIdentity {},lastSelector {};
};
inline int slot(uint32_t identity) {
    const auto cls=(identity>>16)&7,instance=(identity>>20)&63;
    return cls==1 && instance==0 ? 0 : cls==1 && instance==2 ? 1 :
        cls==2 && instance==0 ? 2 : -1;
}
inline void dispatch(unsigned n,uint32_t identity,uint64_t &events) {
    if(identity&1)events|=1ULL<<(3+n);
    if(identity&0x10)events|=1ULL<<(15+n);
    if(identity&0x100)events|=1ULL<<(9+n);
}
// Caller serializes the shared latch and bank snapshot, not just one engine.
// Unresolved selectors survive the parent W1C as software obligations. Masking
// only their low 16 bits prevents a stuck producer from spinning the IRQ line.
// The fixed donor changes these masks only in class-enable, which starts a new
// epoch. Requestor enable/disable changes counts, not these mask registers.
// A timer retry temporarily restores the saved bits before selecting: it can
// acquire a late identity even if masked engines do not populate the latch.
template<class IO> void service(IO &io,State &s,uint32_t snapshot,uint64_t &events,bool retry) {
    const auto old=s.pending;
    if(retry) {
        ++s.retries;
        for(unsigned n=0;n<3;++n)if(old&bits[n])
            io.write(masks[n],(io.read(masks[n])&0xffff0000U)|s.saved[n]);
    }
    uint32_t todo=(snapshot&Owned)|old,completed=0;
    if(!retry)todo&=~old; // The timer owns quarantined selectors.
    for(unsigned n=0;n<3;++n) {
        if(!(todo&bits[n]) || (completed&bits[n]))continue;
        const auto start=io.now();
        unsigned reads=0,identities=0;
        bool matched=false;
        while(reads<4096 && identities<4) {
            io.write(Selector,bits[n]);
            uint32_t identity=0;
            do {
                identity=io.read(Identity);++reads;
            } while(!(identity&Valid) && reads<4096 && io.now()-start<100000);
            s.lastIdentity=identity;s.lastSelector=bits[n];
            if(!(identity&Valid)) {++s.timeouts;break;}
            io.write(Identity,Valid);++identities;
            int actual=slot(identity);
            if(actual>=0 && !(s.present&bits[actual]))actual=-1;
            if(actual>=0) {
                // A late/stale peer identity still belongs to its actual
                // consumer. Never reinterpret it as the selected engine.
                dispatch(actual,identity,events);
                completed|=bits[actual];
                if(s.pending&bits[actual]) {
                    io.write(masks[actual],(io.read(masks[actual])&0xffff0000U)|s.saved[actual]);
                    s.pending&=~bits[actual];++s.resolved;
                }
            } else ++s.unknown;
            if(actual==int(n)) {matched=true;break;}
            ++s.foreign;
            if(io.now()-start>=100000)break;
        }
        if(!matched && !(completed&bits[n])) {
            if(!(s.pending&bits[n]))s.saved[n]=io.read(masks[n])&0xffff;
            s.pending|=bits[n];
        }
    }
    // Reapply quarantine after peer handling, before the parent bank unlock.
    for(unsigned n=0;n<3;++n)if(s.pending&bits[n])
        io.write(masks[n],io.read(masks[n])|0xffffU);
    // The bank read locks its snapshot. Always release that snapshot, including
    // faults, AFTER taking responsibility for every unresolved owned selector.
    io.write(Bank,snapshot);
    s.delayMS=s.pending ? (retry && s.delayMS<100 ? (s.delayMS<50?s.delayMS*2:100) : s.delayMS) : 1;
}
inline void retire(State &s) {
    for(auto bit:bits)if(s.pending&bit)++s.retired;
    s.pending=0;s.delayMS=1;
}
}
#endif
