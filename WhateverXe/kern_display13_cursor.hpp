// Display 13 selected-pipe cursor shutdown before native pipe teardown.
#ifndef KERN_DISPLAY13_CURSOR_HPP
#define KERN_DISPLAY13_CURSOR_HPP
#include "kern_display13_fb_irq.hpp"
namespace Display13Cursor {
// Linux v6.12 intel_cursor.c i9xx_cursor_ctl, Wa_22012358565:adl-p.
// Display 13 active cursors require one arbitration slot. Native Display 12
// writers omit it. Preserve disabled controls and every unrelated field/bank.
inline uint32_t controlWrite(uint64_t reg,uint32_t value) {
    if(reg<0x70080 || reg>0x72080 || (reg-0x70080)%0x1000 || !(value&0x27))return value;
    return (value&~(7U<<28))|(1U<<28);
}
enum class Status : uint32_t {Unavailable,Complete,Power,Conflict,Readback,Timeout};
struct Result {Status status {Status::Unavailable};uint32_t before {},after {},frameBefore {},frameAfter {},writes {},waitUs {};};
template<class IO> Result stop(IO &io,unsigned pipeIndex=0,const Display13Transcoder::Bindings &transcoders={}) {
    Result r;
    // This is a display pipe index, independent of HDMI A/B DDI identity.
    // The caller proves the path/FB association and limits its writer to this bank.
    if(pipeIndex>=3) {r.status=Status::Conflict;return r;}
    const uint32_t bank=pipeIndex*0x1000;
    if(!io.readable(0x700AC+bank) || !Display13FBIRQ::powered(io.read(0x45404),pipeIndex)) {r.status=Status::Power;return r;}
    const auto pipe=Display13Transcoder::control(io,pipeIndex,transcoders);r.before=io.read(0x70080+bank);
    if(pipe==UINT32_MAX || r.before==UINT32_MAX) {r.status=Status::Readback;return r;}
    for(unsigned d=0;d<3;++d) {
        const auto dsb=io.read(Display13FBIRQ::DSBControl(pipeIndex,d));
        if(dsb==UINT32_MAX || (dsb&0x80000001)) {r.status=Status::Conflict;return r;}
    }
    if(!(r.before&63)) {r.after=r.before;r.status=Status::Complete;return r;}
    r.frameBefore=io.read(0x70040+bank);
    if(r.frameBefore==UINT32_MAX) {r.status=Status::Readback;return r;}
    // CURBASE arms CURCNTR/CURPOS, including the disabled cursor state.
    io.write(0x70080+bank,0);io.write(0x70088+bank,0);io.write(0x70084+bank,0);r.writes=3;
    for(unsigned us=0;us<=50000;us+=1000) {
        r.after=io.read(0x70080+bank);r.frameAfter=io.read(0x70040+bank);r.waitUs=us;
        if(r.after==UINT32_MAX || r.frameAfter==UINT32_MAX || !Display13FBIRQ::powered(io.read(0x45404),pipeIndex)) {r.status=Status::Readback;return r;}
        if(!(r.after&63) && (!(pipe&0xC0000000) || r.frameAfter!=r.frameBefore)) {r.status=Status::Complete;return r;}
        if(us<50000)io.delayUs(1000);
    }
    r.status=Status::Timeout;return r;
}
}
#endif
