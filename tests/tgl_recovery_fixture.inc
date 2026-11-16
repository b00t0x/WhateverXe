#include "kern_tgl_recovery.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
alignas(16) uint8_t channel[0x180],affected[0x180],ring[0x100],context[0xD0];
alignas(16) uint8_t owner[0x1300],otherOwner[0x1300],scheduler[0x200],otherScheduler[0x200];
alignas(16) uint8_t events[0x10],otherEvents[0x10],ringVT[0x200],schedulerVT[0x200],eventsVT[0x200];
uint32_t tglRecoveryCalls {},tglRecoveryReturns {};
uint64_t tglRecoveryRecords[16][16] {},ticks {};
unsigned resets,resumes,stage;bool nativeOK,replaceScheduler,replaceOwner,timeout,expectStamp;
uint32_t initialFailures;
using TGLRecovery::field;

bool reset(void *r,void *c) {
    assert(r==ring && c==context && stage++==0);++resets;
    assert(tglRecoveryCalls==1 && !tglRecoveryReturns && !tglRecoveryRecords[0][15]);
    // Reset owns this effect; the caller may only use the later native stamp.
    field<uint32_t>(affected,0x178)=0xABCDEF;
    if (replaceScheduler) field<void *>(owner,0x1250)=otherScheduler;
    return nativeOK;
}
void resume(void *s) {
    assert(s==(replaceScheduler ? otherScheduler : scheduler) && stage++==1);++resumes;
    assert(field<uint32_t>(affected,0x17C)==initialFailures+unsigned(!nativeOK && field<void *>(context,0xB8)));
    if (replaceOwner) field<void *>(channel,0x18)=otherOwner;
}
void increment(void *e,int index) {
    assert(expectStamp && e==(replaceOwner ? otherEvents : events) && index==17 && stage++==2);
    assert(!tglRecoveryReturns && !tglRecoveryRecords[0][15]);
}
void submit(void *a) {
    assert(expectStamp && a==affected && stage++==3);
    assert(field<uint32_t>(affected,0x178)==0xABCDEF);
    field<uint32_t>(affected,0x178)=0x12345;
}
bool wait(void *r,uint32_t *value,uint32_t target,uint32_t us) {
    assert(expectStamp && r==ring && stage++==4 && target==0x12345 && us==1000000);
    *value=timeout ? target-1 : target;return timeout;
}
const TGLRecovery::StampOps tglRecoveryStamp {submit,wait};
void clock_get_uptime(uint64_t *value) {*value=++ticks;}
}
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
struct XE { static void wrapTGLRecovery(void *); };
#include "recovery_entry.hpp"

int main() {
    for (uint8_t engine:{0,1,2,3,4,5,255}) for (bool ok:{false,true})
    for (bool replace:{false,true}) for (bool expired:{false,true}) for (bool present:{false,true})
    for (unsigned initial:{0U,5U,0xFFFFFFFFU}) {
        memset(channel,0,sizeof(channel));memset(affected,0,sizeof(affected));
        field<void *>(channel,0x130)=ring;field<void *>(channel,0x18)=owner;
        field<void *>(ring,0)=ringVT;field<void *>(ring,0x20)=context;field<uint8_t>(ring,0x40)=engine;
        field<void *>(context,0xB8)=present ? affected : nullptr;
        field<void *>(owner,0x1250)=scheduler;field<void *>(owner,0x380)=events;field<void *>(otherOwner,0x380)=otherEvents;
        field<void *>(scheduler,0)=schedulerVT;field<void *>(otherScheduler,0)=schedulerVT;
        field<void *>(events,0)=eventsVT;field<void *>(otherEvents,0)=eventsVT;
        field<decltype(&reset)>(ringVT,0x168)=reset;field<decltype(&resume)>(schedulerVT,0x180)=resume;
        field<decltype(&increment)>(eventsVT,0x1D8)=increment;
        field<uint32_t>(affected,0x20)=17;field<uint32_t>(affected,0x17C)=initial;
        initialFailures=initial;nativeOK=ok;replaceScheduler=replaceOwner=replace;timeout=expired;
        expectStamp=engine==2 && ok && present;
        resets=resumes=stage=tglRecoveryCalls=tglRecoveryReturns=0;ticks=0;
        std::memset(tglRecoveryRecords,0,sizeof(tglRecoveryRecords));
        XE::wrapTGLRecovery(channel);
        assert(resets==1 && resumes==1 && stage==(expectStamp ? 5 : 2));
        assert(tglRecoveryCalls==1 && tglRecoveryReturns==1);
        const auto q=tglRecoveryRecords[0];
        const uint32_t failed=initial+unsigned(present && (!ok || (expectStamp && expired)));
        assert(q[15]==1 && q[1]==1 && q[2]==1 && q[3]==2);
        assert(q[4]==reinterpret_cast<uintptr_t>(channel) && q[5]==reinterpret_cast<uintptr_t>(ring));
        assert(q[6]==reinterpret_cast<uintptr_t>(context) && q[7]==reinterpret_cast<uintptr_t>(present ? affected : nullptr));
        assert(q[8]==(present ? 17U : 0U) && q[9]==(present ? initial : 0U) && q[10]==(present ? failed : 0U) && q[11]==ok);
        assert(q[12]==(expectStamp ? (expired ? 2U : 1U) : 0U));
        assert(q[13]==(expectStamp ? 0x12345U : 0U) && q[14]==(expectStamp ? 0x12345U-unsigned(expired) : 0U));
        assert(field<uint32_t>(affected,0x17C)==failed && field<uint32_t>(channel,0x17C)==0);
        assert(field<uint32_t>(affected,0x178)==(expectStamp ? 0x12345U : 0xABCDEFU));
    }
    puts("BCS single-pass recovery: native ordering, bounded wait, failure accounting and real wrapper records");
}
