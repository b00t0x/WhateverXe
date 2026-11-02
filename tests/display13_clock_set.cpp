#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include "kern_display13_clock.hpp"
using namespace Display13Clock;
struct IO {
    std::map<uint32_t,uint32_t> regs {{DSSM,0x20000000},{PLL,0xC0000044},{Control,0x380518},{Squash,0},
        {Mailbox,0},{Data,0},{Data1,0}};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    bool prepareDenied {}, mailboxError {}, mailboxTimeout {}, pllTimeout {}, badControl {}, concurrentChange {}, unlockTimeout {};
    unsigned delays {};
    unsigned completionPolls {}, pollsLeft {};
    bool readable(uint32_t reg) const {return regs.count(reg);}
    uint32_t read(uint32_t reg) {
        if(reg==Mailbox && pollsLeft && !--pollsLeft)regs[Mailbox]=mailboxError ? 1:0;
        return regs.at(reg);
    }
    void delayUs(unsigned us) {delays+=us;}
    void write(uint32_t reg,uint32_t v) {
        assert(reg==Mailbox || reg==Data || reg==Data1 || reg==PLL || reg==Control);
        writes.emplace_back(reg,v);regs[reg]=v;
        if(reg==Mailbox) {
            assert(v==0x80000007 && regs[Data1]==0);
            if(!mailboxTimeout) {
                pollsLeft=completionPolls;
                regs[Mailbox]=completionPolls ? 0x80000000:mailboxError ? 1:0;
                if(regs[Data]==3)regs[Data]=prepareDenied ? 0:1;
                if(concurrentChange)regs[PLL]^=1;
            }
        }
        if(reg==PLL && (v&(1U<<31))) {
            regs[PLL]|=1U<<30;
            if((v&(1U<<23)) && !pllTimeout)regs[PLL]|=1U<<22;
        }
        if(reg==PLL && !(v&(1U<<31)) && !unlockTimeout)regs[PLL]&=~(1U<<30);
        if(reg==Control && badControl)regs[Control]^=1;
    }
    unsigned writesTo(uint32_t reg) const {unsigned count=0;for(auto w:writes)count+=w.first==reg;return count;}
};
int main() {
    const Conditions ready {true,true,1};
    for(unsigned id:{0xa721U,0xa7a1U,0xa7a9U,0xa7acU,0xa7adU})
        assert(variantForPCI((id<<16)|0x8086)==Variant::RPLU);
    assert(variantForPCI(0xa7a88086)==Variant::ADLP);
    for(unsigned ref:{19200U,24000U,38400U}) {
        assert(select(ref,624000000,false,Variant::RPLU)==960000000);
        assert(select(ref,624000000,false,Variant::ADLP)>960000000);
    }
    {
        IO io;
        assert(set(io,960000000,ready,Variant::ADLP).status==ChangeStatus::InvalidFrequency);
        assert(io.writes.empty());
        auto r=set(io,960000000,ready,Variant::RPLU);
        assert(r.status==ChangeStatus::Complete && r.after.cdclkKHz==480000 &&
               r.after.ratio==50 && io.regs[Data]==1);
    }
    assert(select(38400,0,true)==1305600000 && select(24000,0,true)==1296000000);
    assert(select(38400,345600000,false)==358400000 && select(38400,358400000,false)==384000000);
    assert(!select(38400,1305600000,false) && !select(0,0,true));
    // DSI firmware clock (38.4 MHz reference, /3) to the external-output
    // maximum (/2): prepare only in the stopped bootstrap interval.
    for (unsigned active : {0U,1U}) {
        IO io;io.regs[DSSM]=0x40000000;io.regs[PLL]=0xC000000E;io.regs[Control]=0x780164;
        auto r=set(io,1305600000,{true,true,active});
        assert(r.before.nativeHz()==358400000);
        if(active)assert(r.status==ChangeStatus::ActiveDividerChange && io.writes.empty());
        else assert(r.status==ChangeStatus::Complete && r.after.nativeHz()==1305600000 && r.after.divider==2);
    }
    {IO io;auto r=stop(io,{false,false,0});assert(r.status==ChangeStatus::OwnershipRequired && io.writes.empty());}
    {IO io;auto r=stop(io,ready);assert(r.status==ChangeStatus::ActiveDividerChange && io.writes.empty());}
    {IO io;auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::Complete && r.after.status==Status::Disabled &&
        r.after.nativeMaxHz()==1305600000 && !(io.regs[PLL]&0xC0000000) && io.regs[Control]==0x380011);
     auto restarted=set(io,614400000,{true,true,0});assert(restarted.status==ChangeStatus::Complete && restarted.after.nativeHz()==614400000);}
    {IO io;io.unlockTimeout=true;auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::PllTimeout &&
        r.delayUs==1000 && io.writesTo(Control)==0 && io.writesTo(Data)==1);}
    {IO io;io.prepareDenied=true;auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::PrepareTimeout && io.writesTo(PLL)==0);}
    {IO io;io.regs[PLL]=68;auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::Complete && io.writes.empty());}
    {IO io;io.regs.erase(Data1);auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::Unavailable && io.writes.empty());}
    {IO io;io.badControl=true;auto r=stop(io,{true,false,0});assert(r.status==ChangeStatus::ReadbackMismatch && io.writesTo(Data)==1);}
    {IO io;auto r=set(io,1305600000,{false,false,1});assert(r.status==ChangeStatus::Complete && !r.writes && io.writes.empty());}
    {IO io;auto r=set(io,614400000,{false,true,1});assert(r.status==ChangeStatus::OwnershipRequired && io.writes.empty());}
    {IO io;auto r=set(io,614400000,{true,false,1});assert(r.status==ChangeStatus::WatermarksRequired && io.writes.empty());}
    {IO io;auto r=set(io,614400000,{true,false,3});assert(r.status==ChangeStatus::WatermarksRequired && io.writes.empty());}
    {IO io;auto r=set(io,614400000,{true,true,3});assert(r.status==ChangeStatus::Complete && r.after.nativeHz()==614400000);
     for(auto w:io.writes)if(w.first==PLL)assert(w.second&(1U<<31));}
    {IO io;auto r=set(io,345600000,{true,true,6});assert(r.status==ChangeStatus::ActiveDividerChange && io.writes.empty());}
    {IO io;auto r=set(io,345600000,ready);assert(r.status==ChangeStatus::ActiveDividerChange && io.writes.empty());}
    {IO io;auto r=set(io,614400001,ready);assert(r.status==ChangeStatus::InvalidFrequency && io.writes.empty());}
    // Active pipe, same divider: PLL crawl never clears PLL enable.
    {IO io;auto r=set(io,614400000,ready);
     assert(r.status==ChangeStatus::Complete && r.after.cdclkKHz==307200 && r.after.nativeHz()==614400000);
     assert(io.writes.size()==10 && io.writesTo(PLL)==3 && io.writesTo(Control)==1 && io.writesTo(Mailbox)==2);
     assert(io.writes[0]==std::make_pair(Data,3U) && io.writes[2]==std::make_pair(Mailbox,0x80000007U));
     assert(io.writes[3]==std::make_pair(PLL,0x80000020U) && io.writes[4]==std::make_pair(PLL,0x80800020U));
     assert(io.writes[5]==std::make_pair(PLL,0x80000020U) && io.writes[6]==std::make_pair(Control,0x380264U));
     assert(io.writes[7]==std::make_pair(Data,0U));
     for(auto w:io.writes)if(w.first==PLL)assert(w.second&(1U<<31));}
    // Divider change is only possible with all pipes off; no forced vblank
    // synchronisation is pretended for an active pipe with different divider.
    {IO io;auto r=set(io,345600000,{true,true,0});assert(r.status==ChangeStatus::Complete && r.after.divider==3);}
    {IO io;io.regs[PLL]=0;auto r=set(io,614400000,{true,true,1});assert(r.status==ChangeStatus::ActiveDividerChange && io.writes.empty());}
    {IO io;io.regs[PLL]=0;auto r=set(io,614400000,{true,true,0});assert(r.status==ChangeStatus::Complete && io.writesTo(PLL)==2);}
    // Firmware rejection must precede PLL/control writes. Timeouts are finite.
    {IO io;io.regs[Mailbox]=0x80000000;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::MailboxBusy && io.writes.empty());}
    {IO io;io.mailboxError=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::MailboxError && io.writesTo(PLL)==0);}
    {IO io;io.mailboxTimeout=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::MailboxTimeout && r.delayUs==500 && io.writesTo(PLL)==0);}
    {IO io;io.prepareDenied=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::PrepareTimeout && r.delayUs==53000 && io.writesTo(PLL)==0);}
    {IO io;io.prepareDenied=true;io.completionPolls=26;auto r=set(io,614400000,ready);
     assert((r.status==ChangeStatus::MailboxTimeout || r.status==ChangeStatus::PrepareTimeout) && r.delayUs==53000 && io.writesTo(PLL)==0);}
    {IO io;io.concurrentChange=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::ReadbackMismatch && io.writesTo(PLL)==0 && io.writesTo(Control)==0);}
    // No rollback or low voltage is issued after uncertain hardware state.
    {IO io;io.pllTimeout=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::PllTimeout && r.delayUs==1000 && io.writesTo(Control)==0 && io.writesTo(Data)==1 && (io.regs[PLL]&(1U<<23)));}
    {IO io;io.badControl=true;auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::ReadbackMismatch && io.writesTo(Data)==1);}
    {IO io;io.regs.erase(Data1);auto r=set(io,614400000,ready);assert(r.status==ChangeStatus::Unavailable && io.writes.empty());}
    {IO io;io.regs[PLL]=0;io.regs[Squash]=0x80000000;auto r=set(io,614400000,{true,true,0});assert(r.status==ChangeStatus::NotReady && io.writes.empty());}
    puts("PASS Display13 CDCLK transitions: native Hz, PCODE prepare/error, active crawl, quiescent divider/cold enable, bounded faults. Synthetic IO only.");
}
