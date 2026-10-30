#include "kern_dsi_commands.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace XELP::DSI;
struct IO {
    bool owned=true,missingHeader=false,ulps=false,stuckCredits=false,stuckLP=false;
    unsigned delay=0,creditReads=0,lpReads=0,pending=0,deliveryUs=10;
    std::vector<uint32_t> regs,values;
    bool ready(){return owned;}
    bool readable(uint32_t r){return !(missingHeader && r==0x6B100);}
    uint32_t read(uint32_t r){
        if(r==0x6B0D0){++creditReads;return stuckCredits || pending?0:0x1040;}
        assert(r==0x6B0D8);++lpReads;return ulps?0x10001:stuckLP?0x20000:0;
    }
    void write(uint32_t r,uint32_t v){assert(owned);regs.push_back(r);values.push_back(v);if(r==0x6B100)pending=deliveryUs;}
    void delayUs(unsigned us){delay+=us;pending=pending>us?pending-us:0;}
};
int main(){
 IO io;CommandPort<IO> port{io};Host host(true);uint8_t nop=0;
 assert(host.send(port,0x05,{&nop,1}) && host.drain(port));
 assert(io.regs==std::vector<uint32_t>({0x6B100,0x6B100}));
 assert(io.values==std::vector<uint32_t>({0x40000005,0x40000005}));
 assert(port.writes==2 && host.headers==2 && !host.pending && host.waitedUs==20);
 IO longIO;CommandPort<IO> longPort{longIO};Host longHost(true);uint8_t bytes[]={0xB0,1,2,3,4};
 assert(longHost.send(longPort,0x39,{bytes,5}) && longHost.drain(longPort));
 assert(longIO.regs==std::vector<uint32_t>({0x6B104,0x6B104,0x6B100,0x6B100}));
 assert(longIO.values==std::vector<uint32_t>({0x030201B0,4,0xC0000539,0x40000005}));
 IO video;video.deliveryUs=16000;CommandPort<IO> vp{video};Host vh(true);
 assert(vh.send(vp,0x05,{&nop,1}) && vh.drain(vp,50010) && vh.waitedUs==32000);
 for(unsigned failure=0;failure<5;++failure){
  IO bad;CommandPort<IO> p{bad};Host h(true);
  if(failure==0)bad.owned=false;
  if(failure==1)bad.missingHeader=true;
  if(failure==2)bad.stuckCredits=true;
  if(failure==3)bad.ulps=true;
  if(failure==4)bad.stuckLP=true;
  assert(!(h.send(p,0x05,{&nop,1}) && h.drain(p)));
  assert(h.faulted);const auto count=p.writes;
  assert(!h.send(p,0x05,{&nop,1}) && p.writes==count);
  if(failure<3)assert(!p.writes);
  if(failure==2)assert(h.error==HostError::CreditTimeout && bad.delay==100);
  if(failure==3)assert(h.error==HostError::Unavailable);
  if(failure==4)assert(h.error==HostError::DrainTimeout && bad.delay==40);
 }
 puts("PASS command MMIO adapter: exact LP packet words, payload before header, credit/drain waits, ownership and sticky failures");
}
