#include "kern_dsi_host.hpp"
#include "kern_dsi_profile.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace XELP::DSI;
struct Mock {
    std::vector<uint32_t> h,p;
    uint32_t credit=0x1040,elapsed=0,gpios=0,checks=0,cancelAt=UINT32_MAX;
    bool busy=false,available=true,gpioOK=true;
    bool cancelled(){return ++checks>=cancelAt;}
    bool credits(uint32_t &v){v=credit;return available;}
    bool lpBusy(bool &v){v=busy;return available;}
    void header(uint32_t v){h.push_back(v);}
    void payload(uint32_t v){p.push_back(v);}
    void delayUs(uint32_t us){elapsed+=us;}
    bool gpio(uint8_t role,uint8_t value){assert(nativePanelGPIO(role)&&value<=1);++gpios;return gpioOK;}
};
struct VideoMock:Mock {
    uint32_t releaseAt=0;
    bool credits(uint32_t &v){v=elapsed<releaseAt?0xF40:0x1040;return true;}
    void header(uint32_t v){Mock::header(v);releaseAt=elapsed+1000;}
};
int main(int argc,char **argv) {
    uint8_t data[]={0x29,0x12,0x34,0x56,0x78};
    assert(packet(5,{data,1}).header==0x40002905);
    assert(packet(0x15,{data,2}).header==0x40122915);
    assert(packet(0x39,{data,5}).header==0xC0000539);
    assert(!packet(6,{data,1}).valid&&!packet(5,{nullptr,1}).valid&&!packet(0x39,{data,257}).valid);
    Mock io;Host off;assert(!off.send(io,5,{data,1})&&off.error==HostError::Disabled&&io.h.empty());
    Host ok(true);assert(ok.send(io,0x39,{data,5})&&ok.pending);
    assert(io.p.size()==2&&io.p[0]==0x56341229&&io.p[1]==0x78);
    assert(ok.drain(io)&&!ok.pending&&io.h.size()==2&&io.h.back()==0x40000005);
    for(auto credit:{0U,0x1000U,0x0040U}) {
        Mock stalled;stalled.credit=credit;Host h(true);
        assert(!h.send(stalled,0x39,{data,5})&&h.error==HostError::CreditTimeout);
        assert(stalled.elapsed==100&&stalled.h.empty()&&stalled.p.empty());
    }
    Mock busy;busy.busy=true;Host h(true);assert(h.send(busy,5,{data,1}));
    assert(!h.drain(busy)&&h.error==HostError::DrainTimeout&&h.pending&&busy.elapsed==20);
    const auto count=busy.h.size();assert(!h.send(busy,5,{data,1})&&busy.h.size()==count); // fault latches
    Mock missing;missing.available=false;Host unavailable(true);
    assert(!unavailable.send(missing,5,{data,1})&&unavailable.error==HostError::Unavailable);
    uint8_t ops[]={1,9,0,0x39,5,0,0x29,0x12,0x34,0x56,0x78,2,4,0xD0,7,0,0,3,3,0,2,1,0};
    Sequences s;s.status=SequenceStatus::Valid;s.sequence[2]={ops,sizeof(ops)};
    Mock complete;Host runner(true);assert(runner.run(complete,s,2));
    assert(runner.completedOperations==3&&complete.elapsed==2000&&complete.gpios==1&&!runner.pending);
    {VideoMock io;Host host(true);assert(!host.run(io,s,2));
     assert(host.error==HostError::CreditTimeout && io.elapsed==100 && !io.gpios);}
    {VideoMock io;Host host(true);assert(host.run(io,s,2,2000));
     assert(!host.pending && io.elapsed==4000 && io.gpios==1);}
    for(unsigned budget:{1U,200010U}) {
        Mock io;Host host(true);assert(!host.run(io,s,2,budget));
        assert(host.error==HostError::Invalid && io.h.empty() && !io.gpios);
    }
    for(unsigned id:{1U,3U,4U,5U,6U,7U,8U,9U,10U,11U}) {
        Mock io;Host host(true);assert(host.run(io,s,id) && !host.pending && io.h.empty() && !io.gpios);
        Host disabled;assert(!disabled.run(io,s,id));
    }
    {Mock io;Host host(true);assert(host.send(io,5,{data,1}));
     assert(host.run(io,s,4) && !host.pending && io.h.size()==2 && host.sequenceId==4);}
    {Mock io;io.cancelAt=1;Host host(true);assert(!host.run(io,s,4) && host.error==HostError::Cancelled);}
    for(unsigned id:{0U,12U}){Mock io;Host host(true);assert(!host.run(io,s,id));}
    {Mock io;Host host(true);auto bad=s;bad.sequence[4]={nullptr,1};assert(!host.run(io,bad,4));}
    // Cancellation at every callback boundary must latch first failure and
    // never submit a later GPIO/header after returning an error.
    for(unsigned at=1;at<=complete.checks;++at) {
        Mock cancelled;cancelled.cancelAt=at;Host stopped(true);
        assert(!stopped.run(cancelled,s,2)&&stopped.error==HostError::Cancelled);
        const auto headers=cancelled.h.size(),words=cancelled.p.size();const auto gpios=cancelled.gpios;
        const auto sequence=stopped.sequenceId,offset=stopped.operationOffset;
        assert(!stopped.run(cancelled,s,4)); // an absent slot cannot erase the first fault
        assert(stopped.sequenceId==sequence && stopped.operationOffset==offset);
        assert(!stopped.run(cancelled,s,2));
        assert(cancelled.h.size()==headers&&cancelled.p.size()==words&&cancelled.gpios==gpios);
    }
    Mock invalid;Host rejected(true);ops[0]=4;
    assert(!rejected.run(invalid,s,2)&&invalid.h.empty()&&invalid.p.empty()&&invalid.gpios==0);ops[0]=1;
    Mock failedGPIO;failedGPIO.gpioOK=false;Host gpio(true);
    assert(!gpio.run(failedGPIO,s,2)&&gpio.error==HostError::GPIO&&gpio.completedOperations==2);
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);assert(f);
        std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),{}};
        const auto profile=parseProfile({bytes.data(),bytes.size()},true);assert(profile.valid());
        Mock saved;Host replay(true);
        for(unsigned id:{10U,1U,2U,6U,7U,4U,5U,11U})assert(replay.run(saved,profile.sequences,id));
        assert(saved.gpios==8&&saved.elapsed==486000&&replay.completedOperations==378);
        assert(!replay.pending&&!replay.faulted);
        printf("saved sequences mock replay: operations=%u GPIO=%u delay=%u us headers=%zu (includes drain NOPs)\n",
            replay.completedOperations,saved.gpios,saved.elapsed,saved.h.size());
    }
    puts("DSI packet, credit/drain timeout, cancellation, preflight, first error: PASS");
}
