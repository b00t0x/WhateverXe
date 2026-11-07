#include "kern_display13_tc_mode.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace Display13TC;
struct IO {
    Port port;std::map<uint32_t,uint32_t> regs;
    Display13Power::State power;bool ready {true},adopt {},quiet {true},nativeFail {},auxOK {true};
    unsigned native {},time {},failedTC {99};bool rollbackStuck {};std::vector<unsigned> sequence;
    IO(unsigned d){assert(Port::resolve(d,port));regs={{0x45404,0},{0x45454,0},{0x45444,0},{0x42000,0x8e380000}};
        for(unsigned t=0;t<4;++t){regs[0x161500+4*t]=4;regs[0x64300+0x100*t]=0;}
        assert(Display13Power::initialize(*this,power,Display13Power::nativeDomains(0x7b)));}
    bool readable(uint32_t r){return regs.count(r);}
    uint32_t read(uint32_t r){assert(readable(r));return regs[r];}
    void write(uint32_t r,uint32_t v) {
        assert(readable(r));sequence.push_back(r);
        if(r==0x45404 || r==0x45454 || r==0x45444){
            const auto mask=r==0x45404?0xa80aU:0x2aaaU;auto ack=(v&mask)>>1;
            // Model ADLP dependencies, not unconditional request -> state.
            // AUX needs ownership; DDI IO needs the cold block already held.
            if(r!=0x45404)for(unsigned t=0;t<4;++t) {
                const auto state=1U<<(6+2*t),request=state<<1;
                const bool phy=(regs[0x64300+0x100*t]&64) && (regs[0x161500+4*t]&4);
                if(!phy || (r==0x45454 && !(regs[0x45444]&state)) || t==failedTC)ack&=~state;
                if(rollbackStuck && t==failedTC && (regs[r]&request) && !(v&request))v|=request;
            }
            regs[r]=(v&~(mask>>1))|ack;
        }
        else regs[r]=v;
    }
    void delayUs(unsigned t){time+=t;}
    bool admissionReady(){return ready;}
    unsigned nativeMode(){return native;}
    unsigned nativeSetMode(unsigned v){sequence.push_back(0x550);if(nativeFail)return 1;native=v;return 0;}
    bool tcAuxSupported(){return true;}
    bool prepareTCAux(unsigned t){return (regs[0x45404]&5)==5 && (regs[0x64300+0x100*t]&64);}
    bool tcAuxHealthy(unsigned){return auxOK;}
    Display13Power::Result acquireDomains(Display13Power::Lease &l,uint32_t d){return Display13Power::acquire(*this,power,l,d,true);}
    Display13Power::Result releaseDomains(Display13Power::Lease &l){return Display13Power::release(*this,power,l);}
    bool powerHeld(uintptr_t owner,uintptr_t object,unsigned d){return owner==1 && object==2 && d==port.ddi && Display13Power::held(*this,power,Mode::accessDomains());}
    bool inheritedVerified(uintptr_t,uintptr_t,unsigned){return adopt;}
    bool quiescent(uintptr_t,uintptr_t,unsigned d){return quiet && !(regs[0x64000+0x100*d]&0x80000000U);}
};
int main() {
    // System power loss keeps leases but erases the PHY and AUX request.
    // Reclaim PHY before AUX without accumulating logical references.
    for(unsigned d=3;d<=6;++d) {
        IO io(d);Mode m;assert(m.connect(io,1,2,d).status==Status::Complete);
        const auto before=io.power;
        io.regs[0x45404]=io.regs[0x45454]=io.regs[0x45444]=0;
        io.regs[io.port.buffer]=0;m.notePowerLoss();io.power.suspended=true;
        assert(Display13Power::restore(io,io.power,true).status==Display13Power::Status::Complete);
        assert(!m.ownership.active && m.restorePending && !(io.regs[0x45444]&0xc0));
        assert(m.connect(io,1,2,d).status==Status::Complete);
        assert(m.ownership.active && !m.restorePending);
        for(unsigned n=0;n<Display13Power::Count;++n)assert(before.refs[n]==io.power.refs[n]);
        assert(io.native==4 && Display13Power::held(io,io.power,Display13Power::auxDomain(d)));
        assert(m.disconnect(io,1,2,d).status==Status::Complete);
    }
    for(unsigned d=3;d<=6;++d) {
        IO io(d);Mode m;assert(m.connect(io,1,2,d).status==Status::Complete);
        assert(m.phase==Mode::Phase::Active && io.native==4 && m.ownership.active && m.auxLease.owner==&io.power);
        assert(io.sequence.back()==0x550 && io.regs[io.port.buffer]&64);
        assert(!(io.power.owned & Display13Power::ddiDomain(d))); // DDI waits for PLL enable.
        auto count=io.sequence.size();assert(m.connect(io,1,2,d).status==Status::Complete && io.sequence.size()==count);
        assert(m.connect(io,1,3,d).status==Status::InUse && io.sequence.size()==count);
        io.quiet=false;assert(m.disconnect(io,1,2,d).status==Status::InUse && m.disconnectPending && m.auxLease.owner);
        assert(m.connect(io,1,2,d).status==Status::Complete && !m.disconnectPending);
        io.quiet=true;io.regs[io.port.buffer]|=1U<<31;
        assert(m.disconnect(io,1,2,d).status==Status::InUse && m.auxLease.owner);
        io.regs[io.port.buffer]&=~(1U<<31);
        assert(m.disconnect(io,1,2,d).status==Status::Complete);
        assert(m.phase==Mode::Phase::Idle && !m.ownership.owner && !m.auxLease.owner && !io.native);
        assert(m.connect(io,1,2,d).status==Status::Complete);assert(m.disconnect(io,1,2,d).status==Status::Complete);
    }
    // DP-alt cannot reuse legacy native mode or unprotected FIA state.
    for(unsigned d=3;d<=6;++d)for(unsigned physical:{3U,12U,15U}) {
        IO io(d);Mode m;auto &p=io.port;
        io.regs[p.ready]=0x1d;io.regs[0xc4000]=0;io.regs[0x44470]=1U<<(16+p.tc);
        io.regs[p.laneAssignment()]=physical<<(8*(p.tc&1));
        io.regs[p.pinAssignment()]=(physical==15?3U:4U)<<(4*(p.tc&1));
        assert(m.connect(io,1,2,d,3).status==Status::Complete && io.native==3);
        assert(m.lanes.mask==physical && m.lanes.count()==(physical==15?4:2));
        const auto refs=io.power;
        io.regs[0x45404]=io.regs[0x45454]=io.regs[0x45444]=0;io.regs[p.buffer]=0;
        m.notePowerLoss();io.power.suspended=true;
        assert(Display13Power::restore(io,io.power,true).status==Display13Power::Status::Complete);
        assert(m.connect(io,1,2,d,3).status==Status::Complete && !m.restorePending);
        assert(m.lanes.mask==physical && Display13Power::held(io,io.power,Display13Power::auxDomain(d)));
        for(unsigned n=0;n<Display13Power::Count;++n)assert(refs.refs[n]==io.power.refs[n]);
        assert(m.connect(io,1,2,d,4).status==Status::Readback);
        auto writes=io.sequence.size();io.regs[0x44470]=0;
        assert(m.connect(io,1,2,d,3).status==Status::Readback && io.sequence.size()==writes);
        assert(m.disconnect(io,1,2,d).status==Status::Complete && !m.lanes.mask);
        io.regs[0x44470]=1U<<(16+p.tc);io.regs[p.laneAssignment()]=0;
        assert(m.connect(io,1,2,d,3).status==Status::NotReady && !io.native);
        assert(m.disconnect(io,1,2,d).status==Status::Complete);
    }
    // Firmware may leave a fixed legacy DP source enabled but unowned (Linux
    // intel_tc_port_init_mode). Transfer only ownership; preserve width/link.
    for(unsigned d=3;d<=6;++d)for(unsigned owned:{0U,64U}) {
        IO io(d);Mode m;const auto firmware=0x80000006U|owned;
        io.regs[io.port.buffer]=firmware;io.adopt=true;
        assert(m.connect(io,1,2,d).status==Status::Complete);
        assert(io.regs[io.port.buffer]==(firmware|64));
        assert(m.disconnect(io,1,2,d).status==Status::InUse && m.auxLease.owner);
        io.regs[io.port.buffer]&=~(1U<<31);
        assert(m.disconnect(io,1,2,d).status==Status::Complete);
        assert(io.regs[io.port.buffer]==6 && !m.ownership.owner && !m.auxLease.owner);
    }
    {IO io(3);Mode m;io.regs[io.port.buffer]=0x80000040;
     assert(m.connect(io,1,2,3).status==Status::NeedsAdoption && m.phase==Mode::Phase::Cleanup && m.accessLease.owner && !m.auxLease.owner);
     assert(!io.native && m.connect(io,1,2,3).status==Status::InUse);}
    {IO io(3);Mode m;io.nativeFail=true;
     assert(m.connect(io,1,2,3).status==Status::Readback && m.ownership.owner && m.auxLease.owner);
     io.nativeFail=false;assert(m.disconnect(io,1,2,3).status==Status::Complete);}
    {IO io(3);Mode m;io.auxOK=false;
     assert(m.connect(io,1,2,3).status==Status::NoPower && m.ownership.owner && !io.native && !io.power.faulted);
     assert(m.disconnect(io,1,2,3).status==Status::Complete && !m.accessLease.owner && !m.auxLease.owner);
     io.auxOK=true;assert(m.connect(io,1,2,3).status==Status::Complete);}
    {IO io(3);Mode m;io.regs[0x45404]=15;io.regs[0x45454]=io.regs[0x45444]=0xc0;
     io.regs[io.port.buffer]=64;io.adopt=true;
     assert(m.connect(io,1,2,3).status==Status::Complete && !io.power.owned);
     assert(m.disconnect(io,1,2,3).status==Status::Complete);
     assert(io.regs[0x45404]==15 && io.regs[0x45454]==0xc0 && io.regs[0x45444]==0xc0);}
    {IO io(3);Mode m;io.ready=false;assert(m.connect(io,1,2,3).status==Status::NotReady && io.sequence.empty());}
    // Cold second instances share PW1/PW2, not the first port's ownership.
    for(unsigned second=4;second<=6;++second) {
        IO io(3);Mode first,other;assert(first.connect(io,1,2,3).status==Status::Complete);
        io.regs[0x64300]|=1U<<31;const auto neighbor=io.regs[0x64300];
        assert(Port::resolve(second,io.port));io.native=0;
        assert(other.connect(io,1,2,second).status==Status::Complete);
        assert(other.disconnect(io,1,2,second).status==Status::Complete && !other.accessLease.owner);
        assert(io.regs[0x64300]==neighbor && (io.regs[0x45444]&0xc0)==0xc0 && !(io.regs[0x45454]&0xc0));
        assert(Display13Power::held(io,io.power,Display13Power::auxDomain(3)));
        io.failedTC=second-3;
        const auto failure=other.connect(io,1,2,second);
        assert(failure.status==Status::NoPower && failure.power.status==Display13Power::Status::StateTimeout);
        assert(failure.power.domain==12+2*(second-3) && failure.power.isolated && !io.power.faulted);
        assert(Display13Power::held(io,io.power,Display13Power::auxDomain(3)));
        assert(other.disconnect(io,1,2,second).status==Status::Complete);
        io.failedTC=99;assert(other.connect(io,1,2,second).status==Status::Complete);
    }
    {IO io(4);Mode m;io.failedTC=1;io.rollbackStuck=true;
     const auto r=m.connect(io,1,2,4);
     assert(r.status==Status::NoPower && r.power.rollbackFailed && io.power.faulted && m.accessLease.owner);
     assert(m.disconnect(io,1,2,4).status!=Status::Complete);}
    puts("PASS Display13 legacy TC mode lifecycle: power/ownership/native order, adoption, retries and pending cleanup");
}
