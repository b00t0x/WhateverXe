#include "kern_display13_fb_irq.hpp"
#include <assert.h>
#include <stdio.h>
#include <map>
#include <vector>
#include <algorithm>
using namespace Display13FBIRQ;
struct IO {
    std::map<uint32_t,uint32_t> r;
    std::vector<uint32_t> reads,writes;
    uint32_t wells {0xFC0F};
    bool rejectEnable {};
    uint32_t rejectRegister {UINT32_MAX};
    IO() {
        r[Master]=r[GfxMaster]=0;r[DDIControl]=0x3300;r[0x64100]=r[0x64000]=0;
        r[TCControl]=r[NorthControl]=0;
        r[0xC4004]=~0U;r[0xC4008]=r[0xC400C]=0;
        for(unsigned p=0;p<3;++p) {
            r[0x70008+p*0x1000]=p ? 0 : 0xC0000000;
            r[PipeIIR(p)]=r[PipeIIR(p)+4]=0;r[PipeIIR(p)-4]=~0U;
            r[0x60400+p*0x1000]=r[0x70080+p*0x1000]=0;
            for(unsigned n=0;n<5;++n)r[0x70180+p*0x1000+n*0x100]=0;
            for(unsigned dsb=0;dsb<3;++dsb) {r[DSBInterrupt(p,dsb)]=0;r[DSBInterrupt(p,dsb)-0x20]=0;}
        }
        for(auto iir:{0x44448U,0x44468U,0x44478U,0x44488U}) {r[iir]=r[iir+4]=0;r[iir-4]=~0U;}
    }
    bool readable(uint32_t reg) {return reg<0x1000000;}
    void check(uint32_t reg) {
        for(unsigned p=0;p<3;++p)if((reg>=PipeIIR(p)-8 && reg<=PipeIIR(p)+4) ||
            (reg>=0x70000+p*0x1000 && reg<0x71000+p*0x1000))assert(powered(wells,p));
    }
    uint32_t read(uint32_t reg) {
        reads.push_back(reg);if(reg==Driver)return wells;check(reg);assert(r.count(reg));return r[reg];
    }
    void write(uint32_t reg,uint32_t value) {
        writes.push_back(reg);check(reg);assert(r.count(reg));
        if(reg==rejectRegister)return;
        if(reg==Master || reg==GfxMaster) {r[reg]=(r[reg]&~Enable)|(rejectEnable ? 0 : value&Enable);return;}
        if(reg==TCControl || reg==NorthControl) {r[reg]=(value&~0x333333U)|((r[reg]&0x333333U)&~value);return;}
        if(reg==DDIControl) {r[reg]=(value&~0x3333U)|((r[reg]&0x3333U)&~value);return;}
        for(unsigned p=0;p<3;++p) {
            if(reg==PipeIIR(p)) {r[reg]&=~value;return;}
            for(unsigned dsb=0;dsb<3;++dsb)if(reg==DSBInterrupt(p,dsb)) {
                r[reg]=(value&~0xFU)|((r[reg]&0xFU)&~value);return;
            }
        }
        if(reg==0xC4008 || reg==HPDBank)r[reg]&=~value;else r[reg]=value;
    }
};
int main() {
    // Cold wake erased both peer IRQ banks and HPD programming. Restore only
    // actually restarted pipes, while re-enabling both selected connectors.
    {IO io;State s;s.transcoders.dsi0Pipe=0;
     io.r[0x70008]=0;io.r[0x7B008]=0xC0000000;io.r[0x6B400]=0x80000006;
     io.r[0x71008]=0xC0000000;
     assert(enable(io,s,0,NativeGT,3,0x18,0x18)==Status::Complete);
     IO cold;cold.r[0x70008]=0;cold.r[0x7B008]=0xC0000000;cold.r[0x6B400]=0x80000006;
     cold.r[DDIControl]=4;cold.r[HPDBank]=1; // unrelated masked TBT event
     assert(restoreAfterPowerLoss(cold,s,7)==Status::Complete && s.active==1 && s.enabled);
     assert((cold.r[NorthControl]&0x88)==0x88 && (cold.r[HPDBank+4]&0x30000)==0x30000);
     assert(!(cold.r[HPDBank-4]&0x30000) && cold.r[HPDBank]==1);
     assert((cold.r[DDIControl]&12)==4 && !(cold.r[PipeIIR(1)+4]&PipeOwned));
     cold.r[0x71008]=0xC0000000;
     assert(extendActivePipes(cold,s,7)==Status::Complete && s.active==3);
    }
    // DSI0 can feed A/B/C. IRQ/frame/plane banks follow the pipe, while
    // control/function come from DSI0. Keep external A active for B/C cases.
    for(unsigned pipe=0;pipe<3;++pipe) {
        IO io;State s;s.transcoders.dsi0Pipe=pipe;
        io.r[0x70008+0x1000*pipe]=0;io.r[0x7B008]=0xC0000000;
        const uint32_t inputs[]={0,5,6};io.r[0x6B400]=0x80000006|(inputs[pipe]<<12);
        const auto expected=(1U<<pipe)|(pipe?1U:0U);
        auto actual=readActivePipes(io,7,s.transcoders);assert(actual.status==Status::Complete && actual.active==expected);
        assert(!consumersStopped(io,1U<<pipe,s.transcoders));
        io.r[DDIControl]|=12; // HPD-A enable + OUTPUT_DATA (panel reset).
        assert(enable(io,s,0,NativeGT,expected,pipe?2U:0U)==Status::Complete);
        assert(s.active==expected && (io.r[DDIControl]&12)==4);
        assert(!(io.r[0xC400C]&(1U<<16)) && (io.r[0xC4004]&(1U<<16)));
        io.r[Master]=0;s.enabled=false;
        const auto before=io.writes.size();
        assert(parkStoppedPipe(io,s,pipe,0,0,0)==Status::Topology && io.writes.size()==before);
        io.r[0x7B008]=0x40000000;
        assert(readActivePipes(io,7,s.transcoders).status==Status::Topology);
        assert(!consumersStopped(io,1U<<pipe,s.transcoders));
        io.r[0x7B008]=0;assert(!consumersStopped(io,1U<<pipe,s.transcoders)); // DSI function still enabled.
        io.r[0x6B400]&=~Enable;assert(consumersStopped(io,1U<<pipe,s.transcoders));
        assert(parkStoppedPipe(io,s,pipe,0,0,0)==Status::Complete && s.active==(pipe?1U:0U));
        io.wells&=~(1U<<(10+2*pipe));
        assert(!consumersStopped(io,1U<<pipe,s.transcoders));
    }
    {IO io;Display13Transcoder::Bindings b{0};io.r[0x70008]=0;
     io.r[0x7B008]=0xC0000000;io.r[0x6B400]=0x80005006;
     assert(readActivePipes(io,7,b).status==Status::Readback); // Stale binding to A, hardware feeds B.
     io.r[0x6B400]=0x80000006;io.r[0x70008]=0xC0000000;
     assert(readActivePipes(io,7,b).status==Status::Readback); // Two timing consumers.
     b.dsi0Pipe=3;assert(readActivePipes(io,7,b).status==Status::Topology);}
    // DP-alt and HDMI notifications use different native output words.
    // A short pulse must not become a fake fixed-DP hotplug on the same DDI.
    for(unsigned tc=0;tc<4;++tc)for(unsigned pulse:{1U,2U,3U}) {
        IO io;State state;const auto alt=8U<<tc,north=1U<<(16+tc);
        assert(enable(io,state,0,0,1,2|alt,alt)==Status::Complete);
        assert((io.r[HPDBank+4]&north) && !(io.r[0xc400c]&(1U<<(24+tc))));
        io.r[Master]=Enable|(1U<<21)|(1U<<23);
        io.r[HPDBank]=north|(1U<<tc); // masked TBT status remains untouched
        io.r[NorthControl]|=pulse<<(4*tc);io.r[DDIControl]|=0x20;io.r[0xc4008]=1U<<17;
        const auto e=collect(io,state);
        const auto expected=((pulse&1)?2U:0U)|((pulse&2)?1U:0U);
        assert(e.status==Status::Complete && e.aux==(expected<<(2*tc)) && e.hotplug==4);
        assert(io.r[HPDBank]==(1U<<tc) && !(io.r[NorthControl]&(3U<<(4*tc))));
    }
    // Actual TC1-only firmware enable masks: north TC3/TBT3 and PCH DDI B.
    {IO io;State state;io.r[HPDBank+4]=0x40004;io.r[HPDBank-4]=0;
     io.r[0xC400C]=0x07020000;io.r[0xC4004]=0x38853f07;io.r[0xC4008]=0x20000;
     assert(enable(io,state,0x3e070000,NativeGT,1,8)==Status::Complete);
     assert(!(io.r[0xC400C]&0x3e070000) && (io.r[0xC4004]&0x3e070000)==0x3e070000);
     assert(io.r[0xC4008]==0x20000 && !(io.r[HPDBank+4]&0x40004));}
    // DDI D and other PCH events must still refuse admission, not be hidden.
    for(uint32_t bit:{1U<<19,1U<<23,1U<<30}) {
        IO io;State state;io.r[0xC400C]=bit;io.r[0xC4004]=0;
        assert(enable(io,state,0x3e070000,NativeGT,1,8)==Status::ForeignIRQ);
        assert(state.failureRegister==0xC400C && state.failureBits==bit);
    }
    for(uint32_t excluded:{1U<<19,1U<<24,1U<<30}) {
        IO io;State state;assert(enable(io,state,excluded,NativeGT,1,8)==Status::Topology);
    }
    {IO io;State state;assert(enable(io,state,1U<<17,NativeGT,1,2)==Status::Topology);}
    // Firmware may leave north TC/TBT enables for excluded connectors.
    // Cover all exclusion subsets and each selected TC1..4 independently.
    for(unsigned selected=0;selected<4;++selected)for(unsigned mask=0;mask<64;++mask) {
        if(mask&(1U<<selected))continue;
        IO io;State state;const auto excluded=mask<<24,north=(mask|(1U<<selected))|((mask|(1U<<selected))<<16);
        const uint32_t keep=0x80000000U;
        io.r[HPDBank+4]=north|keep;io.r[HPDBank-4]=keep;io.r[HPDBank]=north|keep;
        assert(enable(io,state,excluded,NativeGT,1,8U<<selected)==Status::Complete);
        assert(io.r[HPDBank+4]==keep && io.r[HPDBank-4]==(north|keep));
        assert(io.r[HPDBank]==(north|keep));
        assert(std::find(io.writes.begin(),io.writes.end(),HPDBank)==io.writes.end());
    }
    {IO io;State state;io.r[HPDBank+4]=0x40004;io.r[HPDBank-4]=0;
     assert(enable(io,state,0x3e000000,NativeGT,1,8)==Status::Complete);}
    {IO io;State state;io.r[HPDBank+4]=0x40004;io.r[HPDBank-4]=0;
     assert(enable(io,state,0,NativeGT,1,8)==Status::ForeignIRQ && io.writes==std::vector<uint32_t>{Master});}
    for(uint32_t bits:{1U<<15,1U<<22,1U<<30}) {
        IO io;State state;io.r[HPDBank+4]=0x40004|bits;io.r[HPDBank-4]=0;
        assert(enable(io,state,0x3e000000,NativeGT,1,8)==Status::ForeignIRQ && io.writes==std::vector<uint32_t>{Master});
        assert(state.failureRegister==HPDBank+4 && state.failureBits==bits);
    }
    for(auto reg:{HPDBank-4,HPDBank+4}) {
        IO io;State state;io.r[HPDBank+4]=0x40004;io.r[HPDBank-4]=0;io.rejectRegister=reg;
        assert(enable(io,state,0x3e000000,NativeGT,1,8)==Status::Readback && !(io.r[Master]&Enable));
    }
    // HDMI TC1/2 + cold DP TC3: GOP left north TC3/TBT3 enabled.
    {IO io;State state;io.r[HPDBank+4]=0x40004;io.r[HPDBank-4]=0;
     io.r[HPDBank]=0x40004;
     assert(enable(io,state,0x38070000,NativeGT,1,0x38)==Status::Complete);
     assert(!(io.r[HPDBank+4]&0x40004) && (io.r[HPDBank-4]&0x40004)==0x40004);
     assert(io.r[HPDBank]==0x40004 && (io.r[0xC400C]&0x07000000)==0x07000000);}
    // ADL-P/N TC legacy HPD is PCH bit24+tc and a separate control bank.
    // Foreign masked statuses must survive both enable and collect.
    for(unsigned tc=0;tc<4;++tc)for(unsigned pulse=0;pulse<4;++pulse) {
        IO io;State state;const auto ports=1U|(8U<<tc),source=1U<<(24+tc);
        const auto excluded=0x3F000000U&~source;
        io.r[TCControl]=1U<<20; // TC6 short pulse, excluded and untouched.
        assert(enable(io,state,excluded,NativeGT,1,ports)==Status::Complete);
        assert(pchOwned(state)==(0x10000|source) && (io.r[TCControl]&(8U<<(4*tc))));
        io.r[TCControl]|=pulse<<(4*tc);io.r[0xC4008]=source|(1U<<29);
        io.r[Master]|=1U<<23;const auto e=collect(io,state);
        const auto expected=((pulse&2)?1U<<(6+2*tc):0)|((pulse&1)?1U<<(7+2*tc):0);
        assert(e.status==Status::Complete && e.hotplug==expected && state.lastTC&(1U<<20));
        assert(!(io.r[TCControl]&(3U<<(4*tc))) && (io.r[TCControl]&(1U<<20)) && io.r[0xC4008]==(1U<<29));
    }
    {IO io;State s;assert(enable(io,s,PCHTypeC,NativeGT,1,9)==Status::Topology);}
    {IO io;State s;assert(enable(io,s,0x3E000000,NativeGT,1,9)==Status::Complete);
     io.r[TCControl]|=0x10;io.r[0xC4008]=1U<<25;io.r[Master]|=1U<<23;
     const auto e=collect(io,s);assert(e.status==Status::Complete && !e.hotplug && (io.r[TCControl]&0x10) && (io.r[0xC4008]&(1U<<25)));}
    const auto tc1=Display13Power::ddiDomain(3),aux1=Display13Power::auxDomain(3);
    assert(affectedPipes(tc1,2,4,0,1,0,0,0)==1 && affectedPipes(aux1,2,4,0,1,0,0,0)==1);
    assert(affectedPipes(tc1)==7 && affectedPipes(tc1,0,0,0,8,0,0,0)==7);
    {IO io;State s;io.r[0x64300]=0;assert(enable(io,s,0x3E000000,NativeGT,1,9)==Status::Complete);
     assert(disable(io,s)==Status::Complete);io.r[0x70008]=0;
     assert(quiescentPipes(io,s,tc1,0,1));io.r[0x64300]=Enable;
     assert(!quiescentPipes(io,s,tc1,0,1));io.r[0x64300]=0;
     PowerReleaseScope scope;const auto n=io.writes.size();
     assert(beginPowerRelease(io,s,scope,tc1,1,0,0,0,0,64)==Status::Unavailable && io.writes.size()==n);
     assert(beginPowerRelease(io,s,scope,tc1,1,0,0,0,0,0)==Status::Complete);
     assert(endPowerRelease(io,s,scope)==Status::Complete);}
    // Three-bank dispatch and Combo C HPD; unrelated DDI D status stays pending.
    {IO io;State state;io.r[0x71008]=io.r[0x72008]=Enable;
     assert(enable(io,state,0,NativeGT,7,7)==Status::Complete);
     assert(pchOwned(state)==0x70000 && ddiMask(state,8)==0x888);
     io.r[DDIControl]|=0x1200;io.r[0xC4008]=1U<<18;
     io.r[Master]|=(7U<<16)|(1U<<23);
     for(unsigned p=0;p<3;++p)io.r[PipeIIR(p)]=8;
     const auto e=collect(io,state);assert(e.status==Status::Complete && e.hotplug==16 && (e.pipe&56)==56);
     assert(!io.r[0xC4008] && !(io.r[DDIControl]&0x200) && (io.r[DDIControl]&0x1000));
     assert(affectedPipes(512,1,2,4)==4 && affectedPipes(512,1,2,7)==7);
    }
    // A secondary activation may not reset the live peer or consume either
    // pipe's completion. Only a validated path's actual powered bank is added.
    {IO io;State state;assert(enable(io,state,0,NativeGT)==Status::Complete);
     io.r[0x71008]=Enable;io.r[PipeIIR(0)]=8;io.r[PipeIIR(1)]=8;
     io.r[DSBInterrupt(1,0)]|=1;io.r[0xC4008]=1U<<17;
     io.r[GfxMaster]|=NativeGT;io.r[Master]|=(1U<<16)|(1U<<17);
     const auto before=io.r;io.writes.clear();
     assert(readActivePipes(io,1).status==Status::Topology && io.writes.empty());
     assert(extendActivePipes(io,state,1)==Status::Topology && io.writes.empty());
     assert(extendActivePipes(io,state,3)==Status::Complete && state.active==3);
     assert(io.r[PipeIIR(0)]==8 && io.r[PipeIIR(1)]==8 && (io.r[DSBInterrupt(1,0)]&1));
     for(auto reg:io.writes)assert(reg==PipeIIR(1)-4 || reg==PipeIIR(1)+4 ||
         reg==DSBInterrupt(1,0) || reg==DSBInterrupt(1,1) || reg==DSBInterrupt(1,2));
     for(auto pair:before)if(std::find(io.writes.begin(),io.writes.end(),pair.first)==io.writes.end())assert(io.r[pair.first]==pair.second);
     io.writes.clear();assert(extendActivePipes(io,state,3)==Status::Complete && io.writes.empty());
     io.r[0x71008]=0;assert(extendActivePipes(io,state,3)==Status::Complete && state.active==3 && io.writes.empty());
    }
    // Wake DSI A while a stopped USB-C peer C retains its armed bank.
    // No peer notification, pending completion, master or GT may be reset.
    {IO io;State state;state.transcoders.dsi0Pipe=0;io.r[0x70008]=0;
     io.r[0x7B008]=0;io.r[0x6B400]=6;io.r[0x72008]=0xC0000000;
     assert(enable(io,state,0,NativeGT,4,2)==Status::Complete);
     io.r[0x72008]=0;io.r[0x7B008]=0xC0000000;io.r[0x6B400]=0x80000006;
     io.r[PipeIIR(2)]=8;io.r[DSBInterrupt(2,0)]|=1;
     io.r[Master]|=1U<<18;io.r[GfxMaster]|=NativeGT;
     const auto before=io.r;io.writes.clear();
     assert(readActivePipes(io,7,state.transcoders).active==1);
     assert(extendActivePipes(io,state,7)==Status::Complete && state.active==5 && !state.faulted);
     for(auto reg:io.writes)assert(reg==PipeIIR(0)-4 || reg==PipeIIR(0)+4 ||
         reg==DSBInterrupt(0,0) || reg==DSBInterrupt(0,1) || reg==DSBInterrupt(0,2));
     for(auto pair:before)if(std::find(io.writes.begin(),io.writes.end(),pair.first)==io.writes.end())assert(io.r[pair.first]==pair.second);
     io.r[0x71008]=0xC0000000;io.r[0x72008]=0xC0000000;
     assert(extendActivePipes(io,state,7)==Status::Complete && state.active==7);
    }
    {IO io;io.wells=0x403;io.reads.clear();assert(readActivePipes(io,3).active==1);
     assert(std::find(io.reads.begin(),io.reads.end(),0x71008)==io.reads.end());
     io.reads.clear();assert(readActivePipes(io,8).status==Status::Topology && io.reads.empty());
     io.r[0x70008]=UINT32_MAX;assert(readActivePipes(io,1).status==Status::Readback);
    }
    for(unsigned kind=0;kind<3;++kind) {
     IO io;State state;assert(enable(io,state,0,NativeGT)==Status::Complete);io.r[0x71008]=Enable;io.writes.clear();
     if(kind==0) {io.r[PipeIIR(1)+4]=1U<<4;io.r[PipeIIR(1)-4]=0;
      assert(extendActivePipes(io,state,3)==Status::ForeignIRQ && io.writes.empty());}
     if(kind==1) {io.r[DSBInterrupt(1,0)]=UINT32_MAX;
      assert(extendActivePipes(io,state,3)==Status::Readback && io.writes.empty());}
     if(kind==2) {io.rejectRegister=PipeIIR(1)+4;
      assert(extendActivePipes(io,state,3)==Status::Readback && state.faulted && !(io.r[Master]&Enable));}
    }
    {IO io;State state;io.r[0x71008]=0xC0000000;assert(enable(io,state,0,NativeGT,3)==Status::Complete);
     io.r[0x71008]=0;io.r[Master]|=1U<<16;io.r[GfxMaster]=Enable|4;io.r[PipeIIR(0)]=8;
     const auto peerIER=io.r[PipeIIR(0)+4],peerIMR=io.r[PipeIIR(0)-4];io.writes.clear();
     PowerReleaseScope scope;
     assert(beginPowerRelease(io,state,scope,32,2,0,0,pipeEvents(0),0,3)==Status::Complete);
     assert(scope.held && scope.parked==2 && !state.enabled && state.active==1);
     assert(io.r[GfxMaster]==(Enable|4) && io.r[PipeIIR(0)]==8);
     assert(io.r[PipeIIR(0)+4]==peerIER && io.r[PipeIIR(0)-4]==peerIMR);
     assert(endPowerRelease(io,state,scope)==Status::Complete && state.enabled && !scope.held);
     assert(io.r[Master]==(Enable|(1U<<16)) && io.r[GfxMaster]==(Enable|4));
     for(auto reg:io.writes)assert(reg==Master || reg==PipeIIR(1)-4 || reg==PipeIIR(1)+4 ||
         reg==DSBInterrupt(1,0) || reg==DSBInterrupt(1,1) || reg==DSBInterrupt(1,2));
     for(unsigned kind=0;kind<7;++kind) {
      auto blocked=io;auto s=state;blocked.writes.clear();PowerReleaseScope g;
      unsigned hw=0,sw=0;uint32_t queued=0,aux=0,hpd=0;
      if(kind==0)hw=1;if(kind==1)sw=1;if(kind==2)queued=pipeEvents(1);if(kind==3)aux=1;if(kind==4)hpd=12;
      if(kind==5)blocked.r[DSBInterrupt(1,0)]|=1;
      if(kind==6)blocked.r[0x71008]=0xC0000000;
      assert(beginPowerRelease(blocked,s,g,32,2,hw,sw,queued,aux,hpd)!=Status::Complete && !g.held && blocked.writes.empty());
     }
     auto failed=io;auto fs=state;PowerReleaseScope fg;failed.rejectRegister=Master;
     const auto failure=beginPowerRelease(failed,fs,fg,32,2,0,0,0,0,0);
     assert(failure==Status::Readback && fs.faulted);
     assert(endPowerRelease(failed,fs,fg)==Status::Faulted);
     auto unreadable=io;auto us=state;PowerReleaseScope ug;
     assert(beginPowerRelease(unreadable,us,ug,32,2,0,0,0,0,0)==Status::Complete);
     unreadable.r[Master]=UINT32_MAX;
     assert(endPowerRelease(unreadable,us,ug)==Status::Readback && us.faulted);
    }
    {IO io;State state;assert(enable(io,state)==Status::Complete);io.r[0x70008]=0;PowerReleaseScope g;
     assert(beginPowerRelease(io,state,g,1,7,0,0,0,0,0)==Status::Complete);
     const auto writes=io.writes.size();io.wells=0;
     assert(endPowerRelease(io,state,g)==Status::Complete && !state.enabled && io.writes.size()==writes);
    }
    {HandlerScopes scopes;HandlerScope live,masked,nested,other;
     assert(scopes.begin(live,1,true) && scopes.total==1 && scopes.display==1);
     assert(scopes.begin(masked,2,false));
     // Restoring the global master/enable state does not change the scope.
     assert(scopes.permits(1) && !scopes.permits(2) && !scopes.permits(99));
     assert(scopes.begin(nested,1,false) && !scopes.permits(1));
     assert(scopes.begin(other,3,true) && scopes.total==4 && scopes.display==2);
     assert(!scopes.begin(live,4,true)); // Do not corrupt an in-flight node.
     assert(scopes.end(masked) && scopes.total==3 && scopes.display==2);
     assert(scopes.end(nested) && scopes.permits(1));
     assert(scopes.end(live) && !scopes.permits(1) && scopes.permits(3));
     assert(scopes.end(other) && !scopes.head && !scopes.total && !scopes.display);
     assert(!scopes.end(other) && !scopes.begin(other,0,true));
    }
    // Explicit DDI B -> pipe B must leave a live pipe A and its pending
    // notification untouched. The legacy default incorrectly selects A.
    {IO mapped;State state;state.active=1;mapped.r[Master]=1U<<16;
     assert(!quiescent(mapped,state,32,0));mapped.reads.clear();
     assert(quiescentPipes(mapped,state,32,0,2) && mapped.writes.empty());
     assert(std::find(mapped.reads.begin(),mapped.reads.end(),0x70008)==mapped.reads.end());
     assert(mapped.r[0x70008]==0xC0000000 && mapped.r[Master]==(1U<<16));
     auto pending=mapped;pending.r[Master]|=1U<<17;assert(!quiescentPipes(pending,state,32,0,2));
     auto live=mapped;live.r[0x71008]=0xC0000000;assert(!quiescentPipes(live,state,32,0,2));
     mapped.reads.clear();assert(!quiescentPipes(mapped,state,1,0,2) && mapped.reads.empty());
     assert(!quiescentPipes(mapped,state,32,0,0) && mapped.reads.empty());
     assert(!quiescentPipes(mapped,state,32,0,8) && mapped.reads.empty());
     assert(!quiescentPipes(mapped,state,32,1,2));
     state.enabled=true;assert(!quiescentPipes(mapped,state,32,0,2));
    }
    // Explicitly qualified A+B pipes get separate IRQ banks/DSB completions.
    // The unchanged live call-site default must still reject this topology.
    {IO dual;State ds;dual.r[0x71008]=0xC0000000;
     assert(enable(dual,ds,0,NativeGT,3)==Status::Complete && ds.active==3);
     for(unsigned p=0;p<3;++p) {
      assert((dual.r[PipeIIR(p)+4]&PipeOwned)==(p<2 ? PipeOwned&~4U : 0U));
      for(unsigned d=0;d<3;++d)assert((dual.r[DSBInterrupt(p,d)]&0xF0000)==(p<2 ? 0xF0000U : 0U));
     }
     dual.r[Master]|=(1U<<16)|(1U<<17);dual.r[PipeIIR(0)]=1|8;dual.r[PipeIIR(1)]=1|8;
     auto e=collect(dual,ds);assert(e.status==Status::Complete && e.pipe==(1|2|8|16));
     assert(ds.vblanks[0]==1 && ds.vblanks[1]==1 && ds.flips[0]==1 && ds.flips[1]==1);
     dual.r[PipeIIR(1)]=8;assert(enable(dual,ds,0,NativeGT,3)==Status::Complete && dual.r[PipeIIR(1)]==8);
     // Drain consumers and mask the master before changing active banks.
     assert(disable(dual,ds)==Status::Complete);dual.r[PipeIIR(1)]=0;dual.r[0x71008]=0;
     assert(enable(dual,ds,0,NativeGT,1)==Status::Complete && ds.active==1 && !(dual.r[PipeIIR(1)+4]&PipeOwned));
     IO implicit;State is;implicit.r[0x71008]=0xC0000000;assert(enable(implicit,is)==Status::Topology);
     IO incomplete;State cs;assert(enable(incomplete,cs,0,NativeGT,3)==Status::Topology);
     IO noPower;State ps;noPower.wells=0x403;noPower.r[0x71008]=0xC0000000;
     assert(enable(noPower,ps,0,NativeGT,3)==Status::Power);
     IO changed;State xs;assert(enable(changed,xs)==Status::Complete);
     assert(enable(changed,xs,0,NativeGT,3)==Status::Topology);
     assert(affectedPipes(32,1,2)==2 && affectedPipes(128,1,2)==1);
     assert(affectedPipes(32|128,2,1)==3 && affectedPipes(4,2,1)==1);
     assert(affectedPipes(1,2,1)==7 && affectedPipes(128,8,1)==7);
    }
    // Park just one of two stopped/active native banks. The caller masks
    // master and proves real HW/SW handler + native E08 drainage first.
    for(unsigned target=0;target<2;++target) {
     IO io;State state;io.r[0x71008]=0xC0000000;assert(enable(io,state,0,NativeGT,3)==Status::Complete);
     for(unsigned p=0;p<3;++p)assert(pipeEvents(p)==nativePipe(UINT32_MAX,p));
     assert(!pipeEvents(3));
     auto live=io;assert(parkStoppedPipe(live,state,target,0,0,0)==Status::Unavailable);
     assert(disable(io,state)==Status::Complete);io.r[0x70008+target*0x1000]=0;
     auto busy=io;busy.writes.clear();assert(parkStoppedPipe(busy,state,target,1,0,0)==Status::Unavailable && busy.writes.empty());
     auto sw=io;sw.writes.clear();assert(parkStoppedPipe(sw,state,target,0,1,0)==Status::Unavailable && sw.writes.empty());
     auto queued=io;queued.writes.clear();assert(parkStoppedPipe(queued,state,target,0,0,pipeEvents(target))==Status::Unavailable && queued.writes.empty());
     auto active=io;active.r[0x70180+target*0x1000]=Enable;active.writes.clear();
     assert(parkStoppedPipe(active,state,target,0,0,0)==Status::Topology && active.writes.empty());
     const auto before=io.r;io.writes.clear();io.reads.clear();
     io.r[PipeIIR(target)]=8;io.r[DSBInterrupt(target,0)]|=1;
     assert(parkStoppedPipe(io,state,target,0,0,pipeEvents(1-target))==Status::Complete && state.active==(1U<<(1-target)));
     for(auto reg:io.writes)assert(reg==PipeIIR(target)-4 || reg==PipeIIR(target)+4 || reg==PipeIIR(target) ||
       reg==DSBInterrupt(target,0) || reg==DSBInterrupt(target,1) || reg==DSBInterrupt(target,2));
     for(auto pair:before)if(std::find(io.writes.begin(),io.writes.end(),pair.first)==io.writes.end())assert(io.r[pair.first]==pair.second || pair.first==DSBInterrupt(target,0));
     assert(io.r[DSBInterrupt(target,0)]&1); // Preserve native DSB completion; no fake busy/queue clearing.
     io.write(Master,Enable);state.enabled=true;io.r[Master]|=1U<<(17-target);io.r[PipeIIR(1-target)]=8;
     assert(collect(io,state).pipe==(1U<<(4-target)));
    }
    {IO io;State state;assert(enable(io,state)==Status::Complete);assert(disable(io,state)==Status::Complete);
     io.r[0x70008]=0;io.rejectRegister=PipeIIR(0)+4;
     assert(parkStoppedPipe(io,state,0,0,0,0)==Status::Readback && state.faulted && !(io.r[Master]&Enable));}
    // Dual idle HPD retains every real pipe/DSB/PCH completion, including
    // after the boot FB identity is gone; no current FB index participates.
    {IO io;State state;io.r[0x71008]=Enable;assert(enable(io,state,0,NativeGT,3)==Status::Complete);
     assert(disable(io,state)==Status::Complete);io.r[0x70008]=io.r[0x71008]=0;
     io.r[PipeIIR(0)]=8|(1U<<13);io.r[PipeIIR(1)]=8|(1U<<13);
     io.r[DSBInterrupt(0,0)]|=1;io.r[DSBInterrupt(1,0)]|=1;
     io.r[0xC4008]=PCHOwned;io.r[DDIControl]|=0x33;io.r[GfxMaster]|=NativeGT;
     const auto before=io.r;io.writes.clear();
     assert(hotplugOnly(io,state,true)==Status::Complete && state.enabled && !state.active);
     for(auto reg:io.writes)assert(reg==Master || reg==PipeIIR(0)-4 || reg==PipeIIR(0)+4 ||
         reg==PipeIIR(1)-4 || reg==PipeIIR(1)+4 || reg==PipeIIR(2)-4 || reg==PipeIIR(2)+4);
     for(auto pair:before)if(std::find(io.writes.begin(),io.writes.end(),pair.first)==io.writes.end())assert(io.r[pair.first]==pair.second);
     io.writes.clear();assert(hotplugOnly(io,state,true)==Status::Complete && io.writes.empty());
     auto foreign=io;auto fs=state;assert(disable(foreign,fs)==Status::Complete);
     foreign.r[PipeIIR(2)+4]|=1U<<4;foreign.r[PipeIIR(2)-4]&=~(1U<<4);foreign.writes.clear();
     assert(hotplugOnly(foreign,fs,true)==Status::ForeignIRQ && foreign.writes.empty());
     auto active=io;auto as=state;active.r[0x71008]=Enable;active.writes.clear();
     assert(hotplugOnly(active,as,true)==Status::Topology && active.writes.empty());
     auto failed=io;auto xs=state;assert(disable(failed,xs)==Status::Complete);failed.rejectEnable=true;
     assert(hotplugOnly(failed,xs,true)==Status::Readback && xs.faulted && !(failed.r[Master]&Enable));
    }
    // Full display -> stopped consumers -> HPD-only -> native pipe enable.
    IO hpd;State hs;assert(enable(hpd,hs)==Status::Complete);assert(disable(hpd,hs)==Status::Complete);
    hpd.r[0x70008]=0;hpd.r[0xC4008]=1U<<17;hpd.r[DDIControl]|=0x20;
    assert(hotplugOnly(hpd,hs)==Status::Complete && hs.enabled && !hs.active);
    assert(hpd.r[0xC4008]==(1U<<17) && (hpd.r[DDIControl]&0x20)); // Pending reconnect survives.
    for(unsigned p=0;p<3;++p)assert(!(hpd.r[PipeIIR(p)+4]&PipeOwned) && (hpd.r[PipeIIR(p)-4]&PipeOwned)==PipeOwned);
    hpd.wells=3;hpd.r[Master]=Enable|(1U<<23);auto he=collect(hpd,hs);
    assert(he.status==Status::Complete && he.hotplug==4 && !he.pipe && !hs.faulted);
    hpd.wells=0xFC0F;hpd.r[0x70008]=0xC0000000;assert(enable(hpd,hs)==Status::Complete && hs.active==1);
    IO running;State rs;assert(enable(running,rs)==Status::Complete);assert(disable(running,rs)==Status::Complete);
    auto beforeWrites=running.writes.size();assert(hotplugOnly(running,rs)==Status::Topology && running.writes.size()==beforeWrites);
    IO hpdLost;State hls;assert(enable(hpdLost,hls)==Status::Complete);assert(disable(hpdLost,hls)==Status::Complete);
    hpdLost.wells=0;assert(hotplugOnly(hpdLost,hls)==Status::Power);
    // External native accelerator owns GT callbacks, while DE only changes
    // its own masks. A pending GT bit survives; GuC bit30 is not admitted.
    IO sharedGT;State sharedState;sharedGT.r[GfxMaster]=Enable|NativeGT;
    assert(enable(sharedGT,sharedState,0,NativeGT)==Status::Complete);
    assert((sharedGT.r[GfxMaster]&NativeGT)==NativeGT);
    IO foreignGT;State foreignState;foreignGT.r[GfxMaster]=Enable|NativeGT;
    assert(enable(foreignGT,foreignState)==Status::ForeignIRQ);
    IO guc;State gucState;guc.r[GfxMaster]=Enable|(1U<<30);
    assert(enable(guc,gucState,0,NativeGT)==Status::ForeignIRQ);
    IO badGT;State badGTState;
    assert(enable(badGT,badGTState,0,1U<<30)==Status::Topology);
    IO io;State s;assert(enable(io,s)==Status::Complete && s.enabled && s.active==1);
    assert(io.r[DDIControl]==0x3388); // Unhandled port C/D status survives enable.
    io.r[Master]|=1U<<16;io.r[PipeIIR(0)]=1|8|(1U<<21)|(1U<<22);
    auto e=collect(io,s);assert(e.status==Status::Complete && e.pipe==(1|8|64));
    assert(s.vblanks[0]==1 && s.flips[0]==1 && s.underruns[0]==1 && !io.r[PipeIIR(0)]);
    assert(vblank(io,s,0,false)==Status::Complete);
    io.r[PipeIIR(0)]=1|8;
    assert(enable(io,s)==Status::Complete && io.r[PipeIIR(0)]==(1|8) && !(io.r[PipeIIR(0)+4]&1));
    io.r[PipeIIR(0)]=1|8;assert(collect(io,s).pipe==8); // Masked vblank is not delivered.
    assert(vblank(io,s,0,true)==Status::Complete);
    io.r[PipeIIR(0)]=(1U<<13);io.r[DSBInterrupt(0,0)]|=1|8;
    e=collect(io,s);assert(e.pipe==((1U<<14)|64) && s.dsbErrors[0]==1);
    assert((io.r[DSBInterrupt(0,0)]&0xF)==1); // Native handler retains actual completion status.
    io.r[Master]=Enable|(1U<<23);io.r[0xC4008]=1U<<17;io.r[DDIControl]|=0x20;
    e=collect(io,s);assert(e.hotplug==4 && io.r[DDIControl]==0x3388 && !io.r[0xC4008]);
    assert(nativeHotplug(1U<<17,0x10)==8 && nativeHotplug(1U<<16,2)==1);
    for(unsigned p=0;p<3;++p) {
        assert(nativePipe(1,p)==(1U<<p));assert(nativePipe(8,p)==(1U<<(3+p)));
        assert(nativePipe(1U<<21,p)==(1U<<(6+p)) && nativePipe(1U<<22,p)==(1U<<(6+p)));
        for(unsigned d=0;d<3;++d)assert(nativePipe(1U<<(13+d),p)==(1U<<(14+3*d+p)));
    }
    IO onlyA;onlyA.wells=0x403;State a;assert(enable(onlyA,a)==Status::Complete && a.configured==1);
    onlyA.r[Master]=Enable|(1U<<17);assert(collect(onlyA,a).status==Status::Power && a.faulted);
    assert(!(onlyA.r[Master]&Enable)); // Does not read an unpowered pipe B bank.
    IO lost;State l;assert(enable(lost,l)==Status::Complete);lost.wells=3;
    lost.r[Master]=Enable|(1U<<16);assert(collect(lost,l).status==Status::Power);
    IO foreign;State f;foreign.r[0x4444C]=1;foreign.r[0x44444]=0;
    assert(enable(foreign,f)==Status::ForeignIRQ && foreign.r[0x4444C]==1);
    assert(f.firstFailure==Status::ForeignIRQ && f.failureRegister==0x4444C && f.failureBits==1);
    assert(enable(foreign,f)==Status::Faulted && f.firstFailure==Status::ForeignIRQ);
    IO pchForeign;State pf;pchForeign.r[0xC400C]=1U<<24;pchForeign.r[0xC4004]&=~(1U<<24);
    assert(enable(pchForeign,pf)==Status::ForeignIRQ && pf.failureRegister==0xC400C && pf.failureBits==(1U<<24));
    IO excluded;State ex;excluded.r[0xC400C]=1U<<24;excluded.r[0xC4004]&=~(1U<<24);
    excluded.r[0xC4008]=1U<<24;
    assert(enable(excluded,ex,PCHTypeC)==Status::Complete && ex.excludedPCH==PCHTypeC);
    assert(!(excluded.r[0xC400C]&PCHTypeC) && (excluded.r[0xC4004]&PCHTypeC)==PCHTypeC);
    assert(excluded.r[0xC4008]==(1U<<24) && ex.priorPCHIER==(1U<<24));
    assert(enable(excluded,ex,PCHTypeC)==Status::Complete);
    IO unrelated;State un;unrelated.r[0xC400C]=(1U<<24)|(1U<<23);unrelated.r[0xC4004]=0;
    assert(enable(unrelated,un,PCHTypeC)==Status::ForeignIRQ && un.failureBits==(1U<<23));
    assert(unrelated.r[0xC400C]==((1U<<24)|(1U<<23)) && !unrelated.r[0xC4004]);
    IO noMask;State nm;noMask.r[0xC4004]=0;noMask.rejectRegister=0xC4004;
    assert(enable(noMask,nm,PCHTypeC)==Status::Readback && !(noMask.r[Master]&Enable));
    IO masked;State m;assert(enable(masked,m)==Status::Complete);
    masked.r[Master]=Enable|(1U<<16);masked.r[PipeIIR(0)]=8|(1U<<26);
    assert(collect(masked,m).pipe==8 && masked.r[PipeIIR(0)]==(1U<<26));
    IO changed;State c;assert(enable(changed,c)==Status::Complete);
    changed.r[Master]=Enable|(1U<<16);changed.r[PipeIIR(0)]=1U<<26;
    changed.r[PipeIIR(0)+4]|=1U<<26;changed.r[PipeIIR(0)-4]&=~(1U<<26);
    assert(collect(changed,c).status==Status::ForeignIRQ && c.faulted);
    IO wrong;State w;wrong.r[0x71008]=Enable;assert(enable(wrong,w)==Status::Topology);
    IO failed;State failState;failed.rejectEnable=true;assert(enable(failed,failState)==Status::Readback);
    assert(disable(io,s)==Status::Complete && !s.enabled && !(io.r[Master]&Enable));
    for(auto reg:{PipeIIR(0)+4,DDIControl,0xC400CU,DSBInterrupt(0,0)}) {
        IO bad;State badState;bad.rejectRegister=reg;
        assert(enable(bad,badState)==Status::Readback && badState.faulted && !(bad.r[Master]&Enable));
    }
    assert(!quiescent(io,s,4,0)); // Active pipe A prevents last owned A request release.
    io.r[0x70008]=0;
    assert(quiescent(io,s,4,0) && !quiescent(io,s,4,1)); // Native IRQ completion still in flight.
    io.r[0x70180]=Enable;assert(!quiescent(io,s,4,0));io.r[0x70180]=0;
    io.r[0x70080]=0x27;assert(!quiescent(io,s,4,0));io.r[0x70080]=0;
    io.r[0x70480]=Enable;assert(!quiescent(io,s,4,0));io.r[0x70480]=0;
    io.r[0x60400]=Enable;assert(!quiescent(io,s,4,0));io.r[0x60400]=0;
    io.r[DSBInterrupt(0,0)-0x20]=1;assert(!quiescent(io,s,4,0));
    io.r[DSBInterrupt(0,0)-0x20]=Enable;assert(!quiescent(io,s,4,0));
    io.r[DSBInterrupt(0,0)-0x20]=0;
    io.r[0x64100]=Enable;assert(!quiescent(io,s,32,0));io.r[0x64100]=0;
    assert(quiescent(io,s,32,0));
    IO temporary;State temp;assert(enable(temporary,temp)==Status::Complete);
    assert(quiescent(temporary,temp,2|8,1)); // Temporary PG3's inactive PW2/B release keeps A/IRQ on.
    temporary.r[0x71008]=Enable;assert(!quiescent(temporary,temp,2|8,0));
    temporary.r[0x71008]=0;temporary.r[PipeIIR(1)+4]=1;temporary.r[PipeIIR(1)-4]&=~1U;
    assert(!quiescent(temporary,temp,2|8,0));
    IO off;State offState;off.wells=3;assert(quiescent(off,offState,31,0)); // No unpowered bank access.

    // DDIA is the appended physical domain bit7; B bit5 stays unchanged.
    {IO a;State state;a.r[0x64000]=Enable;
     assert(!quiescent(a,state,128,0));a.r[0x64000]=0;
     assert(!quiescent(a,state,128,0));a.r[0x70008]=0;
     assert(quiescent(a,state,128,0));state.enabled=true;
     assert(!quiescent(a,state,128,0));}
    {IO b;State state;b.r[0x70008]=0;assert(quiescent(b,state,32,0));
     for(auto reg:b.reads)assert(reg!=0x64000);}
    puts("AlderLakeN Display13 IRQ fixtures passed: powered banks, native events, W1C isolation and fault masking");
}
