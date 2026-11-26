"""Check DC6 entry/exit against the donor's reference-clock table and power transitions."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
donor = Path(sys.argv[1]).read_bytes()
for site, expected in ((0x474a3, 'e85a080000'), (0x474fc, 'e8530b0000'),
                       (0x480a9, '83e0fc'), (0x5a8b7, 'be08500400')):
    if donor[site:site + len(bytes.fromhex(expected))] != bytes.fromhex(expected):
        raise ValueError(f'Unexpected display power transition at {site:#x}')

source = (root/'WhateverXe/kern_xe.cpp').read_text()
body = source[source.index('void *XE::tglDisplayRetentionAccess'):source.index('void XE::wrapTGLComboEnableClocks(')]
header = (root/'WhateverXe/kern_xe.hpp').read_text()
fields = header[header.index('    struct TGLDisplayRetention'):header.index('    mach_vm_address_t orgComboEnableClocks')]
fixture = r'''
#include <array>
#include <map>
#include <mutex>
#include <vector>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include "kern_display13_clock.hpp"
#include "kern_tgl_pcode_gate.hpp"
using mach_vm_address_t=uintptr_t;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
#define arrsize(a) (sizeof(a)/sizeof(*(a)))
#define SYSLOG(...) ((void)0)
static unsigned microseconds;
static void IODelay(unsigned us) { microseconds += us; }
static TGLPcodeGate tglPcodeGate;
static void *current_thread(){return reinterpret_cast<void *>(1);}
struct TGLPcodeWait {void delayUs(unsigned n){IODelay(n);}void sleepMs(unsigned n){IODelay(1000*n);}};
using IOLock = std::mutex;
static void IOLockLock(IOLock *m){m->lock();}
static void IOLockUnlock(IOLock *m){m->unlock();}
struct XE {
 bool tglPcodeNativeReady(){return true;}
FIELDS
 void *tglObservationController;
 uintptr_t orgTGLRegisterRead,orgTGLRegisterWrite;
 static uint32_t wrapTGLRegisterRead(void *,unsigned long);
 static void wrapTGLRegisterWrite(void *,unsigned long,uint32_t);
};
static XE target,*callbackXE=&target;
BODY
alignas(8) unsigned char ctl[0x1000] asm("_fixtureController");
alignas(8) static unsigned char power[0x100],access[0x100];
static uint32_t table[27];
static std::map<unsigned long,uint32_t> regs;
static std::vector<unsigned long> work;
struct ClockSetting { uint32_t ratio, decimal, hz; };
static constexpr ClockSetting clocks[][3] = {
 {{26,622,624000000},{46,1102,1104000000},{54,1294,1296000000}},
 {{32,612,614400000},{58,1112,1113600000},{68,1304,1305600000}},
 {{16,612,614400000},{29,1112,1113600000},{34,1304,1305600000}}
};
static ClockSetting clockSetting;
enum Scenario { Normal, Nested, PllLockFailure, PowerReleased, GateBusy, MailboxBusy, MailboxRejected, MissingReady, FinalVoteRejected };
static Scenario scenario;
static unsigned prepares;
static bool pllCanLock;
static unsigned nativeEntries,nativeExits;
template<class T> static void put(unsigned char *p,unsigned off,T value){memcpy(p+off,&value,sizeof(value));}
static uint32_t read(void *a,unsigned long r){assert(a==access);return regs.at(r);}
static void write(void *a,unsigned long r,uint32_t v){
 assert(a==access);
 if(r==Display13Clock::Data || r==Display13Clock::Data1){regs[r]=v;return;}
 if(r==Display13Clock::Mailbox) {
  assert(v==0x80000007 && tglPcodeGate.owner==1);
  unsigned vote=regs[Display13Clock::Data];
  if(vote==3)++prepares;
  if(prepares==1 || vote!=3)work.push_back(vote);
  regs[r]=(scenario==MailboxRejected || (scenario==FinalVoteRejected && vote!=3))?1:0;
  regs[Display13Clock::Data]=(scenario==MissingReady)?0:1;
  return;
 }
 if(r==0x46070) {
  assert((v&0xff)==clockSetting.ratio);
  work.push_back(v&(1u<<31)?0x46071:0x46070);
  regs[r]=v|(pllCanLock && (v&(1u<<31))?1u<<30:0);
  return;
 }
 if(r==0x46000) {assert(regs[0x46070]>>30==3 && v==regs[r]);work.push_back(r);return;}
 assert(r==0x45008 || r==0x44fe8);
 assert(v==(regs[r]|0x80000000u));work.push_back(r);regs[r]=v|0x40000000u;
}
static uint32_t voltage(void *c){assert(c==ctl);return 2;}
static void enter(void *) {
 ++nativeEntries;
 if(*reinterpret_cast<uint32_t *>(ctl+0xb48)!=1)return;
 auto &count=*reinterpret_cast<int32_t *>(ctl+0xc24);
 if(count++==0)regs[0x45504]=(regs[0x45504]&~3u)|2;
}
static void exitDC(void *) {
 ++nativeExits;
 if(*reinterpret_cast<uint32_t *>(ctl+0xb48)!=1)return;
 auto &count=*reinterpret_cast<int32_t *>(ctl+0xc24);
 if(count--==1)regs[0x45504]&=~3u;
}
static void callEnter(){XE::wrapTGLEnableHWDC6(ctl);}
static void callExit(){XE::wrapTGLDisableHWDC6(ctl);}
int main(int argc,char **argv){
 assert(argc==2);std::ifstream f(argv[1],std::ios::binary);
 f.seekg(0x99fc0);f.read(reinterpret_cast<char *>(table),sizeof(table));assert(f);
 target.tglCDClockTable=(uintptr_t)table;target.tglDisplayVoltageLevel=(uintptr_t)voltage;
 target.orgTGLEnableHWDC6=(uintptr_t)enter;target.orgTGLDisableHWDC6=(uintptr_t)exitDC;
 target.orgTGLRegisterRead=(uintptr_t)read;target.orgTGLRegisterWrite=(uintptr_t)write;
 static IOLock lock;target.tglDC6Lock=&lock;target.tglDC6Ready=true;
 target.tglObservationController=ctl;
 put(ctl,0xc8,power);put(ctl,0xc40,access);put(power,0x10,ctl);put(power,0x78,access);
 put<uint32_t>(power,0x20,1);put(access,0x50,access);put<uint32_t>(access,0x60,0x200000);
 put<uint32_t>(ctl,0xb48,1);
 // Exercise the real wrappers, substituting only the native calls and hardware.
 // Native signed counter and DC_STATE_EN transitions follow the donor.
 for(unsigned reference=0;reference<3;++reference)for(auto gear:clocks[reference]) {
  clockSetting=gear;put<uint64_t>(ctl,0xe98,gear.hz);put<uint32_t>(ctl,0xea8,reference);
 for(unsigned owned=0;owned<4;++owned)for(unsigned lost=0;lost<4;++lost)
 for(unsigned clockLost=0;clockLost<2;++clockLost)for(auto current:{Normal,Nested,PllLockFailure,PowerReleased,GateBusy,MailboxBusy,MailboxRejected,MissingReady,FinalVoteRejected}){
  scenario=current;prepares=0;tglPcodeGate.owner=(scenario==GateBusy)?1:0;
  regs={{0x46070,0xc0000000u|gear.ratio},{0x46000,0x380000u|gear.decimal},{0x51004,reference<<29},{0x45504,0},
        {0x45008,0x60c000|((owned&1)?0xc0000000u:0u)},
        {0x44fe8,0x4010|((owned&2)?0xc0000000u:0u)}};
  regs[Display13Clock::Mailbox]=scenario==MailboxBusy?0x80000000u:0;
  work.clear();microseconds=0;nativeEntries=nativeExits=0;pllCanLock=scenario!=PllLockFailure;
  put<uint32_t>(ctl,0xc24,0);target.tglDisplayRetention={};
  const auto expected=regs;
  callEnter();
  if(scenario==Nested){callEnter();callExit();assert(work.empty() && regs[0x45504]==2);}

  if(clockLost)regs[0x46070]=gear.ratio;
  if(lost&1)regs[0x45008]&=0x3fffffff;
  if(lost&2)regs[0x44fe8]&=0x3fffffff;
  if(scenario==PowerReleased)put<uint32_t>(power,0x20,0);
  callExit();put<uint32_t>(power,0x20,1);
  std::vector<unsigned long> wanted;
  if(scenario!=PowerReleased) {
   bool prepared=scenario!=GateBusy && scenario!=MailboxBusy && scenario!=MailboxRejected && scenario!=MissingReady;
   if(clockLost && !prepared && (scenario==MailboxRejected || scenario==MissingReady))wanted={3};
   if(clockLost && prepared) {
    wanted={3,0x46070,0x46071,pllCanLock?0x46000UL:0x46070UL};
    if(pllCanLock)wanted.push_back(2);
    if(!pllCanLock)assert(microseconds==200 && regs[0x46070]==gear.ratio);
   }
   if(!clockLost || (prepared && pllCanLock && scenario!=FinalVoteRejected)){
    if(owned&lost&1)wanted.push_back(0x45008);
    if(owned&lost&2)wanted.push_back(0x44fe8);
    assert(regs[0x45008]==expected.at(0x45008) && regs[0x44fe8]==expected.at(0x44fe8));
   }
  }
  assert(nativeEntries==(scenario==Nested?2u:1u) && nativeExits==nativeEntries && work==wanted);
  // A second native exit has no saved state left to replay.
  callExit();assert(work==wanted && nativeExits==nativeEntries+1);
 }
 }
 // A negative count represents an active blocker. Its balanced pair must
 // neither replace the saved resources nor allow a hardware DC6 transition.
 scenario=Normal;put<int32_t>(ctl,0xc24,-1);regs[0x45504]=0;work.clear();
 callEnter();assert(regs[0x45504]==0 && !target.tglDisplayRetention.controller);
 callExit();assert(work.empty() && *reinterpret_cast<int32_t *>(ctl+0xc24)==-1);
 // A donor transition with a missing hardware permission is not an exit
 // from retained DC6, even when its software count is one.
 put<int32_t>(ctl,0xc24,0);regs[0x46070]=0xc0000000u|clockSetting.ratio;
 callEnter();assert(target.tglDisplayRetention.controller==ctl);
 regs[0x45504]=0;regs[0x46070]=clockSetting.ratio;callExit();
 assert(work.empty() && !target.tglDisplayRetention.controller);

}
'''
with tempfile.TemporaryDirectory() as directory:
    d = Path(directory)
    (d/'fixture.cpp').write_text(fixture.replace('FIELDS',fields).replace('BODY',body))
    subprocess.run(['xcrun','clang++','-I',root/'WhateverXe','-std=c++17','-arch','x86_64','-O1',d/'fixture.cpp','-o',d/'check'], check=True)
    subprocess.run([d/'check',sys.argv[1]],check=True)
