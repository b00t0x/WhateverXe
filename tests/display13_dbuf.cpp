#include "kern_display13_dbuf.hpp"
#include <assert.h>
#include <map>
#include <stdio.h>
#include <vector>
using namespace Display13DBUF;
struct IO {
    std::map<uint32_t,uint32_t> r {{0x45404,0xFC0F},{0x70008,0xC0000000},{0x70180,0x84000008},
        {0x4438C,0x1C000700},{0x7027C,0x07FF0000},{0x7017C,0}};
    unsigned writes {},delays {};uint32_t reject {~0U};bool stalled {};
    std::vector<uint32_t> reads;
    std::vector<std::pair<uint32_t,uint32_t>> writeTrace;
    IO() {
        for(unsigned i=0;i<4;++i)r[regs[i]]=0x40C000|((i<2)?Request|On:0);
        r[0x70080]=0;
        for(unsigned pipe=0;pipe<3;++pipe) {
            if(pipe)r[0x70008+pipe*0x1000]=0;
            r[0x60400+pipe*0x1000]=0;
            if(pipe)r[0x7027C+pipe*0x1000]=r[0x7017C+pipe*0x1000]=0;
            r[0x70080+pipe*0x1000]=0;
            for(unsigned d=0;d<3;++d)r[0x70B08+pipe*0x1000+d*0x100]=0;
            for(unsigned plane=pipe ? 0 : 1;plane<5;++plane)r[0x70180+pipe*0x1000+plane*0x100]=0;
        }
    }
    bool readable(uint32_t reg) {return r.count(reg);}
    uint32_t read(uint32_t reg) {assert(r.count(reg));reads.push_back(reg);return r[reg];}
    void write(uint32_t reg,uint32_t value) {
        ++writes;writeTrace.emplace_back(reg,value);assert(r.count(reg));if(reg==reject)return;
        r[reg]=stalled ? (value&~On)|(r[reg]&On) : (value&~On)|((value&Request)>>1);
    }
    void delayUs(unsigned us) {delays+=us;}
};
int main() {
    for(unsigned failure=0;failure<5;++failure) {
        IO io;State state;Display13Transcoder::Bindings dsi;dsi.dsi0Pipe=0;
        io.r[0x70008]=0;io.r[0x7B008]=0xC0000000;io.r[0x6B400]=0x80030006;
        assert(begin(io,state,0,dsi)==Status::Complete && !io.writes);
        assert(begin(io,state)==Status::Topology); // cannot erase the active mapping
        assert(singlePipeLayout(io,state).active && pipeLayout(io,state,0).active);
        if(failure==0)assert(stopPipe(io,state,0)==Status::Conflict && state.pipe[0].consumers);
        if(failure==1)assert(stop(io,state)==Status::Conflict && state.pipe[0].consumers);
        if(failure==2){io.r[0x6B400]|=5U<<12;assert(singlePipeLayout(io,state).status==Status::Topology);}
        if(failure==3){io.r[0x7B008]=0x80000000;assert(pipeLayout(io,state,0).status==Status::Conflict);}
        if(failure==4){io.r[0x7B008]=0;io.r[0x6B400]=0;io.r[0x70180]=0;
            assert(stopPipe(io,state,0)==Status::Complete && !state.pipe[0].consumers);}
    }
    // Independent Linux/MBUS examples: absolute ownership and relative plane
    // registers intentionally differ on B, while A remains byte-identical.
    State address;
    const Range globalA {40,2048},globalB {2088,4096};
    assert(hardwareRaw(address,0,globalA)==0x07FF0028);
    assert(hardwareRaw(address,1,globalB)==0x07FF0028);
    assert(hardwareRaw(address,1,{2048,2088})==0x00270000);
    assert(!overlap(globalA,globalB));
    const auto back=hardwareRange(address,1,0x07FF0028);
    assert(back.start==2088 && back.end==4096 && slices(back)==12);
    assert(hardwareRange(address,1,0x00270000).start==2048);
    assert(!valid(hardwareRange(address,1,0x0FFF0828))); // Old absolute MMIO is invalid.
    assert(!valid(hardwareRange(address,1,0xF7FF0028))); // Reserved bits cannot alias a valid range.
    assert(hardwareRaw(address,1,{40,2048})==UINT32_MAX); // Cannot use A's absolute owner for B.
    assert(hardwareRaw(address,1,{2048,2049})==UINT32_MAX); // HW zero means disabled.
    assert(hardwareRaw(address,1,{})==0 && !hardwareRange(address,1,0).end);
    assert(hardwareRaw(address,3,globalB)==UINT32_MAX);
    for(uint32_t start=0;start<2048;start+=17)for(uint32_t end=start+2;end<=2048;end+=31) {
        assert(hardwareRaw(address,0,{start,end})==((end-1)<<16|start));
        const auto absolute=toAbsolute(address,1,{start,end});
        assert(absolute.start==start+2048 && absolute.end==end+2048);
        assert(hardwareRaw(address,1,absolute)==((end-1)<<16|start));
    }
    const auto splitB=plannedLayout(7,1),splitC=plannedLayout(7,2);
    assert(hardwareRaw(address,1,splitB.primary)==0x03FF0028);
    assert(hardwareRaw(address,2,splitC.primary)==0x07FF0428);
    assert(hardwareRaw(address,2,splitC.cursor)==0x04270400);
    address.joined=true;
    assert(hardwareRaw(address,0,{0,4096})==0x0FFF0000);
    assert(hardwareRaw(address,1,globalB)==UINT32_MAX);

    // WM admission must use the actual pipe's DBUF ownership. Pipe B's
    // S2/S3 primary and cursor are valid, but never belong to pipe A.
    State split;
    for(unsigned pipe=0;pipe<2;++pipe) {
        const auto layout=plannedLayout(3,pipe);
        assert(!(slices(layout.primary)&~allowed(split,pipe)));
        assert(!(slices(layout.cursor)&~allowed(split,pipe)));
        assert(slices(layout.primary)&~allowed(split,1-pipe));
    }
    split.joined=true;
    assert(allowed(split,0)==15 && !allowed(split,1) && !allowed(split,2) && !allowed(split,3));

    assert(valid(decode(0x0FFF0000)) && decode(0x0FFF0000).end==4096 && slices(decode(0x0FFF0000))==15);
    IO io;State s;assert(begin(io,s)==Status::Complete && s.consumers==3 && !io.writes);
    const auto layout=singlePipeLayout(io,s);
    assert(layout.status==Status::Complete && layout.active && layout.primary.start==40 && layout.primary.end==2048 &&
        layout.cursor.start==0 && layout.cursor.end==40 && !io.writes);
    IO stopped;State st;assert(begin(stopped,st)==Status::Complete);stopped.r[0x70008]=0;
    assert(singlePipeLayout(stopped,st).status==Status::Complete && !singlePipeLayout(stopped,st).active && !stopped.writes);
    stopped.r[0x70008]=Request;assert(singlePipeLayout(stopped,st).status==Status::Conflict && !stopped.writes);
    assert(tracker(io,s)==Status::Complete);
    for(auto reg:regs)assert((io.r[reg]&MinTracker)==0x10000 && (io.r[reg]&0xF80000)==0x400000);
    assert(power(io,s,7,3)==Status::Complete && s.owned==4);
    assert(power(io,s,3,3)==Status::Complete && !s.owned && s.requested==3);
    assert(io.delays==20); // Full 10us settling for acquisition and release.
    assert(power(io,s,0,0)==Status::Conflict && s.requested==3); // Cannot underreport a tracked active consumer.
    IO joined;joined.r[0x4438C]=0xC0000700;joined.r[0x7027C]=0x0FFF0000;
    for(auto reg:regs)joined.r[reg]|=Request|On;
    State j;assert(begin(joined,j)==Status::Complete && j.consumers==15);
    assert(singlePipeLayout(joined,j).status==Status::Topology && !joined.writes);
    assert(tracker(joined,j)==Status::Complete && (joined.r[regs[0]]&MinTracker)==0x30000);
    IO wrong;wrong.r[0x7027C]=0x0FFF0000;State w;assert(begin(wrong,w)==Status::Topology && !wrong.writes);
    IO overlapIO;overlapIO.r[0x7017C]=0x003F0020;State o;assert(begin(overlapIO,o)==Status::Range);
    IO off;off.r[0x45404]=3;State p;assert(begin(off,p)==Status::Power && !off.writes);
    IO failure;State f;assert(begin(failure,f)==Status::Complete);failure.reject=regs[0];
    assert(tracker(failure,f)==Status::Readback && f.faulted);
    IO timeout;State t;assert(begin(timeout,t)==Status::Complete);timeout.stalled=true;
    assert(power(timeout,t,7,3)==Status::Timeout && timeout.delays==10 && t.owned==4);
    IO busy;State b;assert(begin(busy,b)==Status::Complete);
    assert(power(busy,b,1,3)==Status::Conflict && !busy.writes);
    IO extra;extra.r[0x70280]=Request;State x;
    assert(begin(extra,x)==Status::Topology && !extra.writes);
    IO extraPipe;extraPipe.r[0x71008]=Request;State xp;
    assert(begin(extraPipe,xp)==Status::Topology && !extraPipe.writes);
    IO unsafeCursor;unsafeCursor.r[0x70080]=7;State uc;
    assert(begin(unsafeCursor,uc)==Status::Topology && !unsafeCursor.writes);
    IO preflight;State pf;assert(begin(preflight,pf)==Status::Complete);
    assert(power(preflight,pf,15,3)==Status::Complete);
    preflight.r[regs[3]]&=~Request;const auto writes=preflight.writes;
    assert(power(preflight,pf,3,3)==Status::Conflict && preflight.writes==writes);
    IO update;State u;assert(begin(update,u)==Status::Complete);
    assert(preparePrimary(update,u,40,2047)==Status::Complete && u.updating);
    update.r[0x7027C]=0x07FF0028; // Native body writes inclusive end / 12-bit start.
    assert(finishPrimary(update,u)==Status::Complete && !u.updating && u.pipe[0].primary.start==40 && u.unretired==3);
    assert(power(update,u,1,1)==Status::Conflict); // Old scanout has no retirement ACK.
    IO maxRange;maxRange.r[0x4438C]=0xC0000700;
    for(auto reg:regs)maxRange.r[reg]|=Request|On;
    State mr;assert(begin(maxRange,mr)==Status::Complete);
    assert(preparePrimary(maxRange,mr,0,4095)==Status::Complete);
    maxRange.r[0x7027C]=0x0FFF0000;
    assert(finishPrimary(maxRange,mr)==Status::Complete && mr.pipe[0].primary.end==4096 && mr.consumers==15);
    IO truncated;truncated.r[0x4438C]=0xC0000700;
    for(auto reg:regs)truncated.r[reg]|=Request|On;
    State tr;assert(begin(truncated,tr)==Status::Complete);
    assert(preparePrimary(truncated,tr,0,4095)==Status::Complete);
    assert(finishPrimary(truncated,tr)==Status::Readback && tr.faulted);
    IO collision;collision.r[0x7027C]=0x07FF0028;collision.r[0x7017C]=0x00270000;State co;
    assert(begin(collision,co)==Status::Complete);
    assert(preparePrimary(collision,co,0,2047)==Status::Range && !collision.writes);
    IO reentrant;State re;assert(begin(reentrant,re)==Status::Complete);
    assert(preparePrimary(reentrant,re,40,2047)==Status::Complete);
    assert(preparePrimary(reentrant,re,50,2047)==Status::Conflict && !reentrant.writes);
    IO disabled;State dis;assert(begin(disabled,dis)==Status::Complete);
    assert(preparePrimary(disabled,dis,0,0)==Status::Range && !disabled.writes);
    // A borrowed S0 remains; an S1 acquired for a previous allocation is
    // releasable only after a qualified forward-frame latch of the new range.
    IO retirement;retirement.r[0x7027C]=0x03FF0000;retirement.r[regs[1]]&=~(Request|On);
    State rt;assert(begin(retirement,rt)==Status::Complete && rt.initialRequests==1);
    assert(preparePrimary(retirement,rt,0,2047)==Status::Complete && rt.owned==2);
    retirement.r[0x7027C]=0x07FF0000;assert(finishPrimary(retirement,rt)==Status::Complete);
    assert(preparePrimary(retirement,rt,0,1023)==Status::Complete);
    retirement.r[0x7027C]=0x03FF0000;assert(finishPrimary(retirement,rt)==Status::Complete && rt.unretired==3);
    auto noProgress=rt;
    assert(retire(retirement,noProgress,0x03FF0000,0,10,10)==Status::Conflict && noProgress.unretired==3 && rt.owned==2);
    auto wrongRange=rt;
    assert(retire(retirement,wrongRange,0x07FF0000,0,10,11)==Status::Conflict && wrongRange.unretired==3);
    assert(retire(retirement,rt,0x03FF0000,0,0xFFFFFFFE,0)==Status::Complete && !rt.unretired && !rt.owned && rt.requested==1);
    IO cursor;State cs;assert(begin(cursor,cs)==Status::Complete);
    assert(prepareCursor(cursor,cs)==Status::Range && !cursor.writes); // GOP primary overlaps native cursor reservation.
    IO cursorReady;cursorReady.r[0x7027C]=0x07FF0028;State cr;
    assert(begin(cursorReady,cr)==Status::Complete && prepareCursor(cursorReady,cr)==Status::Complete && cr.pipe[0].pendingCursor);
    cursorReady.r[0x7017C]=39U<<16;
    assert(finishCursor(cursorReady,cr)==Status::Complete && cr.pipe[0].cursor.end==40 && cr.pipe[0].cursor.start==0 && cr.unretired==3);
    assert(preparePrimary(cursorReady,cr,0,2047)==Status::Range); // Cannot reclaim a tracked cursor allocation.
    IO cursorActive;cursorActive.r[0x7027C]=0x07FF0050;cursorActive.r[0x7017C]=0x004F0028;cursorActive.r[0x70080]=7;
    State ca;assert(begin(cursorActive,ca)==Status::Complete);
    assert(prepareCursor(cursorActive,ca)==Status::Conflict && !cursorActive.writes); // Active relocation needs its own proof.
    IO cursorSame;cursorSame.r[0x7027C]=0x07FF0028;cursorSame.r[0x7017C]=39U<<16;cursorSame.r[0x70080]=7;
    State same;assert(begin(cursorSame,same)==Status::Complete && prepareCursor(cursorSame,same)==Status::Complete);
    assert(finishCursor(cursorSame,same)==Status::Complete);
    IO cursorFail;cursorFail.r[0x7027C]=0x07FF0028;State cf;
    assert(begin(cursorFail,cf)==Status::Complete && prepareCursor(cursorFail,cf)==Status::Complete);
    assert(finishCursor(cursorFail,cf)==Status::Readback && cf.updating && cf.faulted);
    IO cursorPower;cursorPower.r[0x7027C]=0x07FF0028;State cp;
    assert(begin(cursorPower,cp)==Status::Complete);cursorPower.r[0x45404]=~0U;
    assert(prepareCursor(cursorPower,cp)==Status::Power && !cursorPower.writes);
    IO primaryPower;State pp;assert(begin(primaryPower,pp)==Status::Complete);primaryPower.r[0x45404]=0;
    assert(preparePrimary(primaryPower,pp,40,2047)==Status::Power && !primaryPower.writes);
    IO allocator;State al;assert(begin(allocator,al)==Status::Complete);
    assert(preparePrimary(allocator,al,layout.primary.start,layout.primary.end-1)==Status::Complete);
    allocator.r[0x7027C]=0x07FF0028;assert(finishPrimary(allocator,al)==Status::Complete && al.unretired==3);
    assert(prepareCursor(allocator,al)==Status::Complete);allocator.r[0x7017C]=39U<<16;
    assert(finishCursor(allocator,al)==Status::Complete && al.unretired==3 && al.consumers==3);
    assert(singlePipeLayout(allocator,al).status==Status::Complete);
    allocator.r[0x71008]=Request|On;assert(singlePipeLayout(allocator,al).status==Status::Topology);
    allocator.r[0x71008]=0;allocator.r[0x70280]=Request;
    assert(singlePipeLayout(allocator,al).status==Status::Topology);
    allocator.r[0x70280]=0;allocator.r[0x7027C]=0x07FE0028;
    assert(singlePipeLayout(allocator,al).status==Status::Conflict);
    IO dead;State de;assert(begin(dead,de)==Status::Complete);dead.r[0x45404]=~0U;
    assert(tracker(dead,de)==Status::Power && !dead.writes);
    IO deadPower;State dp;assert(begin(deadPower,dp)==Status::Complete);deadPower.r[0x45404]=~0U;
    assert(power(deadPower,dp,3,3)==Status::Power && !deadPower.writes);
    // Stop ACK retires stale consumers even with no running frame counter.
    IO stopping;stopping.r[0x7027C]=0x03FF0000;stopping.r[regs[1]]&=~(Request|On);State ss;
    assert(begin(stopping,ss)==Status::Complete && power(stopping,ss,3,3)==Status::Complete && ss.owned==2);
    ss.pipe[0].unretired=3;syncMasks(ss);stopping.r[0x70008]=0;stopping.r[0x70180]=0;
    assert(stop(stopping,ss)==Status::Complete && !ss.unretired && !ss.consumers && !ss.owned && ss.requested==1);
    assert(resume(stopping,ss)==Status::Complete);
    // Full unplug destroys the last native path before getPG restarts the
    // shared engine. Resuming pipe0 then is unavailable, not a power failure.
    {IO io;State s;assert(begin(io,s)==Status::Complete);
     io.r[0x70008]=io.r[0x70180]=0;
     assert(stop(io,s)==Status::Complete && detach(io,s,0)==Status::Complete && !s.selected);
     assert(resume(io,s)==Status::Unavailable);
     const auto writes=io.writes;
     assert(resumeEngine(io,s)==Status::Complete && !s.selected && !s.consumers && !s.faulted && io.writes==writes);
     assert(s.released[0].valid && bindOffPipe(io,s,0)==Status::Complete && s.selected==1);
     assert(resumeEngine(io,s)==Status::Complete && s.consumers==3);}
    // Empty logical ownership must not hide a live foreign hardware consumer
    // or in-flight/retained obligations. Every failure precedes a DBUF write.
    for(unsigned bad=0;bad<6;++bad){IO io;State s;assert(begin(io,s)==Status::Complete);
     io.r[0x70008]=io.r[0x70180]=0;
     assert(stop(io,s)==Status::Complete && detach(io,s,0)==Status::Complete);
     if(bad==0)io.r[0x71008]=Request|On;
     if(bad==1)io.r[0x71180]=Request;
     if(bad==2)io.r[0x70B08]=1;
     if(bad==3)s.updating=1;
     if(bad==4)s.unretired=1;
     if(bad==5)s.pipe[0].primary={40,2048};
     const auto writes=io.writes;
     assert(resumeEngine(io,s)==Status::Conflict && s.faulted && io.writes==writes);}
    // Global resume is independent of pipe0 identity. Use real API transitions
    // to leave B as the only retained path; its MMIO DDB stays MBUS-relative.
    for(unsigned scenario=0;scenario<4;++scenario) {
        IO io;State s;assert(begin(io,s)==Status::Complete);
        io.r[0x70008]=io.r[0x70180]=0;
        assert(bindOffPipe(io,s,1)==Status::Complete);
        assert(preparePrimary(io,s,2088,4095,1)==Status::Complete);
        io.r[0x7127C]=0x07FF0028;
        assert(finishPrimary(io,s,1)==Status::Complete);
        assert(stop(io,s)==Status::Complete);
        if(scenario!=1)assert(detach(io,s,0)==Status::Complete && s.selected==2);
        if(scenario>=2)io.r[0x45404]&=~(3U<<12);
        if(scenario==3) {s.pipe[1].consumers=12;syncMasks(s);}
        io.reads.clear();const auto writes=io.writes;
        if(scenario==3) {
            assert(resumeEngine(io,s)==Status::Power && s.faulted && io.writes==writes);
        } else {
            assert(resumeEngine(io,s)==Status::Complete && !s.faulted);
            assert(s.consumers==(scenario==0 ? 12U : scenario==1 ? 15U : 0U));
            assert(s.selected==(scenario==1 ? 3U : 2U));
        }
        if(scenario>=2)for(auto reg:io.reads)assert(reg<0x71000 || reg>=0x72000);
        if(scenario==2) {
            io.r[0x45404]|=3U<<12;
            assert(resume(io,s,1)==Status::Complete && s.consumers==12);
        }
    }
    // A bad powered peer must fail before powering slices for a valid pipe0.
    {IO io;State s;assert(begin(io,s)==Status::Complete);
     io.r[0x70008]=io.r[0x70180]=0;
     assert(bindOffPipe(io,s,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,4095,1)==Status::Complete);
     io.r[0x7127C]=0x07FF0028;assert(finishPrimary(io,s,1)==Status::Complete);
     assert(stop(io,s)==Status::Complete);io.r[0x7127C]=0x0FFF0828;
     const auto writes=io.writes;
     assert(resumeEngine(io,s)==Status::Conflict && s.faulted && !s.consumers && io.writes==writes);}
    // Measured native DDB layout survives stop and two cold reconnects.
    // Native enablePlane first writes 642 primary blocks, then the optimized
    // layout grows to 2008; old S1 consumers cannot retire before a frame ACK.
    IO reconnect;reconnect.r[0x7027C]=0x07FF0028;reconnect.r[0x7017C]=39U<<16;
    State rc;assert(begin(reconnect,rc)==Status::Complete);
    for(unsigned round=0;round<2;++round) {
        reconnect.r[0x70008]=0;reconnect.r[0x70180]=0;reconnect.r[0x70080]=0;
        assert(stop(reconnect,rc)==Status::Complete && !rc.consumers && !rc.unretired);
        assert(resume(reconnect,rc)==Status::Complete);
        auto cold=singlePipeLayout(reconnect,rc);assert(cold.status==Status::Complete && !cold.active);
        assert(preparePrimary(reconnect,rc,40,681)==Status::Complete);
        reconnect.r[0x7027C]=0x02A90028;assert(finishPrimary(reconnect,rc)==Status::Complete && rc.unretired==3 && rc.consumers==1);
        assert(prepareCursor(reconnect,rc)==Status::Complete && finishCursor(reconnect,rc)==Status::Complete);
        reconnect.r[0x70008]=Request|On;reconnect.r[0x70180]=Request;reconnect.r[0x70080]=7;
        assert(retire(reconnect,rc,0x02A90028,39U<<16,100+round*2,101+round*2)==Status::Complete && !rc.unretired);
        assert(preparePrimary(reconnect,rc,40,2047)==Status::Complete);
        reconnect.r[0x7027C]=0x07FF0028;assert(finishPrimary(reconnect,rc)==Status::Complete);
        assert(retire(reconnect,rc,0x07FF0028,39U<<16,101+round*2,102+round*2)==Status::Complete);
        assert(rc.consumers==3 && !rc.unretired && !rc.updating && !rc.faulted);
    }
    IO busyStop;State bs;assert(begin(busyStop,bs)==Status::Complete);
    busyStop.r[0x70008]=0;busyStop.r[0x70180]=0;busyStop.r[0x70080]=0x27;
    assert(stop(busyStop,bs)==Status::Conflict && !busyStop.writes && bs.consumers==3);
    IO dsbStop;State ds;assert(begin(dsbStop,ds)==Status::Complete);
    dsbStop.r[0x70008]=0;dsbStop.r[0x70180]=0;dsbStop.r[0x70B08]=1;
    assert(stop(dsbStop,ds)==Status::Conflict && !dsbStop.writes);
    {IO hw;State owned;assert(begin(hw,owned)==Status::Complete && bindOffPipe(hw,owned,1)==Status::Complete);
     assert(preparePrimary(hw,owned,2048,2048,1)==Status::Range && !hw.writes);}
    // Independent per-pipe transactions/ACKs share power, never their ranges.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete && s.selected==3);
     const auto b=plannedLayout(s.selected,1);assert(b.primary.start==2088 && b.primary.end==4096 && b.cursor.start==2048);
     assert(preparePrimary(io,s,b.primary.start,b.primary.end-1,1)==Status::Complete);
     {auto old=s;auto hw=io;hw.r[0x7127C]=0x0FFF0828;assert(finishPrimary(hw,old,1)==Status::Readback && old.faulted);}
     assert(s.pipe[1].pendingRaw==0x0FFF0828 && pendingHardwareRaw(s,1)==0x07FF0028);
     io.r[0x7127C]=0x07FF0028U;assert(finishPrimary(io,s,1)==Status::Complete && s.pipe[1].unretired==12 && s.pipe[0].consumers==3);
     assert(prepareCursor(io,s,1)==Status::Complete && preparePrimary(io,s,40,2047)==Status::Complete && s.updating==3);
     io.r[0x7027C]=0x07FF0028;assert(finishPrimary(io,s)==Status::Complete && s.updating==2);
     assert(retire(io,s,0x07FF0028,0,10,11)==Status::Complete && s.pipe[1].unretired==12 && s.updating==2 && s.owned==12);
     io.r[0x7117C]=0x00270000U;assert(finishCursor(io,s,1)==Status::Complete && !s.updating);
     assert(retire(io,s,0x07FF0028U,0x00270000U,20,21,1)==Status::Complete && !s.unretired && s.consumers==15);
     io.r[0x70008]=io.r[0x70180]=0;io.reads.clear();
     assert(stopPipe(io,s,0)==Status::Complete && s.consumers==12 && s.owned==12 && s.requested==15);
     for(auto reg:io.reads)assert(!(reg>=0x71000 && reg<0x72000)); // Do not stop/read B's live bank.
     io.r[0x71008]=io.r[0x71180]=0;
     assert(stopPipe(io,s,1)==Status::Complete && !s.consumers && !s.owned && s.requested==3);
     assert(resume(io,s)==Status::Complete && resume(io,s,1)==Status::Complete && s.consumers==15 && s.owned==12);}
    // Read-only A/B plans accept another qualified live primary, reject a
    // foreign bank/plane, and never retire old ranges or move an active cursor.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete);
     auto b=pipeLayout(io,s,1);assert(b.status==Status::Complete && !b.active && b.primary.start==2088 && b.cursor.start==2048);
     assert(preparePrimary(io,s,b.primary.start,b.primary.end-1,1)==Status::Complete);
     io.r[0x7127C]=0x07FF0028U;assert(finishPrimary(io,s,1)==Status::Complete);
     assert(prepareCursor(io,s,1)==Status::Complete);io.r[0x7117C]=0x00270000U;assert(finishCursor(io,s,1)==Status::Complete);
     io.r[0x71008]=Request|On;io.r[0x71180]=Request;io.r[0x71080]=0x27;
     const auto writes=io.writes;const auto old=s.unretired;
     assert(pipeLayout(io,s,0).status==Status::Complete && pipeLayout(io,s,1).status==Status::Complete && pipeLayout(io,s,1).active);
     assert(io.writes==writes && s.unretired==old);
     auto foreign=io;foreign.r[0x72008]=Request|On;assert(pipeLayout(foreign,s,0).status==Status::Topology && foreign.writes==writes);
     auto plane=io;plane.r[0x71280]=Request;assert(pipeLayout(plane,s,0).status==Status::Topology);
     auto shadow=io;shadow.r[0x7127C]=0;assert(pipeLayout(shadow,s,1).status==Status::Conflict);
     auto pending=s;pending.updating=2;assert(pipeLayout(io,pending,1).status==Status::Conflict);
     pending.updating=1;pending.pipe[0].pendingRaw=encoded(pending.pipe[0].primary);
     assert(pipeLayout(io,pending,1).status==Status::Complete); // Other bank's pending range remains reserved.
     auto unpowered=io;unpowered.r[0x45404]&=~(1U<<12);unpowered.reads.clear();assert(pipeLayout(unpowered,s,1).status==Status::Power);
     assert(unpowered.reads==std::vector<uint32_t>{0x45404});
     auto third=s;auto hardware=io;assert(bindOffPipe(hardware,third,2)==Status::Complete);
     assert(pipeLayout(hardware,third,2).status==Status::Range); // B's existing full S2/3 cannot be stolen for C.
     assert(singlePipeLayout(io,s).status==Status::Topology);}
    // Two owners can use different byte ranges of the same physical slice.
    // Stopping C cannot clear B's S2 request, even though the masks are equal.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete && bindOffPipe(io,s,2)==Status::Complete);
     assert(preparePrimary(io,s,2088,2499,1)==Status::Complete);io.r[0x7127C]=0x01C30028U;
     assert(finishPrimary(io,s,1)==Status::Complete && retire(io,s,io.r[0x7127C],0,10,11,1)==Status::Complete);
     assert(preparePrimary(io,s,2600,2899,2)==Status::Complete);io.r[0x7227C]=0x03530228U;
     assert(finishPrimary(io,s,2)==Status::Complete && retire(io,s,io.r[0x7227C],0,20,21,2)==Status::Complete);
     assert(s.owned==4 && s.pipe[1].consumers==4 && s.pipe[2].consumers==4);
     assert(stopPipe(io,s,2)==Status::Complete && s.owned==4 && s.requested==7 && s.pipe[1].consumers==4);
     assert(stopPipe(io,s,1)==Status::Complete && !s.owned && s.requested==3 && s.consumers==3);
     assert(detach(io,s,2)==Status::Complete && s.selected==3 && !s.pipe[2].primary.end);}
    // Three-connector board capacity reserves B/C before their arrival.
    // Both insertion orders and every removal/re-add retain the same ranges;
    // native writes remain MBUS-relative and need each owner's actual ACK.
    for(unsigned first:{1U,2U}) {
        IO io;State s;assert(begin(io,s,7)==Status::Complete);
        const auto assign=[&](unsigned p) {
            assert(bindOffPipe(io,s,p)==Status::Complete);
            const auto l=pipeLayout(io,s,p);assert(l.status==Status::Complete);
            assert(preparePrimary(io,s,l.primary.start,l.primary.end-1,p)==Status::Complete);
            io.r[0x7027C+0x1000*p]=pendingHardwareRaw(s,p);
            assert(finishPrimary(io,s,p)==Status::Complete && s.pipe[p].unretired);
            assert(prepareCursor(io,s,p)==Status::Complete);
            io.r[0x7017C+0x1000*p]=pendingHardwareRaw(s,p);
            assert(finishCursor(io,s,p)==Status::Complete);
            assert(retire(io,s,io.r[0x7027C+0x1000*p],io.r[0x7017C+0x1000*p],30,31,p)==Status::Complete);
            assert(!s.pipe[p].unretired);
        };
        assign(first);assign(3-first);assert(s.selected==7);
        assert(s.pipe[1].primary.start==2088 && s.pipe[1].primary.end==3072);
        assert(s.pipe[2].primary.start==3112 && s.pipe[2].primary.end==4096);
        assert(io.r[0x7127C]==0x03FF0028 && io.r[0x7227C]==0x07FF0428);
        for(unsigned removed=1;removed<3;++removed) {
            const auto peer=3-removed,raw=io.r[0x7027C+0x1000*peer];
            assert(stopPipe(io,s,removed)==Status::Complete && detach(io,s,removed)==Status::Complete);
            const auto l=pipeLayout(io,s,peer);assert(l.status==Status::Complete);
            assert(io.r[0x7027C+0x1000*peer]==raw);
            assign(removed);assert(s.selected==7 && !s.faulted);
        }
        assert(begin(io,s,0)==Status::Topology && s.capacity==7);
    }
    // After explicit detach, adopt only the prior stopped shadow or an
    // actual empty bank. An unknown or changed allocation remains foreign.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,4095,1)==Status::Complete);io.r[0x7127C]=0x07FF0028U;
     assert(finishPrimary(io,s,1)==Status::Complete && stopPipe(io,s,1)==Status::Complete && detach(io,s,1)==Status::Complete);
     assert(s.released[1].valid && !(s.selected&2) && s.pipe[0].consumers==3);
     const auto before=io.writes;auto changed=io;auto copy=s;changed.r[0x7127C]^=1;
     assert(bindOffPipe(changed,copy,1)==Status::Conflict && changed.writes==before);
     assert(bindOffPipe(io,s,1)==Status::Complete && s.pipe[1].primary.start==2088 && !s.pipe[1].consumers && !s.released[1].valid && io.writes==before);
     assert(stopPipe(io,s,1)==Status::Complete && detach(io,s,1)==Status::Complete);
     io.r[0x7127C]=io.r[0x7117C]=0;assert(bindOffPipe(io,s,1)==Status::Complete && !s.pipe[1].primary.end);}
    // Reserve a pending range before native MMIO and retain its old envelope
    // after finish. A shadow read or another pipe's ACK cannot make it free.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete && bindOffPipe(io,s,2)==Status::Complete);
     assert(preparePrimary(io,s,2088,2999,1)==Status::Complete);
     {auto other=s;auto hw=io;assert(preparePrimary(hw,other,2800,3199,2)==Status::Range && hw.writes==io.writes);}
     io.r[0x7127C]=0x03B70028U;assert(finishPrimary(io,s,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,2499,1)==Status::Complete);io.r[0x7127C]=0x01C30028U;
     assert(finishPrimary(io,s,1)==Status::Complete && s.pipe[1].heldPrimary.end==3000);
     {auto other=s;auto hw=io;assert(preparePrimary(hw,other,2600,2899,2)==Status::Range && hw.writes==io.writes);}
     assert(retire(io,s,io.r[0x7127C],0,10,11,1)==Status::Complete && !s.pipe[1].heldPrimary.end);
     assert(preparePrimary(io,s,2600,2899,2)==Status::Complete);}
    // A's ACK while B has old S3 consumers does not release S3. Only B's
    // own qualified ACK can do that, and a failed release restores its hold.
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,4095,1)==Status::Complete);io.r[0x7127C]=0x07FF0028U;
     assert(finishPrimary(io,s,1)==Status::Complete && retire(io,s,io.r[0x7127C],0,10,11,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,2999,1)==Status::Complete);io.r[0x7127C]=0x03B70028U;
     assert(finishPrimary(io,s,1)==Status::Complete && s.pipe[1].unretired==12);
     assert(preparePrimary(io,s,40,2047)==Status::Complete);io.r[0x7027C]=0x07FF0028;assert(finishPrimary(io,s)==Status::Complete);
     assert(retire(io,s,0x07FF0028,0,20,21)==Status::Complete && s.pipe[1].unretired==12 && s.owned==12);
     io.reject=regs[3];const auto old=s.pipe[1];
     assert(retire(io,s,io.r[0x7127C],0,30,31,1)==Status::Readback && s.faulted && s.pipe[1].unretired==old.unretired &&
        s.pipe[1].heldPrimary.end==old.heldPrimary.end && s.pipe[0].consumers==3);}
    {IO io;State s;assert(begin(io,s)==Status::Complete && bindOffPipe(io,s,1)==Status::Complete);
     assert(preparePrimary(io,s,2088,4095,1)==Status::Complete);io.r[0x7127C]=0x07FF0028U;assert(finishPrimary(io,s,1)==Status::Complete);
     const auto writes=io.writes;assert(power(io,s,3,3)==Status::Conflict && io.writes==writes && s.pipe[1].consumers==12);}
    // Failed fresh-bank admission cannot erase stale DDB or an active owner.
    {IO io;State s;assert(begin(io,s)==Status::Complete);io.r[0x7127C]=0x03FF0000;
     assert(bindOffPipe(io,s,1)==Status::Conflict && s.selected==1 && !io.writes && io.r[0x7127C]==0x03FF0000);}
    {IO io;State s;assert(begin(io,s)==Status::Complete);io.r[0x71B08]=1;
     assert(bindOffPipe(io,s,1)==Status::Conflict && s.selected==1 && !io.writes);}
    for(unsigned mask=1;mask<=7;++mask)for(unsigned p=0;p<3;++p)if(mask&(1U<<p)) {
        const auto a=plannedLayout(mask,p);assert(a.status==Status::Complete && a.cursor.end-a.cursor.start==40 && valid(a.primary) && !overlap(a.primary,a.cursor));
        assert((slices(a.primary)|slices(a.cursor))==(p==0?3U:p==1 && (mask&4)?4U:p==2 && (mask&2)?8U:12U));
        for(unsigned q=p+1;q<3;++q)if(mask&(1U<<q)) {const auto b=plannedLayout(mask,q);assert(!overlap(a.primary,b.primary) && !overlap(a.primary,b.cursor) && !overlap(a.cursor,b.primary) && !overlap(a.cursor,b.cursor));}
    }
    assert(plannedLayout(0,0).status==Status::Topology && plannedLayout(8,0).status==Status::Topology && plannedLayout(1,3).status==Status::Topology);
    puts("AlderLakeN DBUF fixtures passed: absolute ownership / MBUS-relative native and MMIO, independent Linux examples, A register invariance, 12-bit ranges, boot topology, tracker preservation, primary/cursor allocation, slice lifetime, native update transaction, retirement conditions, slice release and failures");
}
