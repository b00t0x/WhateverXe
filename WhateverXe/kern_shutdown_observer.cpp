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
#include <kern/clock.h>
namespace ShutdownObserver {
static const char *traceKey="4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:wex-shutdown-trace";
static const char *armKey="4D1FDA02-38C7-4A6A-9CC6-4BCCA8B30102:wex-shutdown-arm";
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
static void tryArm() {
 if(armed || ready[2]!=63)return;
 auto d=OSDynamicCast(OSData,nvram->getProperty(armKey));
 if(!d || d->getLength()!=sizeof(uint64_t) || memcmp(d->getBytesNoCopy(),&journal.run,sizeof(uint64_t)))return;
 armed=true;record(1,0,"probe");
}
static int wrapSync(proc_t p,void *a,int *r) { bool before=armed;tryArm();record(30);int v=orgSync(p,a,r);record(31,v);if(armed && !before)publish();return v; }
static int wrapReboot(int howto,char *message) { record(10,howto);return orgReboot(howto,message); }
static void wrapNotify(int stage) { record(20,stage);orgNotify(stage);record(21,stage); }
static void wrapPlatform(void *self,unsigned int type) { record(60,type);orgPlatform(self,type);record(61,type); }
static int wrapPEHalt(unsigned int type,uint32_t details) { if(type<=2)record(50,type);int v=orgPEHalt(type,details);if(type<=2)record(51,type);return v; }
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
 nvram=OSDynamicCast(IODTNVRAM,IORegistryEntry::fromPath("/options",gIODTPlane));
 lock=IOLockAlloc();traceSymbol=OSSymbol::withCString(traceKey);
 journal.magic=0x57585344;journal.version=1;journal.run=mach_absolute_time()^base;ready[1]=journal.run;
 snapshots[0]=OSData::withBytes(&journal,sizeof(journal));snapshots[1]=OSData::withBytes(&journal,sizeof(journal));
 if(!nvram || !lock || !traceSymbol || !snapshots[0] || !snapshots[1]){publish();return;}
 KernelPatcher::RouteRequest routes[]={
  {"_reboot_kernel",wrapReboot,orgReboot},
  {"_IOSystemShutdownNotification",wrapNotify,orgNotify},
  {"_sync",wrapSync,orgSync},
  {"_halt_log_enter",wrapHaltLog,orgHaltLog},
  {"_PEHaltRestartInternal",wrapPEHalt,orgPEHalt},
  {"__ZN14IOPMrootDomain25handlePlatformHaltRestartEj",wrapPlatform,orgPlatform}
 };
 // Resolve all targets before any routing. A partial install stays unarmed.
 for(auto &r:routes)if(!p.solveSymbol(KernelPatcher::KernelID,r.symbol)){p.clearError();publish();return;}
 if(p.routeMultiple(KernelPatcher::KernelID,routes,arrsize(routes)))ready[2]=63;
 else p.clearError();
 publish();
}
}
