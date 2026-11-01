// Pure packet/sequence engine; IO is supplied by an owning, serialized adapter.
// Intel icl_dsi.c/icl_dsi_regs.h (Linux v7.0) define credit and LP drain rules.
// No Apple objects, shared-resource allocation, or hardware MMIO lives here.
#ifndef KERN_DSI_HOST_HPP
#define KERN_DSI_HOST_HPP
#include "kern_dsi_sequence.hpp"
namespace XELP { namespace DSI {
enum class HostError : uint32_t {None, Disabled, Invalid, Cancelled, Unavailable, CreditTimeout, DrainTimeout, GPIO};
struct Packet {
    bool valid {};uint32_t header {};Span payload;
};
inline Packet packet(uint8_t type,Span data) {
    Packet p;
    if(!data.data || !data.size || data.size>256)return p;
    uint32_t parameters=0;
    if(type==0x05 && data.size==1)parameters=data.data[0];
    else if(type==0x15 && data.size==2)parameters=data.data[0]|uint32_t(data.data[1])<<8;
    else if(type==0x39){parameters=uint32_t(data.size);p.payload=data;}
    else return p;
    // Fresh header: VC0, LP, no stale fence/flush or short-packet payload bit.
    p.header=(p.payload.data?1U<<31:0)|(1U<<30)|(parameters<<8)|type;
    p.valid=true;return p;
}
struct Host {
    HostError error {HostError::None};
    bool enabled {},pending {},faulted {};
    uint32_t payloadWords {},headers {},completedOperations {},sequenceId {},operationOffset {};
    uint64_t waitedUs {};
    explicit Host(bool writable=false):enabled(writable) {}
    bool fail(HostError e) {if(error==HostError::None)error=e;faulted=true;return false;}
    // No implicit recovery/release after failure. Adapter must stop hardware,
    // retain uncertain leases, and construct a new Host only after recovery.
    template<class IO> bool ready(IO &io) {
        if(faulted)return false;
        if(!enabled)return fail(HostError::Disabled);
        if(io.cancelled())return fail(HostError::Cancelled);
        return true;
    }
    template<class IO> bool credits(IO &io,unsigned h,unsigned p,unsigned budgetUs=100) {
        if(budgetUs>200000 || budgetUs%10)return fail(HostError::Invalid);
        for(unsigned us=0;;us+=10) {
            if(!ready(io))return false;
            uint32_t status=0;
            if(!io.credits(status) || status==UINT32_MAX)return fail(HostError::Unavailable);
            const auto headers=(status>>8)&31,payload=status&255;
            if(headers>16 || payload>64)return fail(HostError::Unavailable);
            if(headers>=h && payload>=p)return true;
            if(us==budgetUs)return fail(HostError::CreditTimeout);
            io.delayUs(10);waitedUs+=10;
        }
    }
    template<class IO> bool send(IO &io,uint8_t type,Span data,unsigned creditBudgetUs=100) {
        const auto p=packet(type,data);
        if(!p.valid)return fail(HostError::Invalid);
        if(!ready(io))return false;
        // Reserve sufficient credit before the first payload write. The caller
        // serializes the only producer; hardware can only return more credit.
        if(!credits(io,1,unsigned((p.payload.size+3)/4),creditBudgetUs))return false;
        for(size_t at=0;at<p.payload.size;at+=4) {
            if(!ready(io))return false;
            uint32_t word=0;
            for(unsigned i=0;i<4 && at+i<p.payload.size;++i)word|=uint32_t(p.payload.data[at+i])<<(8*i);
            pending=true;io.payload(word);++payloadWords;
        }
        if(!ready(io))return false;
        pending=true;io.header(p.header);++headers;return true;
    }
    template<class IO> bool drain(IO &io,unsigned creditBudgetUs=100) {
        if(!ready(io))return false;
        if(!pending)return true;
        if(!credits(io,16,64,creditBudgetUs))return false;
        const uint8_t nop=0;
        if(!send(io,0x05,{&nop,1},creditBudgetUs) || !credits(io,16,0,creditBudgetUs))return false;
        for(unsigned us=0;;us+=2) {
            if(!ready(io))return false;
            bool busy=false;
            if(!io.lpBusy(busy))return fail(HostError::Unavailable);
            if(!busy){pending=false;return true;}
            if(us==20)return fail(HostError::DrainTimeout);
            io.delayUs(2);waitedUs+=2;
        }
    }
    template<class IO> bool delay(IO &io,uint32_t us) {
        while(us) {
            if(!ready(io))return false;
            const auto chunk=us>1000?1000:us;
            io.delayUs(chunk);waitedUs+=chunk;us-=chunk;
        }
        return ready(io);
    }
    template<class IO> bool run(IO &io,const Sequences &sequences,unsigned id,unsigned creditBudgetUs=100) {
        if(faulted)return false;
        if(!sequences.valid() || !id || id>=12 || creditBudgetUs>200000 || creditBudgetUs%10)return fail(HostError::Invalid);
        sequenceId=id;operationOffset=0;
        if(absentSequence(sequences.sequence[id]))return drain(io,creditBudgetUs);
        const auto bytes=sequences.sequence[id];Sequences check;
        if(validateSequence(bytes,check)!=SequenceStatus::Valid)return fail(HostError::Invalid);
        if(!ready(io))return false;
        for(size_t at=0;at<bytes.size;) {
            operationOffset=uint32_t(at);const auto op=bytes.data[at++];
            if(!op)return drain(io,creditBudgetUs);
            if(!ready(io))return false;
            const auto n=bytes.data[at++];const auto p=bytes.data+at;at+=n;
            if(op==1) {if(!send(io,p[1],{p+4,size_t(n-4)},creditBudgetUs))return false;}
            else {
                // Delay/GPIO must not overtake queued commands on the wire.
                if(!drain(io,creditBudgetUs))return false;
                if(op==2){if(!delay(io,sequenceU32(p)))return false;}
                else if(!io.gpio(p[1],p[2]))return fail(HostError::GPIO);
            }
            ++completedOperations;
        }
        return fail(HostError::Invalid);
    }
};
}}
#endif
