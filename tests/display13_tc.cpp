#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "kern_display13_tc.hpp"
#include "kern_display13_power_bridge.hpp"
using namespace Display13Power;
struct PowerIO {
    std::map<uint32_t,uint32_t> regs {{0x45404,0},{0x45454,0},{0x45444,0},{0x42000,0x8E380000}};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool supported {true},healthy {true},prepared {true};unsigned time {},prepares {},healthReads {},preparedTC {};
    bool readable(uint32_t r){return regs.count(r);}
    uint32_t read(uint32_t r){assert(readable(r));return regs[r];}
    void write(uint32_t r,uint32_t v){
        assert(r==0x45404 || r==0x45454 || r==0x45444);writes.emplace_back(r,v);
        // Literal hardware request/state pairs, independently of Domain numbering.
        const auto mask=r==0x45404?0xA80AU:0x2AAAU;
        regs[r]=(v&~(mask>>1))|((v&mask)>>1);
    }
    void delayUs(unsigned n){time+=n;}
    bool tcAuxSupported(){return supported;}
    bool prepareTCAux(unsigned tc){
        assert((regs[0x45404]&5)==5); // PW1/PW2 must already be physically on.
        ++prepares;preparedTC=tc;return prepared;
    }
    bool tcAuxHealthy(unsigned tc){assert(tc==preparedTC);++healthReads;return healthy;}
};
struct PHYIO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool power {true},inherited {},idle {true},rejectWrite {};unsigned time {};
    bool readable(uint32_t r){return regs.count(r);}
    uint32_t read(uint32_t r){assert(readable(r));return regs[r];}
    void write(uint32_t r,uint32_t v){writes.emplace_back(r,v);if(!rejectWrite)regs[r]=v;}
    void delayUs(unsigned n){time+=n;}
    bool powerHeld(uintptr_t owner,uintptr_t object,unsigned){return power && owner==0x100 && object==0x200;}
    bool inheritedVerified(uintptr_t,uintptr_t,unsigned){return inherited;}
    bool quiescent(uintptr_t,uintptr_t,unsigned){return idle;}
};
struct DekelIO {
    std::map<uint32_t,uint32_t> regs;
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool power {true},held {};unsigned locks {},unlocks {};uint32_t invalid {},rejected {};
    bool readable(uint32_t r){return regs.count(r);}
    bool powerHeld(){return power;}
    uint32_t read(uint32_t r){assert(held && readable(r));return r==invalid?UINT32_MAX:regs[r];}
    void write(uint32_t r,uint32_t v){assert(held);writes.emplace_back(r,v);if(r!=rejected)regs[r]=v;}
    unsigned lock(){assert(!held);held=true;++locks;return 123;}
    void unlock(unsigned irq){assert(held && irq==123);held=false;++unlocks;}
};
int main(){
    for(unsigned tc=0;tc<4;++tc){
        Display13TC::Port p;assert(Display13TC::Port::resolve(tc+3,p));
        assert(p.ready==0x161500+4*tc && p.buffer==0x64300+0x100*tc &&
            p.pll==0x46038+8*tc && p.clock==0x4610C+4*tc && p.phy==0x168000+0x1000*tc);
        const auto prev=0x76543210U,internal=0x236CU;
        const auto selected=p.bank(prev,internal);
        assert(((selected>>(tc*8))&15)==2 && ((selected^prev)&~(15U<<(tc*8)))==0);
        assert(p.phyRegister(internal)==0x16836C+0x1000*tc);
        assert(p.legacyHPD(1U<<(24+tc)) && !p.legacyHPD(UINT32_MAX));
        PowerIO io;NativeBridge b;const auto port=tc+3;
        assert(b.begin(io,0,1U|(1U<<port)).status==Status::Complete);
        assert(b.getDDI(io,port).status==Status::Complete && b.nativeDDIRefs(port)==1);
        assert((io.regs[0x45454]&(3U<<(2*(tc+3))))==(3U<<(2*(tc+3))));
        assert(b.state.refs[static_cast<unsigned>(Domain::PW2)]==1);
        assert(b.getAUX(io,port).status==Status::Complete && io.prepares==1 && io.preparedTC==tc);
        assert((io.regs[0x45444]&(3U<<(2*(tc+3))))==(3U<<(2*(tc+3))));
        assert(b.state.refs[1]==2 && b.state.refs[3]==0 && b.state.refs[4]==0); // DDI + AUX, no phantom pipe B/C.
        assert(b.getAUX(io,port).status==Status::Complete && b.nativeAUXRefs(port)==2 && io.prepares==1);
        assert(b.putAUX(io,port).status==Status::Complete && b.state.refs[1]==2);
        assert(suspend(io,b.state).status==Status::Complete);
        assert(restore(io,b.state).status==Status::Complete && io.prepares==2);
        assert(suspend(io,b.state).status==Status::Complete);
        const auto priorPW2=b.state.refs[1];
        assert(restore(io,b.state,true).status==Status::Complete && io.prepares==2);
        assert(b.state.deferredTC==(auxDomain(port)|ddiDomain(port)));
        assert(b.getAUX(io,port).status==Status::Complete && io.prepares==3 && b.auxHeld(io,port));
        assert(b.putAUX(io,port).status==Status::Complete && b.state.refs[1]==priorPW2);
        assert(b.getDDI(io,port).status==Status::Complete && b.ddiHeld(io,port) && !b.state.deferredTC);
        assert(b.putDDI(io,port).status==Status::Complete && b.state.refs[1]==priorPW2);
        assert(b.putAUX(io,port).status==Status::Complete && b.state.refs[1]==1 && (io.regs[Driver]&0xC)==0xC);
        assert(b.putDDI(io,port).status==Status::Complete && !b.nativeDDIRefs(port));
        assert((io.regs[0x45404]&0xA80A)==0x802 && (io.regs[0x45454]&0x2AAA)==2);
        assert(!b.nativeDDIRefs(7) && !b.nativeAUXRefs(8));
        // A TC boot GOP on pipe A must retain PW2 without any AUX client.
        // Dropping another native PG2 user cannot turn its lanes off.
        PowerIO bootIO;NativeBridge boot;
        assert(boot.begin(bootIO,port,1U<<port).status==Status::Complete && !boot.nativeAUXRefs(port));
        assert(boot.adoptBootPath(1,0,port).status==Status::Complete);
        assert(boot.getPG(bootIO,2).status==Status::Complete && boot.putPG(bootIO,2).status==Status::Complete);
        assert(boot.state.refs[1]==1 && (bootIO.regs[Driver]&0xC)==0xC && boot.ddiHeld(bootIO,port));
        assert(boot.putDDI(bootIO,port).status==Status::Complete && !boot.state.refs[1] && !(bootIO.regs[Driver]&0xC));
        PHYIO phy;phy.regs={{p.ready,4},{p.buffer,0x12300080}};Display13TC::Ownership own;
        auto r=own.acquire(phy,0x100,0x200,port);assert(r.status==Display13TC::Status::Complete && r.writes==1);
        assert(phy.regs[p.buffer]==0x123000C0 && own.active);
        phy.regs[p.buffer]&=~64U;const auto nw=phy.writes.size();
        assert(own.acquire(phy,0x100,0x200,port).status==Display13TC::Status::Readback && phy.writes.size()==nw);
        phy.regs[p.buffer]|=64;
        assert(own.acquire(phy,0x100,0x201,port).status==Display13TC::Status::InUse);
        phy.idle=false;assert(own.release(phy,0x100,0x200,port).status==Display13TC::Status::InUse);
        phy.idle=true;phy.regs[p.buffer]|=1U<<31;
        assert(own.release(phy,0x100,0x200,port).status==Display13TC::Status::InUse && own.active);
        phy.regs[p.buffer]&=~(1U<<31);phy.power=false;
        assert(own.release(phy,0x100,0x200,port).status==Display13TC::Status::NoPower && own.active);
        phy.power=true;assert(own.release(phy,0x100,0x200,port).status==Display13TC::Status::Complete);
        assert(phy.regs[p.buffer]==0x12300080 && !own.owner && !own.active);
        // Native PLL placeholder resolves to ADL-P/N's 8-byte port stride.
        for(unsigned index: {0x38U,0x39U,0x3CU,0x48U,0x49U}) {
            const uint32_t raw=index==0x38?0x46030:index==0x39?0x2200:index==0x3C?0x168210:index==0x48?0xA0:0x10A0;
            uint32_t off=0,in=0;bool direct=false;
            assert(Display13TC::Dekel::resolve(p,index,raw,off,in,direct));
            const auto expected=index==0x38?0x46038+8*tc:index==0x39?0x168200+0x1000*tc:index==0x3C?0x168210+0x1000*tc:0x1680A0+0x1000*tc;
            assert(off==expected && direct==(index==0x38));
            DekelIO d;d.regs={{p.hip,0x76543210},{off,0x7E284274}};uint32_t value=0;
            auto dr=Display13TC::Dekel::access(d,p,index,raw,false,value);
            assert(dr.status==Display13TC::Status::Complete && value==0x7E284274 && d.regs[p.hip]==0x76543210 && d.locks==1 && d.unlocks==1);
            value=0x84275;dr=Display13TC::Dekel::access(d,p,index,raw,true,value);
            const auto mask=Display13TC::Dekel::writeMask(index);
            assert(dr.status==Display13TC::Status::Complete && d.regs[off]==((0x7E284274&~mask)|(0x84275&mask)) && d.regs[p.hip]==0x76543210);
            assert(!Display13TC::Dekel::resolve(p,index,raw^1,off,in,direct));
        }
    }
    for(unsigned d: {0U,1U,2U,7U,8U,UINT32_MAX}){Display13TC::Port p;assert(!Display13TC::Port::resolve(d,p));}
    // Actual N95 TC1 inherited output requires an explicit handoff proof.
    {PHYIO io;io.regs={{0x161500,0xC},{0x64300,0x80000040}};Display13TC::Ownership s;
     assert(s.acquire(io,0x100,0x200,3).status==Display13TC::Status::NeedsAdoption && io.writes.empty());
     assert(s.acquire(io,0x100,0x200,3,true).status==Display13TC::Status::NeedsAdoption);
     io.inherited=true;assert(s.acquire(io,0x100,0x200,3,true).status==Display13TC::Status::Complete && io.writes.empty());
     assert(s.release(io,0x100,0x200,3).status==Display13TC::Status::InUse);
     io.regs[0x64300]=64;assert(s.release(io,0x100,0x200,3).status==Display13TC::Status::Complete);}
    // No AUX backend: refuse before changing power or software ownership.
    {PowerIO io;io.supported=false;NativeBridge b;assert(b.begin(io,0,9).status==Status::Complete);
     const auto n=io.writes.size();assert(b.getAUX(io,3).status==Status::UnqualifiedTCAUX);
     assert(io.writes.size()==n && !b.state.faulted && !b.auxRefs[3] && !b.state.refs[1]);}
    // Health failure rolls back only the new TC scope, preserving the A lease.
    {PowerIO io;io.healthy=false;NativeBridge b;assert(b.begin(io,0,9).status==Status::Complete);
     assert(b.getAUX(io,3).status==Status::AuxHealthTimeout && io.time==1000 && b.state.faulted);
     assert(!b.auxRefs[3] && !b.auxLease[3].owner && b.pgLease[0].owner && b.ddiLease[0].owner);
     assert((io.regs[0x45404]&0xA80A)==0x802 && !(io.regs[0x45444]&0x2AAA));}
    for(unsigned ready: {0U,1U,2U,5U,UINT32_MAX}){
        PHYIO io;io.regs={{0x161500,ready},{0x64300,0}};Display13TC::Ownership s;
        const auto r=s.acquire(io,0x100,0x200,3);
        assert(r.status==(ready==0?Display13TC::Status::NotReady:ready==UINT32_MAX?Display13TC::Status::Unavailable:Display13TC::Status::UnsupportedMode));
        if(ready==0) {
            assert(s.owner && io.writes.size()==1 && io.time==500000);
            assert(s.release(io,0x100,0x200,3).status==Display13TC::Status::Complete);
        } else assert(!s.owner && io.writes.empty());
    }
    {PHYIO io;io.regs={{0x161500,4},{0x64300,0}};io.rejectWrite=true;Display13TC::Ownership s;
     assert(s.acquire(io,0x100,0x200,3).status==Display13TC::Status::Readback && s.owner && !s.active);
     assert(s.release(io,0x100,0x200,3).status==Display13TC::Status::Complete && !s.owner);}
    {Display13TC::Port p;assert(Display13TC::Port::resolve(3,p));DekelIO io;io.regs={{p.hip,1},{0x168200,0}};
     uint32_t v=0;io.rejected=p.hip;
     assert(Display13TC::Dekel::access(io,p,0x39,0x2200,false,v).status==Display13TC::Status::Readback);
     assert(io.locks==io.unlocks && !io.held && io.regs[p.hip]==1);
     io.rejected=0;io.invalid=0x168200;
     assert(Display13TC::Dekel::access(io,p,0x39,0x2200,false,v).status==Display13TC::Status::Unavailable && io.regs[p.hip]==1);
     io.power=false;const auto n=io.locks;
     assert(Display13TC::Dekel::access(io,p,0x39,0x2200,false,v).status==Display13TC::Status::NoPower && io.locks==n);}
    puts("PASS ADL-N TC1..4: literal coordinates/PLL stride, PW2/AUX lease order, nested refs, suspend/restore, health rollback, ownership/adoption/drain. CPU only.");
}
