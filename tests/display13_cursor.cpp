#include "kern_display13_cursor.hpp"
#include <assert.h>
#include <map>
#include <vector>
#include <stdio.h>
struct IO {
    std::map<uint32_t,uint32_t> regs {{0x45404,0xFC0F},{0x70008,0xC0000000},{0x70080,0x27},{0x70040,10},{0x70084,0x6000000},{0x70088,1},{0x70B00,0x80001000},{0x70C00,0x100000},{0x70D00,0},{0x70B08,0},{0x70C08,0},{0x70D08,0}};
    std::vector<uint32_t> writes;bool frozen {},drop {},losePower {};unsigned elapsed {};
    std::vector<uint32_t> reads;
    bool readable(uint32_t r) {return r<0x1000000;}
    uint32_t read(uint32_t r) {assert(regs.count(r));reads.push_back(r);return regs[r];}
    void write(uint32_t r,uint32_t v) {assert(regs.count(r));writes.push_back(r);if(!drop)regs[r]=v;}
    void delayUs(unsigned us) {
        elapsed+=us;if(!frozen && elapsed>=17000)for(unsigned p=0;p<3;++p)
            if(regs.count(0x70040+p*0x1000))regs[0x70040+p*0x1000]=11;
        if(losePower)regs[0x45404]=3;
    }
};
int main() {
    using namespace Display13Cursor;
    for(unsigned pipe=0;pipe<3;++pipe) {
        const auto reg=0x70080+0x1000*pipe;
        // All active hardware modes in Linux v6.12 intel_cursor_regs.h,
        // including the 256px ARGB mode used by larger cursor images.
        for(auto mode:{2U,3U,4U,7U,0x22U,0x23U,0x27U}) {
            for(unsigned slots=0;slots<8;++slots) {
                const auto value=mode|(1U<<15)|(slots<<28);
                assert(controlWrite(reg,value)==(mode|(1U<<15)|(1U<<28)));
            }
        }
        assert(controlWrite(reg,0)==0 && controlWrite(reg,1U<<28)==(1U<<28));
        assert(controlWrite(reg+4,0x27)==0x27 && controlWrite(reg+8,0x27)==0x27);
    }
    assert(controlWrite(0x73080,0x27)==0x27 && controlWrite(UINT64_MAX,0x27)==0x27);
    IO a;auto r=stop(a);assert(r.status==Status::Complete && r.waitUs==17000 && r.frameAfter==11);
    assert((a.writes==std::vector<uint32_t>{0x70080,0x70088,0x70084}));assert(!a.regs[0x70084]);
    IO off;off.regs[0x70008]=0;r=stop(off);assert(r.status==Status::Complete && !r.waitUs && r.writes==3);
    // PIPECONF=0 on DSI is not evidence of stopped scanout. Cursor retirement
    // still needs the separately bound pipe's frame to advance.
    {IO io;io.regs[0x70008]=0;io.regs[0x7B008]=0xC0000000;io.regs[0x6B400]=0x80000006;
     r=stop(io,0,{0});assert(r.status==Status::Complete && r.waitUs==17000);
     IO frozen;frozen.regs=io.regs;frozen.regs[0x70080]=0x27;frozen.frozen=true;
     assert(stop(frozen,0,{0}).status==Status::Timeout);
     IO wrong;wrong.regs[0x70008]=0;wrong.regs[0x7B008]=0xC0000000;wrong.regs[0x6B400]=0x80005006;
     assert(stop(wrong,0,{0}).status==Status::Readback && wrong.writes.empty());}
    IO empty;empty.regs[0x70080]=0;r=stop(empty);assert(r.status==Status::Complete && empty.writes.empty());
    IO busy;busy.regs[0x70C08]=1;r=stop(busy);assert(r.status==Status::Conflict && busy.writes.empty());
    IO power;power.regs[0x45404]=3;r=stop(power);assert(r.status==Status::Power && power.writes.empty());
    IO freeze;freeze.frozen=true;r=stop(freeze);assert(r.status==Status::Timeout && r.waitUs==50000);
    IO dropped;dropped.drop=true;r=stop(dropped);assert(r.status==Status::Timeout && r.after==0x27);
    IO lost;lost.losePower=true;r=stop(lost);assert(r.status==Status::Readback);
    IO invalid;invalid.regs[0x70080]=UINT32_MAX;r=stop(invalid);assert(r.status==Status::Readback && invalid.writes.empty());
    // A/B/C pipe addressing is independent of the board's A/B connector mask.
    // Stopping one cursor must not read or write another pipe's consumer bank.
    for(unsigned pipe=0;pipe<3;++pipe) {
        IO io;const IO baseline;const auto bank=pipe*0x1000;
        for(auto pair:baseline.regs)if(pair.first!=0x45404)io.regs[pair.first+bank]=pair.second;
        const auto result=stop(io,pipe);assert(result.status==Status::Complete && result.waitUs==17000);
        assert((io.writes==std::vector<uint32_t>{0x70080+bank,0x70088+bank,0x70084+bank}));
        for(auto reg:io.reads)assert(reg==0x45404 || (reg>=0x70000+bank && reg<0x71000+bank));
        if(pipe)assert(io.regs[0x70080]==0x27 && io.regs[0x70084]==0x6000000 && io.regs[0x70088]==1);
        IO unpowered;unpowered.regs[0x45404]&=~(1U<<(10+2*pipe));
        assert(stop(unpowered,pipe).status==Status::Power && unpowered.writes.empty());
        assert((unpowered.reads==std::vector<uint32_t>{0x45404}));
        IO pending;for(auto pair:baseline.regs)if(pair.first!=0x45404)pending.regs[pair.first+bank]=pair.second;
        pending.regs[0x70D08+bank]=1;assert(stop(pending,pipe).status==Status::Conflict && pending.writes.empty());
        IO enabled;for(auto pair:baseline.regs)if(pair.first!=0x45404)enabled.regs[pair.first+bank]=pair.second;
        enabled.regs[0x70B08+bank]=0x80000000;
        assert(stop(enabled,pipe).status==Status::Conflict && enabled.writes.empty());
    }
    for(auto pipe:{3U,UINT32_MAX}) {IO io;assert(stop(io,pipe).status==Status::Conflict && io.reads.empty() && io.writes.empty());}
    puts("AlderLakeN cursor stop fixtures passed");
}
