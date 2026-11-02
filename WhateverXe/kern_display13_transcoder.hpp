// Display pipe and timing transcoder are independent identities. The owner
// installs this mapping only after DSI readout/resource qualification, and
// changes it only while affected callers/IRQs are quiesced. Default is ordinary
// A/B/C timing, with no DSI MMIO reads. This is not an MMIO address translator.
#ifndef KERN_DISPLAY13_TRANSCODER_HPP
#define KERN_DISPLAY13_TRANSCODER_HPP
#include <stdint.h>
namespace Display13Transcoder {
struct Bindings {
    uint32_t dsi0Pipe {UINT32_MAX};
    bool valid() const {return dsi0Pipe==UINT32_MAX || dsi0Pipe<3;}
    bool dsi(unsigned pipe) const {return pipe<3 && pipe==dsi0Pipe;}
    uint32_t control(unsigned pipe) const {return dsi(pipe)?0x7B008U:0x70008U+0x1000*pipe;}
    uint32_t function(unsigned pipe) const {return dsi(pipe)?0x6B400U:0x60400U+0x1000*pipe;}
    uint32_t timing(unsigned pipe) const {return dsi(pipe)?0x6B000U:0x60000U+0x1000*pipe;}
};
// Caller proves the pipe and DSI bank's power lifetime. Normal PIPECONF must
// remain disabled for an owned DSI route. Check the live input selector as
// well as the binding: a stale CPU mapping must not qualify another pipe.
// A partial enable/active pair is returned intact for stop/wait consumers;
// callers requiring running scanout must require both bits.
template<class IO> uint32_t control(IO &io,unsigned pipe,const Bindings &bindings={}) {
    if(pipe>=3 || !bindings.valid())return UINT32_MAX;
    const auto normal=io.read(0x70008+0x1000*pipe);
    if(!bindings.dsi(pipe) || normal==UINT32_MAX)return normal;
    if(normal&0xC0000000U)return UINT32_MAX;
    const auto conf=io.read(0x7B008),function=io.read(0x6B400);
    const uint32_t inputs[]={0,5,6};
    if(conf==UINT32_MAX || function==UINT32_MAX || ((function>>12)&7)!=inputs[pipe] ||
       ((conf&0xC0000000U) && !(function&0x80000000U)))return UINT32_MAX;
    return conf;
}
template<class IO> bool running(IO &io,unsigned pipe,const Bindings &bindings={}) {
    const auto conf=control(io,pipe,bindings);
    const auto mask=bindings.dsi(pipe)?0xC0000000U:0x80000000U;
    return conf!=UINT32_MAX && (conf&mask)==mask;
}
}
#endif
