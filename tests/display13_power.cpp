#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
#include <type_traits>
#include "kern_display13_power.hpp"
using namespace Display13Power;
static_assert(!std::is_copy_constructible<Lease>::value, "Power ownership cannot be copied");
static_assert(!std::is_copy_assignable<Lease>::value, "Power ownership cannot be reassigned");

// Hardware model uses literal XE_LPD register masks, independent of the
// implementation's bit helpers. Firmware and driver requests are separate.
struct IO {
    std::map<uint32_t, uint32_t> regs {{0x45404,0}, {0x45454,0}, {0x45444,0}, {0x45440,0}, {0x4544C,0}, {0x42000,0x8E380000},
        {0x45400,0}, {0x45408,0}, {0x4540C,0}, {0x45450,0}, {0x4545C,0}};
    std::vector<std::pair<uint32_t,uint32_t>> writes;
    uint32_t deny {}, allones {}, failedEnable {}, failedEnableReg {}, stickyRelease {};
    unsigned time {}, delayEnable {}, delayFuse {}, fuseUnavailableAt {UINT32_MAX};
    uint32_t delayedFuse {};
    bool loseDDIAfterWrite {};
    bool readable(uint32_t reg) { return reg != deny && regs.count(reg); }
    uint32_t read(uint32_t reg) {
        assert(regs.count(reg));
        if (reg == allones || (loseDDIAfterWrite && reg==0x45454 && writes.size()>=3)) return UINT32_MAX;
        update();
        if(reg==0x42000) {
            if(time>=fuseUnavailableAt)return UINT32_MAX;
            return regs[reg] & ~(time<delayFuse ? delayedFuse : 0U);
        }
        return regs[reg];
    }
    void update() {
        const auto pw = regs[0x45404] | regs[0x45400] | regs[0x45408] | regs[0x4540C];
        const auto ddi = regs[0x45454] | regs[0x45450] | regs[0x4545C];
        uint32_t states = (pw & 0xA80A) >> 1;
        if (time < delayEnable) states = 0;
        states &= ~((!failedEnableReg || failedEnableReg==0x45404)?failedEnable:0U);
        regs[0x45404] = (regs[0x45404] & ~0x5405U) | states;
        regs[0x45454] = (regs[0x45454] & ~0x5U) | (((ddi & 0xA) >> 1) & ~((!failedEnableReg || failedEnableReg==0x45454)?failedEnable:0U));
        const auto aux = regs[0x45444] | regs[0x45440] | regs[0x4544C];
        regs[0x45444] = (regs[0x45444] & ~5U) | (((aux & 0xA) >> 1) & ~((!failedEnableReg || failedEnableReg==0x45444)?failedEnable:0U));
    }
    void write(uint32_t reg, uint32_t value) {
        assert(reg == 0x45404 || reg == 0x45454 || reg == 0x45444); // Never BIOS/KVMR/debug.
        writes.emplace_back(reg,value);
        regs[reg] = value | (regs[reg] & stickyRelease);
        update();
    }
    void delayUs(unsigned us) { assert(us == 1); ++time; }
};
static bool ok(Result r) { return r.status == Status::Complete; }
static void empty(const State &s) {
    for (auto refs : s.refs) assert(!refs);
    assert(!s.owned);
}
int main() {
    // Cold Pipe A/HDMI-B: PW1 then PW_A then DDI_B; no PW2/B/C/AUX writes.
    { IO io; State s; Lease l; assert(initialize(io,s) && io.writes.empty());
      assert(ok(acquire(io,s,l,BootHDMIB)));
      assert(io.writes.size()==3 && io.writes[0].first==0x45404 && io.writes[0].second==2);
      assert(io.writes[1].first==0x45404 && (io.writes[1].second&0xAAAA)==0x802);
      assert(io.writes[2].first==0x45454 && (io.writes[2].second&0xAAAA)==8);
      assert(s.refs[0]==1 && !s.refs[1] && s.refs[2]==1 && !s.refs[3] && !s.refs[4] && s.refs[5]==1);
      assert(ok(release(io,s,l)) && !l.owner); empty(s);
      assert(io.writes.size()==6 && io.writes[3].first==0x45454);
      assert(!(io.regs[0x45404]&0xA80A) && !(io.regs[0x45454]&8)); }
    // Actual S1 FC0F readout: preserve every predecessor driver request,
    // including unused PW_B/C, rather than treating them as new ownership.
    { IO io; io.regs[0x45404]=0xFC0F; io.regs[0x45454]=0xC;
      State s; Lease l; assert(initialize(io,s));
      assert(s.initialDriver==0xFC0F && s.borrowed==0x3F);
      assert(ok(acquire(io,s,l,BootHDMIB)) && ok(release(io,s,l)));
      assert(io.writes.empty() && io.regs[0x45404]==0xFC0F && io.regs[0x45454]==0xC); empty(s); }
    // Firmware requests remain unchanged; releasing our request is success
    // while hardware state stays on. No fake power-off acknowledgement.
    { IO io; io.regs[0x45400]=0x802; io.regs[0x45450]=8;
      io.regs[0x45408]=0x2000; io.regs[0x4540C]=0x8000; io.regs[0x4545C]=2;
      const auto bios=io.regs[0x45400], kvm=io.regs[0x45408], debug=io.regs[0x4540C];
      State s; Lease l; assert(initialize(io,s)); assert(ok(acquire(io,s,l,BootHDMIB)));
      auto r=release(io,s,l); assert(ok(r) && r.forcedOn==0x25); empty(s);
      assert(io.regs[0x45400]==bios && io.regs[0x45408]==kvm && io.regs[0x4540C]==debug);
      assert(io.regs[0x45450]==8 && io.regs[0x4545C]==2); }
    // Independent B/C wells share PW2 and PW1, not one another. Live A stays
    // leased while the extra C lease is released.
    { IO io; State s; Lease a,b,c; assert(initialize(io,s));
      assert(ok(acquire(io,s,a,BootHDMIB)) && ok(acquire(io,s,b,bit(Domain::PipeB))) &&
             ok(acquire(io,s,c,bit(Domain::PipeC))));
      assert((io.regs[0x45404]&0xA80A)==0xA80A && s.refs[0]==3 && s.refs[1]==2);
      assert(ok(release(io,s,c)) && (io.regs[0x45404]&0xA80A)==0x280A);
      assert(ok(release(io,s,b)) && (io.regs[0x45404]&0xA80A)==0x802);
      assert(ok(release(io,s,a))); empty(s); }
    // Nesting and lease identity: no double put, reuse, or release through
    // another state. These cases must not issue MMIO or drop dependencies.
    { IO io; State s,other; Lease a,b; assert(initialize(io,s) && initialize(io,other));
      assert(ok(acquire(io,s,a,BootHDMIB)) && ok(acquire(io,s,b,BootHDMIB)));
      const auto n=io.writes.size(); assert(acquire(io,s,a,BootHDMIB).status==Status::InvalidDomains);
      assert(release(io,other,a).status==Status::Unbalanced && io.writes.size()==n);
      assert(ok(release(io,s,a)) && io.writes.size()==n && s.refs[0]==1);
      assert(release(io,s,a).status==Status::Unbalanced && io.writes.size()==n);
      assert(ok(release(io,s,b))); empty(s); }
    // Preflight failures never modify refs/requests.
    { IO io; State s; Lease l; assert(initialize(io,s)); io.deny=0x45454;
      assert(acquire(io,s,l,BootHDMIB).status==Status::Unavailable && io.writes.empty()); empty(s); }
    { IO io; State s; Lease l; assert(initialize(io,s)); io.allones=0x42000;
      assert(acquire(io,s,l,BootHDMIB).status==Status::Unavailable && io.writes.empty()); empty(s); }
    { IO io; State s; Lease l; assert(initialize(io,s)); s.refs[2]=UINT32_MAX;
      assert(acquire(io,s,l,BootHDMIB).status==Status::Overflow && io.writes.empty() && !l.owner); }
    { IO io; State s; Lease l; assert(initialize(io,s));
      assert(acquire(io,s,l,0x80).status==Status::InvalidDomains && io.writes.empty()); }
    // PW1 prerequisite fuse timeout occurs before any write; no fake lease.
    { IO io; State s; Lease l; assert(initialize(io,s)); io.regs[0x42000]&=~0x08000000U;
      assert(acquire(io,s,l,BootHDMIB).status==Status::FuseTimeout && io.time==1000 && io.writes.empty());
      assert(s.faulted && !l.owner); empty(s); }
    // Linux's fuse wait uses a millisecond budget, not its comment's 1/5 us.
    // Exercise PG0, PW1, PW2 and every supported pipe after ACK, in acquire
    // and restore. Literal masks keep the delayed-hardware model independent.
    const uint32_t masks[]={0x08000000,0x04000000,0x02000000,0x00200000,0x00100000,0x00080000};
    const Domain domains[]={Domain::PW1,Domain::PW1,Domain::PW2,Domain::PipeA,Domain::PipeB,Domain::PipeC};
    unsigned fuseCases=0;
    for(unsigned i=0;i<6;++i)for(unsigned delay:{0U,1U,2U,5U,6U,999U,1000U,1001U,UINT32_MAX})
    for(bool restoring:{false,true}) {
      IO io;State s;Lease l;assert(initialize(io,s));
      if(restoring)assert(ok(acquire(io,s,l,bit(domains[i]))) && ok(suspend(io,s)));
      io.delayedFuse=masks[i];io.delayFuse=delay;
      const auto refs=s.refs[0];
      auto r=restoring?restore(io,s):acquire(io,s,l,bit(domains[i]));
      if(delay<=1000) {
        assert(ok(r) && io.time==delay && l.owner==&s && !s.faulted && !s.suspended);
        assert(s.refs[0]==(restoring?refs:1));
        assert(ok(release(io,s,l)));empty(s);
      } else {
        assert(r.status==Status::FuseTimeout && io.time==1000 && s.faulted);
        if(restoring)assert(s.suspended && l.owner==&s && s.refs[0]==refs);
        else {assert(!l.owner && !r.rollbackFailed);empty(s);}
      }
      ++fuseCases;
    }
    // MMIO disappearing during the longer poll never counts as fuse ready.
    for(unsigned i=0;i<6;++i)for(bool restoring:{false,true}) {
      IO io;State s;Lease l;assert(initialize(io,s));
      if(restoring)assert(ok(acquire(io,s,l,bit(domains[i]))) && ok(suspend(io,s)));
      io.delayedFuse=masks[i];io.delayFuse=1000;io.fuseUnavailableAt=37;
      auto r=restoring?restore(io,s):acquire(io,s,l,bit(domains[i]));
      assert(r.status==Status::Unavailable && io.time==37 && s.faulted);
      if(restoring)assert(l.owner==&s && s.suspended && s.refs[0]==1);
      else {assert(!l.owner && !r.rollbackFailed);empty(s);}
      ++fuseCases;
    }
    printf("PASS %u delayed fuse acquire/restore cases\n",fuseCases);
    // Bounded state wait plus rollback; never attempt DDI after failed PW_A.
    { IO io; io.failedEnable=0x400; State s; Lease l; assert(initialize(io,s));
      auto r=acquire(io,s,l,BootHDMIB); assert(r.status==Status::StateTimeout && r.domain==2 &&
          io.time==1000 && !r.rollbackFailed && !l.owner && s.faulted); empty(s);
      assert(io.writes.size()==4); for (auto w:io.writes) assert(w.first==0x45404); }
    // A failed rollback retains its PW1 prerequisite and exact ownership;
    // later operations fail closed rather than reporting zero references.
    { IO io; io.failedEnable=0x400; io.stickyRelease=0x800; State s; Lease l; assert(initialize(io,s));
      auto r=acquire(io,s,l,BootHDMIB); assert(r.status==Status::StateTimeout && r.rollbackFailed==5 &&
          s.owned==5 && s.faulted && (io.regs[0x45404]&0x802)==0x802);
      assert(acquire(io,s,l,BootHDMIB).status==Status::NotReady); }
    // Native pipe fuse is checked after request/state, before DDI acquisition.
    { IO io; State s; Lease l; assert(initialize(io,s)); io.regs[0x42000]&=~0x00200000U;
      auto r=acquire(io,s,l,BootHDMIB); assert(r.status==Status::FuseTimeout && r.domain==2 && io.time==1000);
      assert(!l.owner && !r.rollbackFailed); empty(s); }
    // A stuck final release must retain refs/lease/prerequisites for recovery.
    { IO io; State s; Lease l; assert(initialize(io,s)); assert(ok(acquire(io,s,l,BootHDMIB)));
      io.stickyRelease=8; auto r=release(io,s,l); assert(r.status==Status::ReleaseFailed && r.domain==5);
      assert(s.faulted && s.refs[0]==1 && s.refs[2]==1 && s.refs[5]==1 && l.domains==0x25 && s.owned==0x25); }
    // MMIO disappearing after the DDI request leaves all dependencies held;
    // neither rollback nor a later operation may pretend the request cleared.
    { IO io; io.loseDDIAfterWrite=true; State s; Lease l; assert(initialize(io,s));
      auto r=acquire(io,s,l,BootHDMIB); assert(r.status==Status::Unavailable && r.domain==5 &&
          r.rollbackFailed==0x25 && s.owned==0x25 && s.faulted && io.writes.size()==3 && !l.owner); }
    // Delayed enable is observed, rather than declaring completion on write.
    { IO io; io.delayEnable=7; State s; Lease l; assert(initialize(io,s));
      assert(ok(acquire(io,s,l,BootHDMIB)) && io.time==7 && ok(release(io,s,l))); empty(s); }
    // Losing an existing request must not be silently reacquired as a new
    // owner, and must not drop the still-valid original lease.
    { IO io; State s; Lease a,b; assert(initialize(io,s)); assert(ok(acquire(io,s,a,BootHDMIB)));
      io.regs[0x45404]&=~0x800U; const auto n=io.writes.size();
      auto r=acquire(io,s,b,BootHDMIB); assert(r.status==Status::RequestLost && r.domain==2 &&
          io.writes.size()==n && s.faulted && a.owner==&s && !b.owner && s.refs[0]==1); }
    // Suspend preserves the native logical leases while removing only owned
    // physical requests. Restore never increments get/put counts.
    {IO io;State s;Lease a;assert(initialize(io,s) && ok(acquire(io,s,a,BootHDMIB)));
     assert(ok(suspend(io,s)) && s.suspended && !s.owned && s.refs[0]==1 && a.domains==0x25);
     assert(release(io,s,a).status==Status::NotReady);
     assert(ok(restore(io,s)) && !s.suspended && s.owned==0x25 && s.refs[0]==1);
     assert(ok(release(io,s,a)));empty(s);}
    {IO io;io.regs[Driver]=0xFC0F;io.regs[DDI]=0xC;State s;Lease a;
     assert(initialize(io,s) && ok(acquire(io,s,a,BootHDMIB)) && ok(suspend(io,s)) && io.writes.empty());
     assert(ok(restore(io,s)) && io.writes.empty() && !s.owned);
     assert(ok(suspend(io,s)));io.regs[Driver]=io.regs[DDI]=0;
     assert(ok(restore(io,s)) && s.owned==0x25 && s.refs[0]==1 && io.writes.size()==3);
     assert(ok(release(io,s,a)));empty(s);}
    {IO io;State s;Lease a;assert(initialize(io,s) && ok(acquire(io,s,a,BootHDMIB)) && ok(suspend(io,s)));
     io.failedEnable=0x400;auto r=restore(io,s);
     assert(r.status==Status::StateTimeout && s.faulted && s.suspended && a.owner==&s && s.refs[0]==1 && s.owned==5);}
    {IO io;State s;Lease a;assert(initialize(io,s) && ok(acquire(io,s,a,BootHDMIB)));
     io.regs[Driver]&=~0x800U;const auto count=io.writes.size();
     assert(restore(io,s).status==Status::RequestLost && s.faulted && io.writes.size()==count && a.owner==&s);}
    {IO io;State s;Lease a;assert(initialize(io,s) && ok(acquire(io,s,a,BootHDMIB)));
     io.regs[Driver]&=~0x800U;const auto count=io.writes.size();
     assert(suspend(io,s).status==Status::RequestLost && s.faulted && io.writes.size()==count && a.owner==&s);}
    // Combo B AUX uses only PW1 + AUX driver bit3; no PG fuse bit for AUX.
    { IO io; State s; Lease a; assert(initialize(io,s));
      assert(ok(acquire(io,s,a,bit(Domain::AUXB))) && a.domains==0x41 && s.refs[6]==1);
      assert(io.writes.size()==2 && io.writes[0].first==0x45404 && io.writes[0].second==2 &&
          io.writes[1].first==0x45444 && io.writes[1].second==8);
      assert(ok(suspend(io,s)) && s.suspended && !s.owned && a.domains==0x41);
      assert(io.writes[2].first==0x45444 && !(io.regs[0x45444]&8));
      assert(ok(restore(io,s)) && s.refs[6]==1 && s.owned==0x41 && ok(release(io,s,a)));empty(s); }
    // A pre-existing AUX driver request stays borrowed. BIOS/debug request
    // ownership is independent and never mutated, including final release.
    { IO io; io.regs[0x45404]=0xFC0F; io.regs[0x45444]=0xC;
      State s; Lease a; assert(initialize(io,s) && s.initialAUX==0xC && s.borrowed==0x5F);
      assert(ok(acquire(io,s,a,bit(Domain::AUXB))) && ok(release(io,s,a)) && io.writes.empty()); }
    { IO io; io.regs[0x45404]=0xFC0F;io.regs[0x45440]=8;io.regs[0x4544C]=8;
      State s; Lease a; assert(initialize(io,s) && ok(acquire(io,s,a,bit(Domain::AUXB))));
      assert(ok(release(io,s,a)) && !(io.regs[0x45444]&8) && (io.regs[0x45444]&4) &&
          io.regs[0x45440]==8 && io.regs[0x4544C]==8); }
    { IO io; State s; Lease a; assert(initialize(io,s));io.failedEnable=4;
      auto r=acquire(io,s,a,bit(Domain::AUXB));
      assert(r.status==Status::StateTimeout && r.domain==6 && s.faulted && !a.owner && !s.owned &&
          !(io.regs[0x45444]&8) && !(io.regs[0x45404]&2)); }
    { IO io; State s; Lease a; assert(initialize(io,s));io.deny=0x45444;
      assert(acquire(io,s,a,bit(Domain::AUXB)).status==Status::Unavailable && io.writes.empty()); }
    // A domains are opt-in; old B callers retain their seven-domain mask.
    assert(control(unsigned(Domain::DDIA))==DDI && request(unsigned(Domain::DDIA))==2 && enabled(unsigned(Domain::DDIA))==1);
    assert(control(unsigned(Domain::AUXA))==AUX && request(unsigned(Domain::AUXA))==2);
    {IO io;State s;assert(!initialize(io,s,bit(Domain::DDIA)) && io.writes.empty());
     assert(!initialize(io,s,1U<<Count) && io.writes.empty());}
    {IO io;State s;Lease a;assert(initialize(io,s) && s.allowed==LegacyDomains);
     assert(acquire(io,s,a,BootHDMIA).status==Status::InvalidDomains && io.writes.empty());}
    // A and B on independent pipes share PW1, not their DDI request bits.
    {IO io;State s;Lease a,b;assert(initialize(io,s,All));
     assert(ok(acquire(io,s,a,BootHDMIA)));
     assert(ok(acquire(io,s,b,bit(Domain::PipeB)|bit(Domain::DDIB))));
     assert(s.refs[0]==2 && s.refs[1]==1 && io.regs[DDI]==0xf);
     assert(ok(release(io,s,a)) && io.regs[DDI]==0xc && s.refs[0]==1 && s.refs[1]==1);
     assert(ok(release(io,s,b)));empty(s);}
    {IO io;State s;Lease a,b;assert(initialize(io,s,All));
     assert(ok(acquire(io,s,a,bit(Domain::AUXA))) && ok(acquire(io,s,b,bit(Domain::AUXB))));
     assert(io.regs[AUX]==0xf && s.refs[0]==2);
     assert(ok(release(io,s,b)) && io.regs[AUX]==3 && s.refs[0]==1);
     assert(ok(release(io,s,a)));empty(s);}
    // Preserve borrowed A/B requests and restore leases without duplicate refs.
    {IO io;io.regs[DDI]=3;State s;Lease a;assert(initialize(io,s,All) && (s.borrowed&bit(Domain::DDIA)));
     assert(ok(acquire(io,s,a,BootHDMIA)) && ok(release(io,s,a)) && io.regs[DDI]==3);empty(s);}
    {IO io;io.regs[DDI]=3;State s;Lease b;assert(initialize(io,s) && !(s.borrowed&bit(Domain::DDIA)));
     assert(ok(acquire(io,s,b,BootHDMIB)) && ok(release(io,s,b)) && io.regs[DDI]==3);empty(s);}
    {IO io;State s;Lease a,b;assert(initialize(io,s,All));
     assert(ok(acquire(io,s,a,BootHDMIA)) && ok(acquire(io,s,b,bit(Domain::PipeB)|bit(Domain::DDIB))));
     assert(ok(suspend(io,s)) && s.suspended);assert(ok(restore(io,s)) && !s.suspended && s.refs[0]==2 && io.regs[DDI]==0xf);
     assert(ok(release(io,s,b)) && ok(release(io,s,a)));empty(s);}
    // The DSI selector runs with PW1 on and before the DDI A request.
    for(bool select : {false,true}) {
        IO io;State s;Lease a;assert(initialize(io,s,All));
        assert(ok(acquire(io,s,a,BootHDMIA)) && ok(suspend(io,s)));
        unsigned calls=0;
        auto r=restore(io,s,true,[&](uint32_t ddi) {
            ++calls;assert((io.regs[Driver]&3)==3 && !(ddi&3));return select;
        });
        assert(calls==1 && ok(r)==select && bool(io.regs[DDI]&2)==select);
    }
    // A state failure rolls back A/PW_A/PW1, never a borrowed B request.
    {IO io;io.regs[DDI]=0xc;io.failedEnableReg=DDI;io.failedEnable=1;
     State s;Lease a;assert(initialize(io,s,All));auto r=acquire(io,s,a,BootHDMIA);
     assert(r.status==Status::StateTimeout && r.domain==unsigned(Domain::DDIA) && io.time==1000);
     assert(!a.owner && !r.rollbackFailed && s.faulted && io.regs[DDI]==0xc);empty(s);}
    puts("PASS Display13 power requests: leases, suspend/restore without count duplication, borrowed requests, bounded faults. Synthetic IO only.");
}
