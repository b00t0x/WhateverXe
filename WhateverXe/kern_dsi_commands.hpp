// Intel icl_dsi.c / icl_dsi_regs.h v7.0 command transport. The owning adapter
// serializes the sole producer and holds the host's power/clock throughout.
#ifndef KERN_DSI_COMMANDS_HPP
#define KERN_DSI_COMMANDS_HPP
#include "kern_dsi_host.hpp"
namespace XELP { namespace DSI {
template<class IO> struct CommandPort {
    IO &io;
    bool failed {};uint32_t writes {};
    bool cancelled(){return failed || !io.ready();}
    bool read(uint32_t reg,uint32_t &value) {
        if(cancelled() || !io.readable(reg))return false;
        value=io.read(reg);return value!=UINT32_MAX;
    }
    void write(uint32_t reg,uint32_t value) {
        if(cancelled() || !io.readable(reg)){failed=true;return;}
        io.write(reg,value);++writes;
    }
    bool credits(uint32_t &value){return read(0x6B0D0,value);}
    bool lpBusy(bool &busy) {
        uint32_t value=0;if(!read(0x6B0D8,value) || (value&((1U<<16)|1U)))return false;
        busy=value&(1U<<17);return true;
    }
    void payload(uint32_t value){write(0x6B104,value);}
    void header(uint32_t value){write(0x6B100,value);}
    void delayUs(unsigned us){io.delayUs(us);}
};
}}
#endif
