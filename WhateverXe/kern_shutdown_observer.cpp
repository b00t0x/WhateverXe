// Optional, explicitly armed diagnostics for the qualified Big Sur kernel.
// No timeout, panic, power-policy or shutdown-return-value changes.
#include "kern_shutdown_observer.hpp"
#include <Headers/kern_util.hpp>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IONVRAM.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSSymbol.h>
#include <mach-o/loader.h>
#include <i386/machine_routines.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include <kern/clock.h>
namespace ShutdownObserver {
static const char *traceKey="4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:wex-shutdown-trace";
struct Event { uint32_t code, detail; uint64_t time, pc; char label[24]; };
struct Journal { uint32_t magic, version; uint64_t run; uint32_t count, dropped, failed, reserved; Event events[32]; };
static Journal journal {};
static IODTNVRAM *nvram;
static OSData *snapshots[2];
static const OSSymbol *traceSymbol;
static IOLock *lock;
static bool armed;
static uint64_t ready[8] {};
static void (*syncNvram)(IODTNVRAM *,bool);
static int (*orgReboot)(int,char *);
static void (*orgNotify)(int);
static int (*orgSync)(proc_t,void *,int *);
static void (*orgHaltLog)(const char *,const void *,uint64_t);
static int (*orgPEHalt)(unsigned int,uint32_t);
static void (*orgPlatform)(void *,unsigned int);
// _sync starts with a RIP-relative LEA. Lilu copies prologues verbatim,
// so replay that exact prefix with its original callback address explicitly.
extern "C" {
uintptr_t wexShutdownSyncCallback=0, wexShutdownSyncContinue=0;
}
__attribute__((naked,noinline)) static int syncBridge(proc_t,void *,int *) {
 __asm__("pushq %rbp\n\tmovq %rsp, %rbp\n\tmovq _wexShutdownSyncCallback(%rip), %rsi\n\tjmpq *_wexShutdownSyncContinue(%rip)");
}
static void publish() { IORegistryEntry::getRegistryRoot()->setProperty("WEXShutdownObserver",ready,sizeof(ready)); }
// All call sites precede the final platform power-off. Refuse an interrupt-off
// context; never take a blocking diagnostic lock or allocate journal storage here.
static void record(uint32_t code,uint32_t detail=0,const char *label=nullptr,const void *pc=nullptr) {
 if(!armed || !ml_get_interrupts_enabled())return;
 if(!IOLockTryLock(lock)){OSIncrementAtomic(reinterpret_cast<volatile SInt32 *>(&journal.dropped));return;}
 if(journal.count>=128){IOLockUnlock(lock);return;}
 auto &e=journal.events[journal.count%32];bzero(&e,sizeof(e));
 e.code=code;e.detail=detail;e.time=mach_absolute_time();e.pc=reinterpret_cast<uint64_t>(pc);
 if(label)strlcpy(e.label,label,sizeof(e.label));
 ++journal.count;
 auto data=snapshots[journal.count%2];
 memcpy(const_cast<void *>(data->getBytesNoCopy()),&journal,sizeof(journal));
 const bool written=nvram->setProperty(traceSymbol,data);
 if(written)syncNvram(nvram,false); // qualified syncInternal(false): no 15-minute throttle
 auto read=OSDynamicCast(OSData,nvram->getProperty(traceSymbol));
 const bool same=read && read->getLength()==sizeof(journal) && !memcmp(read->getBytesNoCopy(),&journal,sizeof(journal));
 if(!written || !same)++journal.failed;
 ready[3]=journal.count;ready[4]=journal.failed;ready[5]=journal.dropped;
 IOLockUnlock(lock);
}
// No registry access from normal sync calls. Only this root-only control
// operation resolves NVRAM and arms the observer after userspace is online.
static int armControl SYSCTL_HANDLER_ARGS {
 uint64_t token=0;
 int error=sysctl_handle_quad(oidp,&token,0,req);
 if(error || !req->newptr)return error;
 if(ready[2]!=63 || token!=journal.run)return EINVAL;
 if(armed)return 0;
 auto entry=IORegistryEntry::fromPath("IODeviceTree:/options");
 nvram=OSDynamicCast(IODTNVRAM,entry);
 if(!nvram){if(entry)entry->release();ready[6]=2;publish();return ENXIO;}
 armed=true;record(1,0,"probe");publish();
 return journal.failed ? EIO : 0;
}
SYSCTL_PROC(_debug, OID_AUTO, wex_shutdown_arm, CTLTYPE_QUAD | CTLFLAG_RW | CTLFLAG_LOCKED,
 nullptr, 0, armControl, "Q", "Arm bounded shutdown diagnostics for this boot");
static int wrapSync(proc_t p,void *a,int *r) { record(30);int v=orgSync(p,a,r);record(31,static_cast<uint32_t>(v));return v; }
static int wrapReboot(int howto,char *message) { record(10,howto);return orgReboot(howto,message); }
static void wrapNotify(int stage) { record(20,stage);orgNotify(stage);record(21,stage); }
static void wrapPlatform(void *self,unsigned int type) { record(60,type);orgPlatform(self,type);record(61,type); }
static int wrapPEHalt(unsigned int type,uint32_t details) { if(type==0 || type==1 || type==3)record(50,type);int v=orgPEHalt(type,details);if(type==0 || type==1 || type==3)record(51,type);return v; }
static void wrapHaltLog(const char *what,const void *pc,uint64_t time) {
 orgHaltLog(what,pc,time);
 if(!what)return;
 // Per-driver successful callbacks are numerous; retain slow/outstanding ones.
 if(!strcmp(what,"PowerOff/Restart handler completed"))return;
 record(40,0,what,pc);
}
void install(KernelPatcher &p) {
 if(!checkKernelArgument("-wex-shutdown-observe"))return;
 ready[0]=1;
 const auto base=p.solveSymbol(KernelPatcher::KernelID,"__mh_execute_header");
 if(!base){p.clearError();publish();return;}
 auto h=reinterpret_cast<const mach_header_64 *>(base);
 const uint8_t expected[]={0x51,0x9a,0x08,0x98,0xed,0x4b,0x3e,0x72,0x8f,0x00,0x1b,0x49,0xf7,0x15,0xa4,0x74};
 bool match=false;
 if(h->magic!=MH_MAGIC_64 || h->sizeofcmds>65536 || h->ncmds>128){publish();return;}
 auto at=reinterpret_cast<const uint8_t *>(h+1);auto end=at+h->sizeofcmds;
 for(uint32_t i=0;i<h->ncmds && at+sizeof(load_command)<=end;i++) {
  auto c=reinterpret_cast<const load_command *>(at);
  if(c->cmdsize<sizeof(load_command)||at+c->cmdsize>end)break;
  if(c->cmd==LC_UUID && c->cmdsize==sizeof(uuid_command))match=!memcmp(reinterpret_cast<const uuid_command *>(c)->uuid,expected,16);
  at+=c->cmdsize;
 }
 if(!match){publish();return;}
 const auto sync=p.solveSymbol(KernelPatcher::KernelID,"__ZN9IODTNVRAM12syncInternalEb");
 // Match the exact function entry as well as the kernel UUID.
 const uint8_t syncBytes[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x83,0xbf,0x88,0,0,0,0};
 if(!sync || memcmp(reinterpret_cast<void *>(sync),syncBytes,sizeof(syncBytes))){p.clearError();publish();return;}
 syncNvram=reinterpret_cast<decltype(syncNvram)>(sync);
 // NVRAM may not be published when Lilu routes the kernel; resolve it on arm.
 lock=IOLockAlloc();traceSymbol=OSSymbol::withCString(traceKey);
 journal.magic=0x57585344;journal.version=1;journal.run=mach_absolute_time()^base;ready[1]=journal.run;
 snapshots[0]=OSData::withBytes(&journal,sizeof(journal));snapshots[1]=OSData::withBytes(&journal,sizeof(journal));
 if(!lock || !traceSymbol || !snapshots[0] || !snapshots[1]){ready[6]=1;publish();return;}
 KernelPatcher::RouteRequest routes[]={
  {"_reboot_kernel",wrapReboot,orgReboot},
  {"_IOSystemShutdownNotification",wrapNotify,orgNotify},
  {"_sync",wrapSync},
  {"_halt_log_enter",wrapHaltLog,orgHaltLog},
  {"_PEHaltRestartInternal",wrapPEHalt,orgPEHalt},
  {"__ZN14IOPMrootDomain25handlePlatformHaltRestartEj",wrapPlatform,orgPlatform}
 };
 // Every automatic trampoline copies exactly six position-independent
 // bytes. Reject changed entries or out-of-range rel32 jumps before patching.
 const uint8_t simple[]={0x55,0x48,0x89,0xe5,0x41,0x57};
 const uint8_t syncEntry[]={0x55,0x48,0x89,0xe5,0x48,0x8d,0x35,0x25,0,0,0,0xbf,1,0,0,0};
 for(auto &r:routes) {
  auto address=p.solveSymbol(KernelPatcher::KernelID,r.symbol);
  if(!address){ready[6]=3;p.clearError();publish();return;}
  const auto delta=static_cast<int64_t>(r.to-(address+5));
  if(delta!=static_cast<int32_t>(delta)){ready[6]=5;publish();return;}
  if(!strcmp(r.symbol,"_sync")) {
   if(memcmp(reinterpret_cast<void *>(address),syncEntry,sizeof(syncEntry))){ready[6]=6;publish();return;}
   wexShutdownSyncCallback=address+0x30;wexShutdownSyncContinue=address+11;orgSync=syncBridge;
  } else if(memcmp(reinterpret_cast<void *>(address),simple,sizeof(simple))){ready[6]=7;publish();return;}
 }
 if(p.routeMultipleShort(KernelPatcher::KernelID,routes,arrsize(routes))){ready[2]=63;sysctl_register_oid(&sysctl__debug_wex_shutdown_arm);}
 else {ready[6]=4;ready[7]=static_cast<uint64_t>(p.getError());p.clearError();}
 publish();
}
}
