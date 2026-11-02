#include "kern_display13_wm.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
#include <vector>
using namespace Display13WM;
struct IO {
    std::map<uint32_t,uint32_t> r {{Mailbox,0},{Data,0},{Data1,0}};
    unsigned writes {},commands {},delay {},hold {},badCommand {};
    bool timeout {};
    bool readable(uint32_t reg) {return r.count(reg);}
    uint32_t read(uint32_t reg) {
        if(reg==Mailbox && (r[reg]&Enabled) && !timeout && delay>=hold) {
            const auto command=r[reg]&255;
            r[reg]=(commands==badCommand) ? 3 : 0;
            if(command==6)r[Data]=r[Data] ? 0x00909090 : 0x63503300;
            else if(command==0x23)r[Data]=30;
        }
        return r[reg];
    }
    void write(uint32_t reg,uint32_t v) {assert(r.count(reg));r[reg]=v;++writes;if(reg==Mailbox)++commands;}
    void delayUs(unsigned us) {delay+=us;}
};
struct DisplayIO {
    std::map<uint32_t,uint32_t> regs {{0x45404,0x403}};
    std::vector<uint32_t> writes;
    std::vector<uint32_t> reads;
    uint32_t drop {};
    DisplayIO(bool cursor,unsigned pipe=0) {
        const auto base=(cursor ? 0x70140U : 0x70240U)+pipe*0x1000;
        for(unsigned i=0;i<6;++i)regs[base+4*i]=0xDEADBEEF;
        regs[base+0x18]=regs[base+0x1C]=regs[base+0x28]=0xDEADBEEF;
    }
    bool readable(uint32_t reg) {return regs.count(reg);}
    uint32_t read(uint32_t reg) {assert(regs.count(reg));reads.push_back(reg);return regs[reg];}
    void write(uint32_t reg,uint32_t value) {assert(regs.count(reg));writes.push_back(reg);if(reg!=drop)regs[reg]=value;}
};
struct LatchIO : DisplayIO {
    uint64_t elapsed {},clock {614400000};
    bool frozen {},losePower {},oldSurface {},changeClock {},resetFrame {},advanceForeign {};
    uint32_t frameAdvance {1},surface {0x100000},cursorSurface {0x200000};
    unsigned pipeIndex {};
    void moveToPipe(unsigned index) {
        assert(!pipeIndex && index<3);
        const auto old=regs;regs.clear();pipeIndex=index;
        for(auto pair:old) {
            const auto reg=pair.first;
            regs[reg==0x45270 ? reg+4*index : reg>=0x60000 && reg<0x71000 ? reg+0x1000*index : reg]=pair.second;
        }
        regs[0x45404]=0xFC0F;reads.clear();
    }
    LatchIO(const Plan &p,const Plan *c=nullptr,bool only=false) : DisplayIO(false) {
        regs[0x70008]=0xC0000000;regs[0x70180]=0x84000000;regs[0x70080]=c ? 7 : 0;
        regs[0x70190]=0x0437077F;regs[0x60000]=(2199U<<16)|1919;
        regs[0x6000C]=(1124U<<16)|1079;regs[0x70040]=100;
        regs[0x45270]=pipeLineTime(p.htotal,p.pixelHz);
        for(unsigned i=1;i<5;++i)regs[0x70180+0x100*i]=0;
        regs[0x7027C]=0x07D70000;regs[0x7017C]=c ? 0x07FF07DC : 0;
        regs[0x45004]=8;regs[0x7019C]=surface;regs[0x701AC]=0;
        regs[0x70084]=cursorSurface;regs[0x700AC]=0;
        for(unsigned d=0;d<3;++d) {regs[0x70B00+0x100*d]=0x80001000;regs[0x70B08+0x100*d]=0;}
        assert(program(*this,p,false,only).status==Status::Complete);
        if(c) {
            for(unsigned i=0;i<6;++i)regs[0x70140+4*i]=0;
            regs[0x70158]=regs[0x7015C]=regs[0x70168]=0;
            assert(program(*this,*c,true,only).status==Status::Complete);
        }
        writes.clear();
    }
    uint32_t read(uint32_t reg) {
        if(reg>=0x70000 && reg<0x80000)assert(Display13FBIRQ::powered(regs[0x45404],pipeIndex));
        return DisplayIO::read(reg);
    }
    uint64_t nativeClockHz() {return clock;}
    uint64_t nowUs() {return elapsed;}
    void delayUs(unsigned us) {
        const auto before=elapsed;elapsed+=us;
        if(advanceForeign && pipeIndex)++regs[0x70040];
        if(before<2000 && elapsed>=2000) {
            const auto bank=pipeIndex*0x1000;
            if(!frozen)regs[0x70040+bank]=resetFrame ? 0 : regs[0x70040+bank]+frameAdvance;
            if(!oldSurface)regs[0x701AC+bank]=surface;
            regs[0x700AC+bank]=cursorSurface;
            if(changeClock)clock=384000000;
            if(losePower)regs[0x45404]=3;
        }
    }
    Snapshot snapshot(const Plan &p,const Plan *c=nullptr,bool only=false) {
        return {&p,c,regs[0x7027C+pipeIndex*0x1000],regs[0x7017C+pipeIndex*0x1000],surface,cursorSurface,8,clock,only,c!=nullptr,pipeLineTime(p.htotal,p.pixelHz),p.pixelHz,pipeIndex};
    }
};
int main() {
    assert(arbitrationControl(0x84001000)==0x94001000);
    assert(arbitrationControl(0x82001000)==0x92001000);
    assert(arbitrationControl(0x94001000)==0x94001000);
    assert(arbitrationControl(0x04001000)==0x04001000);
    assert(arbitrationControl(0x86001000)==0x86001000); // unsupported FP16
    assert(arbitrationControl(0x84009000)==0x84009000); // compressed
    for(uint32_t slots=0;slots<8;slots++) {
        const uint32_t ctl=0x84001000|(slots<<28),out=arbitrationControl(ctl);
        assert((out&~(7U<<28))==(ctl&~(7U<<28)) && ((out>>28)&7)==1);
        assert(arbitrationControl(out)==out);
    }

    // A quiet pipe A must not admit shared clock/engine changes during a
    // peer's programming or latch. Releasing A-only power must not invent a
    // dependency on unrelated B pending work, while a shared release must.
    struct Pipe {bool wmProgramming{},wmCommitting{};uint32_t wmPending{};} pipes[3];
    assert(!busyPipes(pipes,true));
    pipes[1].wmProgramming=true;assert(busyPipes(pipes)==2);
    pipes[2].wmCommitting=true;assert(busyPipes(pipes)==6);
    pipes[1].wmProgramming=false;pipes[2].wmCommitting=false;pipes[1].wmPending=1;
    assert(!busyPipes(pipes) && busyPipes(pipes,true)==2);
    assert(!(busyPipes(pipes,true)&1) && (busyPipes(pipes,true)&7));
    pipes[1].wmPending=0;pipes[0].wmCommitting=true;
    assert(busyPipes(pipes)==1); // Preserve single-output rejection.
    {struct Consumer {bool wmProgramming{},wmCommitting{},destroyingPath{};uint32_t wmPending{};
       uintptr_t memoryFramebuffer{};Latch wmLatch;uint64_t watermarkClockHz{};} consumers[3];
     for(unsigned p=0;p<3;++p) {consumers[p].memoryFramebuffer=p+1;consumers[p].wmLatch.status=Status::Complete;consumers[p].watermarkClockHz=614400000;}
     assert(clockConsumersReady(consumers,3,614400000) && clockConsumersReady(consumers,6,614400000));
     consumers[1].watermarkClockHz=384000000;
     assert(!clockConsumersReady(consumers,3,614400000) && clockConsumersReady(consumers,5,614400000));
     consumers[1].watermarkClockHz=614400000;consumers[2].wmCommitting=true;
     assert(!clockConsumersReady(consumers,3,614400000)); // Shared owner also checks peer work.
     consumers[2].wmCommitting=false;consumers[1].wmPending=1;assert(!clockConsumersReady(consumers,3,614400000));
     consumers[1].wmPending=0;consumers[1].destroyingPath=true;assert(!clockConsumersReady(consumers,3,614400000));
     consumers[1].destroyingPath=false;consumers[1].wmLatch.status=Status::Unavailable;assert(!clockConsumersReady(consumers,3,614400000));
     consumers[1].wmLatch.status=Status::Complete;consumers[1].memoryFramebuffer=0;assert(!clockConsumersReady(consumers,3,614400000));
     assert(!clockConsumersReady(consumers,0,614400000) && !clockConsumersReady(consumers,8,614400000) && !clockConsumersReady(consumers,1,0));
    }
    const TriggerPacket flips[] {{0x7027C,0x02A90028,19},{0x7019C,0,19},
        {0x7019C,0x40401000,19},{0x70084,0x110000,20}};
    const auto intent=triggerSurfaces(flips,4,0,0);
    assert(intent.valid && intent.primary==0x40401000 && intent.cursor==0x110000 && intent.changed==3);
    const auto unchanged=triggerSurfaces(nullptr,0,0x41300000,0x110000);
    assert(unchanged.valid && unchanged.primary==0x41300000 && !unchanged.changed);
    // Non-WM primary flip must be remembered by the next cursor-only queue.
    const TriggerPacket primaryFlip[] {{0x7019C,0x40401000,19}};
    const TriggerPacket cursorFlip[] {{0x70084,0x120000,20}};
    const auto ordinary=triggerSurfaces(primaryFlip,1,0x41300000,0x110000);
    const auto cursorOnly=triggerSurfaces(cursorFlip,1,ordinary.primary,ordinary.cursor);
    assert(cursorOnly.valid && cursorOnly.primary==0x40401000 && cursorOnly.cursor==0x120000 && cursorOnly.changed==2);
    assert(!triggerSurfaces(nullptr,1,0,0).valid && !triggerSurfaces(flips,65,0,0).valid);
    const TriggerPacket bad[] {{0x7019C,0x40401001,19}};
    assert(!triggerSurfaces(bad,1,0,0).valid);
    const TriggerPacket allOnes[] {{0x70084,UINT32_MAX,20}};
    assert(!triggerSurfaces(allOnes,1,0,0).valid);
    assert(writerMapping(0x100000,0x100000,0x4010000000ULL,0x1000000,0x4010000000ULL,0x1000000));
    assert(!writerMapping(0x200000,0x100000,0x4010000000ULL,0x1000000,0x4010000000ULL,0x1000000));
    assert(!writerMapping(0x100000,0x100000,0x4010000000ULL,0x1000000,0x4020000000ULL,0x1000000));
    assert(!writerMapping(0x100000,0x100000,0x4010000000ULL,0x1000000,0x4010000000ULL,0x800000));
    // Native direct swaps may supersede cached WM cursor intent; only the
    // current CPU-selected, correctly allocated buffer can be already applied.
    const auto direct=directCursorSurface(0x6000000,0,0,0x6000000,0x6040000,0x6000000);
    assert(direct.valid && direct.applied && direct.surface==0x6000000);
    assert(!directCursorSurface(0x6000000,0,1,0x6000000,0x6040000,0x6000000).applied);
    assert(!directCursorSurface(0x6000000,1,0,0x6000000,0x6040000,0x6000000).valid);
    assert(!directCursorSurface(0x6000000,0,2,0x6000000,0x6040000,0x6000000).valid);
    assert(!directCursorSurface(0xFFFF0000,0,0,0xFFFF0000,0,0xFFFF0000).valid);
    const TriggerPacket cursorAfterDirect[] {{0x70084,0x6040000,19}};
    const auto overridden=triggerSurfaces(cursorAfterDirect,1,0x41300000,direct.surface);
    assert(overridden.valid && overridden.cursor==0x6040000 && overridden.changed==2);
    // Captured native RGB8888/Y-tiled control, not the GOP linear control.
    assert(supportedRGBControl(0x94001008) && supportedRGBControl(0x94000008));
    // Actual dual hot-add switches pipe A to 32bpp RGB2101010, while B
    // stays RGB8888. Neither permits YUV, FP16, CCS or rotation.
    assert(supportedRGBControl(0x92001000) && supportedRGBControl(0x82000000));
    assert(!supportedRGBControl(0x92009000) && !supportedRGBControl(0x92001010));
    assert(!supportedRGBControl(0x92001001) && !supportedRGBControl(0x96001000));
    assert(!supportedRGBControl(0x91001000) && !supportedRGBControl(0x90801000));
    assert(!supportedRGBControl(0x94001408) && !supportedRGBControl(0x94009008));
    assert(!supportedRGBControl(0x94001018) && !supportedRGBControl(0x94001009));
    const auto y0=yTiledLevel(1920,2200,148500000,3,642);
    assert(y0.enable && y0.blocks==62 && y0.lines==4 && y0.minimum==123);
    const auto y51=yTiledLevel(1920,2200,148500000,54,642);
    assert(y51.enable && y51.blocks==62 && y51.lines==4 && y51.minimum==123);
    const auto yLong=yTiledLevel(1920,2200,148500000,147,642);
    assert(yLong.enable && yLong.blocks==154 && yLong.lines==10 && yLong.minimum==245);
    assert(!yTiledLevel(1920,2200,148500000,3,122).enable);
    const auto yt=transition(y0,642,true,61);
    assert(yt.enable && yt.blocks==137 && yt.minimum==138);
    assert(pipeLineTime(2200,148500000)==119); // Donor's truncation produced118.
    assert(pipeLineTime(2200,148000000)==119); // Preserve sub-MHz precision.
    assert(pipeLineTime(2000,160000000)==100); // Exact division.
    assert(pipeLineTime(16384,1000000)==511); // Saturate; never wrap the mask.
    assert(!pipeLineTime(0,148500000) && !pipeLineTime(16385,148500000));
    assert(!pipeLineTime(2200,0) && !pipeLineTime(2200,2000000001ULL));
    // Recorded S1 Linux curve [3,54,83,102,147,147] follows raw WM0=0 +3us.
    const auto latency=decodeLatency(0x63503300,0x00909090,30);
    assert(latency.status==Status::Complete);
    const unsigned expected[] {3,54,83,102,147,147};
    for(unsigned i=0;i<6;++i)assert(latency.us[i]==expected[i]);
    auto nonzero=decodeLatency(0x08060402,0x00000C0A,30);
    assert(nonzero.us[0]==2 && nonzero.us[1]==4); // No unconditional +2/+3.
    auto hole=decodeLatency(0x08000400,0x00000C0A,30);
    assert(hole.us[0]==3 && hole.us[1]==7);
    for(unsigned i=2;i<6;++i)assert(!hole.us[i]);
    assert(decodeLatency(0,0,0).status==Status::SAGV);
    IO io;const auto queried=queryLatency(io);
    assert(queried.status==Status::Complete && queried.writes==9 && queried.sagvUs==30);
    IO slow;slow.hold=1500;assert(queryLatency(slow).status==Status::Complete && slow.delay==1500);
    IO busy;busy.timeout=true;busy.r[Mailbox]=Enabled;
    const auto blocked=queryLatency(busy);
    assert(blocked.status==Status::MailboxBusy && blocked.mailboxStatus==Enabled && !busy.writes);
    IO timeout;timeout.timeout=true;
    assert(queryLatency(timeout).status==Status::MailboxTimeout && timeout.delay==20500 && timeout.writes==3);
    IO error;error.badCommand=2;const auto failed=queryLatency(error);
    assert(failed.status==Status::MailboxError && failed.raw[0]==0x63503300 && failed.writes==6);
    auto yp=plan(1920,2200,148500000,642,latency,true,true,1080);
    assert(yp.status==Status::Complete && yp.yTiled && yp.normal[0].minimum==123 && yp.sagv.enable);
    LatchIO yLatch(yp);yLatch.regs[0x70180]=0x94001008;
    assert(latch(yLatch,yLatch.snapshot(yp)).status==Status::Complete);
    LatchIO wrongTile(yp);
    assert(latch(wrongTile,wrongTile.snapshot(yp)).status==Status::Conflict);
    assert(cursorMatches(0,256,true) && cursorMatches(35,256) && cursorMatches(34,128) && cursorMatches(39,64));
    assert(!cursorMatches(35,64) && !cursorMatches(0,256) && !cursorMatches(4,64) && !cursorMatches(UINT32_MAX,256,true));
    auto c256=plan(256,2200,148500000,40,latency,true);
    assert(c256.status==Status::Complete && c256.sagv.enable && c256.normal[5].enable && c256.normal[5].minimum<=40);
    LatchIO cursor256(yp,&c256,true);cursor256.regs[0x70180]=0x94001008;cursor256.regs[0x70080]=35;
    assert(latch(cursor256,cursor256.snapshot(yp,&c256,true)).status==Status::Complete);
    LatchIO wrongCursorSize(yp,&c256,true);wrongCursorSize.regs[0x70180]=0x94001008;
    assert(latch(wrongCursorSize,wrongCursorSize.snapshot(yp,&c256,true)).status==Status::Conflict);
    auto p=plan(1920,2200,148500000,2008,latency,true,false,1080);
    assert(p.status==Status::Complete);
    const unsigned blocks[] {16,65,97,113,161,161},lines[] {1,4,6,7,10,10};
    for(unsigned i=0;i<6;++i) {
        assert(p.normal[i].enable && p.normal[i].blocks==blocks[i] && p.normal[i].lines==lines[i]);
        assert((p.normal[i].encoded()&4095)==blocks[i]);
    }
    assert(p.normal[0].minimum==19 && p.transition.blocks==30 && p.transition.minimum==31);
    assert(p.sagv.blocks==49 && p.sagv.lines==3 && p.sagvTransition.blocks==63);
    auto tight=plan(1920,2200,148500000,19,latency,true);
    assert(tight.status==Status::Complete && tight.normal[0].enable && !tight.normal[1].enable && !tight.sagv.enable);
    assert(plan(1920,2200,148500000,18,latency,true).status==Status::WM0);
    assert(!plan(1920,2200,148500000,2008,latency,false).transition.enable);
    // Cursor is the same linear RGB equation with its own width / allocation.
    auto cursor=plan(64,2200,148500000,36,latency,true);
    assert(cursor.status==Status::Complete && cursor.normal[0].blocks==6 && cursor.sagv.blocks==7);
    DisplayIO primaryIO(false);auto programmed=program(primaryIO,p,false,false);
    assert(programmed.status==Status::Complete && programmed.writes==9 && programmed.readbacks==9);
    for(unsigned i=0;i<6;++i)assert(primaryIO.regs[0x70240+4*i]==p.normal[i].encoded());
    assert(primaryIO.regs[0x70258]==p.sagv.encoded() && primaryIO.regs[0x7025C]==p.sagvTransition.encoded());
    assert(primaryIO.writes[6]==0x70268 && primaryIO.writes[7]==0x70258 && primaryIO.writes[8]==0x7025C);
    DisplayIO cursorIO(true);assert(program(cursorIO,cursor,true,true).status==Status::Complete);
    for(unsigned i=1;i<6;++i)assert(!cursorIO.regs[0x70140+4*i]);
    assert(!cursorIO.regs[0x70168] && cursorIO.regs[0x70158]==cursor.sagv.encoded());
    DisplayIO off(false);off.regs[0x45404]=0x3;
    assert(program(off,p,false,false).status==Status::Power && off.writes.empty());
    DisplayIO missing(false);missing.regs.erase(0x7025C);
    assert(program(missing,p,false,false).status==Status::Unavailable && missing.writes.empty());
    DisplayIO failedWrite(false);failedWrite.drop=0x70258;
    assert(program(failedWrite,p,false,false).status==Status::Readback && failedWrite.writes.size()==8);
    DisplayIO noSAGV(false);assert(program(noSAGV,tight,false,false).status==Status::SAGV && noSAGV.writes.empty());
    for(unsigned pipe=0;pipe<3;++pipe)for(unsigned height:{720U,1080U,1200U,1440U,2160U}) {
        auto variable=p;variable.height=height;LatchIO io(variable);io.moveToPipe(pipe);
        const auto bank=0x1000*pipe;
        io.regs[0x70190+bank]=((height-1)<<16)|(variable.width-1);
        io.regs[0x6000C+bank]=((height+45-1)<<16)|(height-1);
        assert(latch(io,io.snapshot(variable)).status==Status::Complete);
        io.regs[0x70190+bank]^=1U<<16;
        assert(latch(io,io.snapshot(variable)).status==Status::Readback);
        variable.height=0;assert(latch(io,io.snapshot(variable)).status==Status::Input);
    }
    // DSI timing follows transcoder0 while the surface/frame/WM proof stays
    // on the actual pipe. Remove ordinary timings so accidental fallback fails.
    for(unsigned pipe=0;pipe<3;++pipe) {
        LatchIO io(p);io.moveToPipe(pipe);const auto bank=0x1000*pipe;
        io.regs[0x6B000]=io.regs[0x60000+bank];io.regs[0x6B00C]=io.regs[0x6000C+bank];
        io.regs.erase(0x60000+bank);io.regs.erase(0x6000C+bank);
        io.regs[0x70008+bank]=0;io.regs[0x7B008]=0xC0000000;
        const uint32_t inputs[]={0,5,6};io.regs[0x6B400]=0x80000006|(inputs[pipe]<<12);
        auto expected=io.snapshot(p);expected.transcoders.dsi0Pipe=pipe;
        auto result=latch(io,expected);
        assert(result.status==Status::Complete && result.waitUs==2000 && io.writes.empty());
        io.regs[0x6B400]^=1U<<12;assert(latch(io,expected).status==Status::Conflict);
        io.regs[0x6B400]^=1U<<12;io.regs[0x70008+bank]=0xC0000000;
        assert(latch(io,expected).status==Status::Conflict);
    }
    LatchIO committed(p);const auto proof=latch(committed,committed.snapshot(p));
    assert(proof.status==Status::Complete && proof.frameBefore==100 && proof.frameAfter==101 && proof.waitUs==2000);
    assert(proof.budgetUs==51001 && committed.writes.empty());
    LatchIO flipped(p);flipped.surface=0x40401000;flipped.regs[0x7019C]=flipped.surface;
    auto staleIntent=flipped.snapshot(p);staleIntent.primarySurface=0;
    assert(latch(flipped,staleIntent).status==Status::Readback && !flipped.elapsed);
    const auto finalFlip=triggerSurfaces(flips,3,0,0);
    staleIntent.primarySurface=finalFlip.primary;
    assert(latch(flipped,staleIntent).status==Status::Complete);
    // A real cursor flip's live readback may follow the frame counter.
    struct DelayedCursor : LatchIO {
        bool stuck {};uint32_t previous {};
        DelayedCursor(const Plan &p,const Plan &c) : LatchIO(p,&c) {previous=cursorSurface-0x40000;}
        void delayUs(unsigned us) {
            LatchIO::delayUs(us);
            regs[0x700AC]=(stuck || elapsed<3000) ? previous : cursorSurface;
        }
    };
    DelayedCursor delayed(p,cursor);const auto delayedProof=latch(delayed,delayed.snapshot(p,&cursor));
    assert(delayedProof.status==Status::Complete && delayedProof.frameAfter!=delayedProof.frameBefore && delayedProof.waitUs==3000);
    DelayedCursor stuckCursor(p,cursor);stuckCursor.stuck=true;
    const auto stuckProof=latch(stuckCursor,stuckCursor.snapshot(p,&cursor));
    assert(stuckProof.status==Status::Unavailable && stuckProof.check==LatchCheck::AwaitingFlip && stuckProof.waitUs==52000 && stuckProof.lastReg==0x700AC);
    LatchIO oldLive(p);oldLive.surface=finalFlip.primary;oldLive.regs[0x7019C]=oldLive.surface;
    oldLive.oldSurface=true;
    const auto pendingFlip=latch(oldLive,oldLive.snapshot(p));
    assert(pendingFlip.status==Status::Unavailable && pendingFlip.check==LatchCheck::AwaitingFlip && oldLive.writes.empty());
    // Repeated attempts cannot manufacture completion or retire anything.
    oldLive.elapsed=0;
    assert(latch(oldLive,oldLive.snapshot(p)).status==Status::Unavailable);
    oldLive.elapsed=0;oldLive.oldSurface=false;
    assert(latch(oldLive,oldLive.snapshot(p)).status==Status::Complete);
    // An invalid read remains a fault, even at the live-register boundary.
    LatchIO invalidLive(p);invalidLive.oldSurface=true;invalidLive.regs[0x701AC]=UINT32_MAX;
    assert(latch(invalidLive,invalidLive.snapshot(p)).status==Status::Readback);
    auto clockPlan=committed.snapshot(p);
    assert(clockFits(clockPlan,1305600000) && clockFits(clockPlan,297000000));
    assert(!clockFits(clockPlan,296999999));
    auto fasterCursor=cursor;fasterCursor.pixelHz=200000000;
    auto cursorClockPlan=committed.snapshot(p,&fasterCursor);
    assert(clockFits(cursorClockPlan,400000000) && !clockFits(cursorClockPlan,399999999));
    clockPlan.pipePixelHz=200000000;clockPlan.pipeLineTime=pipeLineTime(p.htotal,clockPlan.pipePixelHz);
    assert(!clockFits(clockPlan,384000000));
    LatchIO afterCrawl(p);afterCrawl.clock=1305600000;
    assert(latch(afterCrawl,afterCrawl.snapshot(p)).status==Status::Complete && afterCrawl.writes.empty());
    LatchIO sameFrame(p);sameFrame.frozen=true;
    const auto timeoutProof=latch(sameFrame,sameFrame.snapshot(p));
    assert(timeoutProof.status==Status::CommitTimeout && timeoutProof.waitUs==52000);
    LatchIO staleSurface(p);staleSurface.oldSurface=true;
    assert(latch(staleSurface,staleSurface.snapshot(p)).status==Status::Unavailable);
    LatchIO poweredOff(p);poweredOff.losePower=true;
    assert(latch(poweredOff,poweredOff.snapshot(p)).status==Status::Power);
    LatchIO invalidWM(p);invalidWM.regs[0x70258]^=1;
    assert(latch(invalidWM,invalidWM.snapshot(p)).status==Status::Readback && !invalidWM.elapsed);
    LatchIO oldLineTime(p);oldLineTime.regs[0x45270]=118;
    assert(latch(oldLineTime,oldLineTime.snapshot(p)).status==Status::Readback && !oldLineTime.elapsed);
    LatchIO missingLineTime(p);missingLineTime.regs.erase(0x45270);
    assert(latch(missingLineTime,missingLineTime.snapshot(p)).status==Status::Unavailable && !missingLineTime.elapsed);
    LatchIO clockChanged(p);clockChanged.changeClock=true;
    assert(latch(clockChanged,clockChanged.snapshot(p)).status==Status::Conflict);
    LatchIO autonomous(p);autonomous.regs[0x70B08]=1;
    assert(latch(autonomous,autonomous.snapshot(p)).status==Status::Conflict);
    LatchIO extraPlane(p);extraPlane.regs[0x70280]=Enabled;
    assert(latch(extraPlane,extraPlane.snapshot(p)).status==Status::Conflict && !extraPlane.elapsed);
    LatchIO reset(p);reset.resetFrame=true;
    assert(latch(reset,reset.snapshot(p)).status==Status::Conflict);
    LatchIO wrap(p);wrap.regs[0x70040]=0xFFFFFFFE;wrap.frameAdvance=2;
    assert(latch(wrap,wrap.snapshot(p)).status==Status::Complete);
    LatchIO activeCursor(p,&cursor,true);
    assert(latch(activeCursor,activeCursor.snapshot(p,&cursor,true)).status==Status::Complete);
    // Independent register banks and intent, including 4-byte LINETIME
    // stride and the shared IPC register. Never obtain B/C retirement from A.
    const TriggerPacket both[] {{0x7019C,0x100000,19},{0x70084,0x200000,20},
        {0x7119C,0x300000,19},{0x71084,0x400000,20},
        {0x7219C,0x500000,19},{0x72084,0x600000,20}};
    for(unsigned pipe=0;pipe<3;++pipe) {
        const auto intent=triggerSurfaces(both,6,0,0,pipe);
        assert(intent.valid && intent.changed==3 && intent.primary==0x100000+0x200000*pipe && intent.cursor==0x200000+0x200000*pipe);
        DisplayIO programmed(false,pipe);programmed.regs[0x45404]=0xFC0F;
        assert(program(programmed,p,false,false,pipe).status==Status::Complete && programmed.writes.size()==9);
        for(auto reg:programmed.writes)assert(reg>=0x70240+pipe*0x1000 && reg<=0x70268+pipe*0x1000);
        LatchIO selected(p,&cursor,true);selected.moveToPipe(pipe);
        assert(latch(selected,selected.snapshot(p,&cursor,true)).status==Status::Complete);
        for(auto reg:selected.reads)assert(reg==0x45404 || reg==0x45004 || reg==0x45270+4*pipe ||
            (reg>=0x60000+0x1000*pipe && reg<0x61000+0x1000*pipe) ||
            (reg>=0x70000+0x1000*pipe && reg<0x71000+0x1000*pipe));
        LatchIO missing(p);missing.moveToPipe(pipe);missing.regs[0x45404]&=~(1U<<(10+2*pipe));missing.reads.clear();
        assert(latch(missing,missing.snapshot(p)).status==Status::Power && (missing.reads==std::vector<uint32_t>{0x45404}));
        DisplayIO missingProgram(false,pipe);missingProgram.regs[0x45404]=3;
        assert(program(missingProgram,p,false,false,pipe).status==Status::Power && missingProgram.writes.empty());
        LatchIO late(p);late.moveToPipe(pipe);late.frozen=true;late.advanceForeign=true;late.regs[0x70040]=200;
        assert(latch(late,late.snapshot(p)).status==Status::CommitTimeout);
        if(pipe)assert(late.regs[0x70040]>200 && late.regs[0x70040+pipe*0x1000]==100);
        LatchIO wrongWM(p);wrongWM.moveToPipe(pipe);wrongWM.regs[0x70258+pipe*0x1000]^=1;
        assert(latch(wrongWM,wrongWM.snapshot(p)).status==Status::Readback);
        LatchIO wrongLine(p);wrongLine.moveToPipe(pipe);wrongLine.regs[0x45270+4*pipe]=118;
        assert(latch(wrongLine,wrongLine.snapshot(p)).status==Status::Readback);
        // Reconnect keeps the old SURF while native WM/scaler flushes precede
        // plane enable. A nonzero address cannot certify an active scanout.
        LatchIO disabledPlane(p);disabledPlane.moveToPipe(pipe);
        disabledPlane.regs[0x70180+0x1000*pipe]&=~Enabled;
        const auto controls=latch(disabledPlane,disabledPlane.snapshot(p));
        assert(controls.status==Status::Unavailable && controls.check==LatchCheck::AwaitingScanout &&
            controls.pipeControl==0xC0000000 && controls.planeControl==0x04000000 &&
            !controls.cursorControl && disabledPlane.writes.empty() && !disabledPlane.elapsed && controls.frameAfter==0);
        disabledPlane.regs[0x70280+0x1000*pipe]=Enabled;
        assert(latch(disabledPlane,disabledPlane.snapshot(p)).status==Status::Conflict);
        disabledPlane.regs[0x70280+0x1000*pipe]=0;
        disabledPlane.regs[0x70180+0x1000*pipe]|=Enabled;
        disabledPlane.frozen=true;
        assert(latch(disabledPlane,disabledPlane.snapshot(p)).status==Status::CommitTimeout);
        disabledPlane.frozen=false;disabledPlane.elapsed=0;
        disabledPlane.regs[0x701AC+0x1000*pipe]=0;
        assert(latch(disabledPlane,disabledPlane.snapshot(p)).status==Status::Complete);
        // Native scaler flush before the first attached scanout: timing and
        // WM are programmed, but the primary remains disabled and SURF is 0.
        // This is pending, not success; a later enabled surface must earn the
        // ordinary frame/live proof. The same state applies on every pipe.
        LatchIO preparing(p);preparing.moveToPipe(pipe);const auto bank=0x1000*pipe;
        preparing.regs[0x70180+bank]=0;preparing.regs[0x7019C+bank]=0;
        preparing.regs[0x70040+bank]=0;preparing.surface=0;
        const auto pending=latch(preparing,preparing.snapshot(p));
        assert(pending.status==Status::Unavailable && pending.check==LatchCheck::AwaitingScanout &&
            preparing.reads.back()==Display13FBIRQ::DSBControl(pipe,2) && preparing.writes.empty() && !preparing.elapsed);
        // Pending cannot hide a foreign consumer or a changed shared clock.
        preparing.regs[0x70280+bank]=Enabled;
        assert(latch(preparing,preparing.snapshot(p)).status==Status::Conflict);
        preparing.regs[0x70280+bank]=0;preparing.regs[0x70B08+bank]=1;
        assert(latch(preparing,preparing.snapshot(p)).status==Status::Conflict);
        preparing.regs[0x70B08+bank]=0;auto intendedClock=preparing.snapshot(p);
        preparing.clock=384000000;
        assert(latch(preparing,intendedClock).status==Status::Conflict);
        preparing.clock=614400000;
        preparing.regs[0x70180+bank]=0x84000000;
        preparing.regs[0x7019C+bank]=preparing.surface=0x100000;
        assert(latch(preparing,preparing.snapshot(p)).status==Status::Complete && preparing.elapsed==2000);
        LatchIO badBlank(p);badBlank.moveToPipe(pipe);badBlank.surface=0;
        badBlank.regs[0x70180+bank]=UINT32_MAX;
        assert(latch(badBlank,badBlank.snapshot(p)).status==Status::Conflict);
        LatchIO blankWithCursor(p);blankWithCursor.moveToPipe(pipe);blankWithCursor.surface=0;
        blankWithCursor.regs[0x70180+bank]=0;blankWithCursor.regs[0x70080+bank]=7;
        assert(latch(blankWithCursor,blankWithCursor.snapshot(p)).status==Status::Conflict);
        // A qualified cursor may arrive before the first primary surface.
        // It cannot certify retirement; the later primary still needs VBlank
        // and live-surface evidence. An inconsistent cursor remains a fault.
        LatchIO cursorFirst(p,&cursor);cursorFirst.moveToPipe(pipe);
        cursorFirst.surface=0;cursorFirst.regs[0x70180+bank]=0;cursorFirst.regs[0x7019C+bank]=0;
        const auto cursorPending=latch(cursorFirst,cursorFirst.snapshot(p,&cursor));
        assert(cursorPending.status==Status::Unavailable && cursorPending.check==LatchCheck::AwaitingScanout &&
            cursorFirst.writes.empty() && !cursorFirst.elapsed);
        cursorFirst.regs[0x70080+bank]=0;
        assert(latch(cursorFirst,cursorFirst.snapshot(p,&cursor)).status==Status::Conflict);
        cursorFirst.regs[0x70080+bank]=0x22;
        assert(latch(cursorFirst,cursorFirst.snapshot(p,&cursor)).status==Status::Conflict);
        cursorFirst.regs[0x70080+bank]=7;
        cursorFirst.regs[0x70180+bank]=0x84000000;
        cursorFirst.regs[0x7019C+bank]=cursorFirst.surface=0x100000;
        assert(latch(cursorFirst,cursorFirst.snapshot(p,&cursor)).status==Status::Complete && cursorFirst.elapsed==2000);
        LatchIO peerPlane(p);peerPlane.moveToPipe(pipe);peerPlane.regs[0x70280+0x1000*pipe]=Enabled;
        const auto peer=latch(peerPlane,peerPlane.snapshot(p));
        assert(peer.status==Status::Conflict && peer.check==LatchCheck::OtherPlane &&
            peer.checkRegister==0x70280+0x1000*pipe && peer.checkValue==Enabled &&
            peerPlane.reads.back()==peer.checkRegister && peerPlane.writes.empty());
        LatchIO otherClock(p);otherClock.moveToPipe(pipe);auto beforeClock=otherClock.snapshot(p);
        otherClock.clock=384000000;
        const auto clockProof=latch(otherClock,beforeClock);
        assert(clockProof.status==Status::Conflict && clockProof.check==LatchCheck::Clock &&
            clockProof.clockHz==384000000 && otherClock.writes.empty());
        for(auto control:{1U,0x80000000U,UINT32_MAX}) {
            LatchIO dsb(p);dsb.moveToPipe(pipe);dsb.regs[0x70B08+0x1000*pipe]=control;
            const auto proof=latch(dsb,dsb.snapshot(p));
            assert(proof.status==Status::Conflict && !dsb.elapsed && proof.check==LatchCheck::DSB &&
                proof.checkRegister==0x70B08+0x1000*pipe && proof.checkValue==control &&
                dsb.reads.back()==proof.checkRegister && dsb.writes.empty());
        }
    }
    // PW2 is a physical prerequisite for pipe B/C even if its own well is on.
    for(unsigned pipe=1;pipe<3;++pipe) {
        LatchIO noShared(p);noShared.moveToPipe(pipe);noShared.regs[0x45404]&=~4U;
        assert(latch(noShared,noShared.snapshot(p)).status==Status::Power);
    }
    for(auto pipe:{3U,UINT32_MAX}) {
        DisplayIO invalid(false);assert(program(invalid,p,false,false,pipe).status==Status::Input && invalid.writes.empty() && invalid.reads.empty());
        LatchIO invalidLatch(p);invalidLatch.reads.clear();auto s=invalidLatch.snapshot(p);s.pipeIndex=pipe;
        assert(latch(invalidLatch,s).status==Status::Input && invalidLatch.reads.empty());
        assert(!triggerSurfaces(both,6,0,0,pipe).valid);
    }
    // Display13 permits >31 lines. The older donor returns disabled here.
    const auto longLevel=linearLevel(1920,2200,148500000,500,4096);
    assert(longLevel.enable && longLevel.lines==34 && longLevel.blocks==545);
    assert(!linearLevel(1920,2200,148500000,5000,4096).enable);
    assert(linearLevel(64,100,10000000,3,100).blocks==3); // Both byte counts below one block select method2.
    assert(!linearLevel(0,2200,148500000,3,2008).enable);
    assert(!linearLevel(1920,1900,148500000,3,2008).enable);
    assert(!linearLevel(1920,2200,0,3,2008).enable);
    // All supported arithmetic remains bounded even at maximum PCODE block time.
    assert(!linearLevel(16384,16384,2000000000ULL,65793,4096).enable);
    puts("AlderLakeN WM fixtures passed: live PCODE protocol/errors, latency sanitization, Display13 RGB/cursor/SAGV equations, nine-register programming, frame/live-surface latch, bounded failures, and DDB/255-line bounds");
}
