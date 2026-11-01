// Display 13 DBUF layout and power requests. Caller holds the display owner
// lock and proves live plane consumers before shrinking an owned slice mask.
#ifndef KERN_DISPLAY13_DBUF_HPP
#define KERN_DISPLAY13_DBUF_HPP
#include <stdint.h>
#include "kern_display13_fb_irq.hpp"
namespace Display13DBUF {
constexpr uint32_t Request=1U<<31,On=1U<<30,MinTracker=7U<<16;
constexpr uint32_t regs[4] {0x45008,0x44FE8,0x44300,0x44304};
enum class Status : uint32_t {Unavailable,Complete,Power,Topology,Range,Conflict,Readback,Timeout,Faulted};
struct Range {uint32_t start {},end {};};
inline Range decode(uint32_t raw) {
    const auto end=(raw>>16)&0xFFF;
    return {raw&0xFFF,end ? end+1 : 0}; // Hardware end is inclusive; zero disables an entry.
}
inline bool valid(Range r) {return r.end && r.start<r.end && r.end<=4096;}
inline bool overlap(Range a,Range b) {return a.end && b.end && a.start<b.end && b.start<a.end;}
inline uint32_t slices(Range r) {
    if(!valid(r))return 0;
    uint32_t mask=0;for(unsigned s=r.start/1024;s<=(r.end-1)/1024;++s)mask|=1U<<s;
    return mask;
}
inline uint32_t encoded(Range r) {return r.end ? ((r.end-1)<<16)|r.start : 0;}
inline Range cover(Range a,Range b) {
    if(!a.end)return b;
    if(!b.end)return a;
    return {a.start<b.start ? a.start : b.start,a.end>b.end ? a.end : b.end};
}
struct PipeAllocation {
    Range primary,cursor,heldPrimary,heldCursor;
    uint32_t consumers {},unretired {},pendingRaw {}; // pendingRaw is ABSOLUTE, never a direct MMIO value.
    bool pendingCursor {};
};
struct State {
    Display13Transcoder::Bindings transcoders;
    bool begun {},faulted {},joined {};
    uint32_t initial[4] {},raw[4] {},initialRequests {},requested {},powered {},owned {},consumers {},mbus {};
    PipeAllocation pipe[3];
    struct Released {bool valid {};Range primary,cursor;} released[3];
    uint32_t capacity {}; // Optional board capacity: reserve B/C banks before either path attaches.
    uint32_t selected {},updating {},unretired {}; // Updating is a per-pipe transaction mask.
};
inline Status fail(State &s,Status status) {s.faulted=true;return status;}
inline uint32_t tracked(const State &s) {
    uint32_t mask=0;
    for(unsigned p=0;p<3;++p) {
        mask|=s.pipe[p].consumers;
        if(s.updating&(1U<<p))mask|=slices(decode(s.pipe[p].pendingRaw));
    }
    return mask;
}
inline void syncMasks(State &s) {
    s.unretired=0;for(const auto &p:s.pipe)s.unretired|=p.unretired;
    s.consumers=tracked(s)|(s.updating ? s.unretired : 0);
}
inline uint32_t allowed(const State &s,unsigned p) {
    if(p>=3)return 0;
    return s.joined ? (p==0 ? 15U : 0U) : p==0 ? 3U : 12U;
}
// State ranges and pendingRaw use absolute DBUF blocks. Plane registers and
// the native plane ABI use MBUS-relative blocks. In unjoined ADL-P/N, B/C's
// MBUS bank starts at S3 (2048); A and joined A start at zero.
inline uint32_t mbusBase(const State &s,unsigned pipe) {
    return pipe>=3 || (s.joined && pipe) ? UINT32_MAX : !s.joined && pipe ? 2048 : 0;
}
inline Range invalidRange() {return {UINT32_MAX,UINT32_MAX};}
inline Range toHardware(const State &s,unsigned pipe,Range absolute) {
    const auto base=mbusBase(s,pipe),capacity=s.joined ? 4096U : 2048U;
    if(base==UINT32_MAX)return invalidRange();
    if(!absolute.end)return absolute.start ? invalidRange() : Range {};
    if(!valid(absolute) || absolute.start<base || absolute.end>base+capacity)return invalidRange();
    const Range relative {absolute.start-base,absolute.end-base};
    return encoded(relative) ? relative : invalidRange();
}
inline Range toAbsolute(const State &s,unsigned pipe,Range relative) {
    const auto base=mbusBase(s,pipe),capacity=s.joined ? 4096U : 2048U;
    if(base==UINT32_MAX)return invalidRange();
    if(!relative.end)return relative.start ? invalidRange() : Range {};
    if(!valid(relative) || relative.end>capacity || !encoded(relative))return invalidRange();
    return {relative.start+base,relative.end+base};
}
inline uint32_t hardwareRaw(const State &s,unsigned pipe,Range absolute) {
    const auto relative=toHardware(s,pipe,absolute);
    return relative.start==UINT32_MAX ? UINT32_MAX : encoded(relative);
}
inline Range hardwareRange(const State &s,unsigned pipe,uint32_t raw) {
    if(raw==UINT32_MAX || (raw && raw!=encoded(decode(raw))))return invalidRange();
    return toAbsolute(s,pipe,decode(raw));
}
inline uint32_t pendingHardwareRaw(const State &s,unsigned pipe) {
    return pipe<3 ? hardwareRaw(s,pipe,decode(s.pipe[pipe].pendingRaw)) : UINT32_MAX;
}
template<class IO> bool hardwareMatches(IO &io,const State &s,unsigned pipe,Range primary,Range cursor) {
    const auto p=hardwareRaw(s,pipe,primary),c=hardwareRaw(s,pipe,cursor);
    return p!=UINT32_MAX && c!=UINT32_MAX &&
        io.read(0x7027C+0x1000*pipe)==p && io.read(0x7017C+0x1000*pipe)==c;
}
// Hold old byte ranges until this pipe's ACK, not just their slice bits.
// Even a stopped, bound path retains its layout until explicit detach.
inline bool overlapsOther(const State &s,unsigned owner,Range next) {
    for(unsigned p=0;p<3;++p)if(p!=owner && (s.selected&(1U<<p))) {
        const auto &a=s.pipe[p];
        if(overlap(next,a.primary) || overlap(next,a.cursor) || overlap(next,a.heldPrimary) || overlap(next,a.heldCursor) ||
           ((s.updating&(1U<<p)) && overlap(next,decode(a.pendingRaw))))return true;
    }
    return false;
}
struct Layout {Status status {Status::Unavailable};Range primary,cursor;bool active {};};
struct AllocationPlan {Status status {Status::Topology};Range primary,cursor;};
// ADLP allowed DBUF table: A uses S0/1; B/C share S2/3. Split that bank
// when both paths are selected; native programming/ACK must establish it.
inline AllocationPlan plannedLayout(uint32_t pipes,unsigned p) {
    AllocationPlan r;
    if(!pipes || (pipes&~7U) || p>=3 || !(pipes&(1U<<p)))return r;
    const uint32_t start=p==0 ? 0 : p==2 && (pipes&2) ? 3072 : 2048;
    const uint32_t end=p==0 ? 2048 : p==1 && (pipes&4) ? 3072 : 4096;
    r.primary={start+40,end};r.cursor={start,start+40};r.status=Status::Complete;return r;
}
// ADL-P/N unjoined pipe A owns S0/S1. Keep the native 40-block cursor prefix
// and give the only RGB primary the remaining 2008 blocks. Default and optimized
// requests use the same layout while there is only one live pipe/primary; no
// Display 12 intermediate allocation or unconditional slice request is needed.
template<class IO> Layout singlePipeLayout(IO &io,const State &s) {
    Layout r;
    if(s.faulted) {r.status=Status::Faulted;return r;}
    if(!s.begun)return r;
    if(s.updating) {r.status=Status::Conflict;return r;}
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&0x403)!=0x403) {r.status=Status::Power;return r;}
    if(s.joined || io.read(0x4438C)!=s.mbus || (wells&(3U<<16))) {r.status=Status::Topology;return r;}
    for(unsigned pipe=0;pipe<3;++pipe) {
        if(!(wells&(1U<<(2*(5+pipe)))))continue;
        const auto conf=Display13Transcoder::control(io,pipe,s.transcoders);
        if(conf==UINT32_MAX || (pipe && (conf&(Request|On)))) {r.status=Status::Topology;return r;}
        if(!pipe) {
            // Transitional enable/disable must settle before changing DDB.
            if(bool(conf&Request)!=bool(conf&On)) {r.status=Status::Conflict;return r;}
            r.active=conf&On;
        }
        for(unsigned plane=pipe ? 0 : 1;plane<5;++plane) {
            const auto ctrl=io.read(0x70180+pipe*0x1000+plane*0x100);
            if(ctrl==UINT32_MAX || (ctrl&Request)) {r.status=Status::Topology;return r;}
        }
    }
    const auto primary=io.read(0x7027C),cursor=io.read(0x7017C);
    if(primary!=((s.pipe[0].primary.end-1)<<16|s.pipe[0].primary.start) ||
       cursor!=(s.pipe[0].cursor.end ? ((s.pipe[0].cursor.end-1)<<16|s.pipe[0].cursor.start) : 0)) {r.status=Status::Conflict;return r;}
    // The native cursor setup uses this same reservation. Moving a different
    // existing reservation is a separate two-consumer relocation operation.
    if(cursor && cursor!=(39U<<16)) {r.status=Status::Range;return r;}
    r.primary={40,2048};r.cursor={0,40};r.status=Status::Complete;return r;
}
// Qualified multi-pipe layout query. The caller binds each real FB/path and
// proves an initially stopped bank before adding it to selected. This is a
// read-only plan, never a latch/ACK or permission to replace another owner.
template<class IO> Layout pipeLayout(IO &io,const State &s,unsigned index) {
    Layout r;
    if(s.faulted) {r.status=Status::Faulted;return r;}
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return r;
    if(s.updating&(1U<<index)) {r.status=Status::Conflict;return r;}
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || !Display13FBIRQ::powered(wells,index)) {r.status=Status::Power;return r;}
    if(s.transcoders.dsi0Pipe<3 && !Display13FBIRQ::powered(wells,s.transcoders.dsi0Pipe)) {r.status=Status::Power;return r;}
    if(s.joined || (wells&(3U<<16)) || io.read(0x4438C)!=s.mbus) {r.status=Status::Topology;return r;}
    for(unsigned p=0;p<3;++p)if(Display13FBIRQ::powered(wells,p)) {
        const auto conf=Display13Transcoder::control(io,p,s.transcoders);
        if(conf==UINT32_MAX) {r.status=Status::Conflict;return r;}
        if((conf&(Request|On)) && !(s.selected&(1U<<p))) {r.status=Status::Topology;return r;}
        if(p==index) {
            if(bool(conf&Request)!=bool(conf&On)) {r.status=Status::Conflict;return r;}
            r.active=conf&On;
        }
        for(unsigned plane=(s.selected&(1U<<p)) ? 1 : 0;plane<5;++plane) {
            const auto ctl=io.read(0x70180+p*0x1000+plane*0x100);
            if(ctl==UINT32_MAX || (ctl&Request)) {r.status=Status::Topology;return r;}
        }
    }
    const auto bank=index*0x1000;const auto &a=s.pipe[index];
    if(!hardwareMatches(io,s,index,a.primary,a.cursor)) {
        r.status=Status::Conflict;return r;
    }
    const auto planned=plannedLayout(s.capacity ? s.capacity : s.selected,index);
    if(planned.status!=Status::Complete || overlap(planned.primary,planned.cursor) ||
       overlapsOther(s,index,planned.primary) || overlapsOther(s,index,planned.cursor)) {r.status=Status::Range;return r;}
    const auto cursor=io.read(0x70080+bank);
    if(cursor==UINT32_MAX || ((cursor&63) && encoded(a.cursor)!=encoded(planned.cursor))) {r.status=Status::Conflict;return r;}
    r.primary=planned.primary;r.cursor=planned.cursor;r.status=Status::Complete;return r;
}
// Read the measured boot pipe A allocation, not a Linux allocation copied into
// the hardware. All four physical slices exist; a single unjoined A uses S0/1.
template<class IO> Status begin(IO &io,State &s,uint32_t capacity=0,const Display13Transcoder::Bindings &transcoders={}) {
    if(s.faulted)return Status::Faulted;
    if(s.begun)return capacity==s.capacity && transcoders.dsi0Pipe==s.transcoders.dsi0Pipe ? Status::Complete : Status::Topology;
    if(!transcoders.valid() || (transcoders.dsi0Pipe!=UINT32_MAX && transcoders.dsi0Pipe!=0))return Status::Topology;
    s.transcoders=transcoders;
    if(capacity && capacity!=7)return Status::Topology;
    s.capacity=capacity;
    if(!io.readable(0x4438C) || !io.readable(0x7027C) || !io.readable(0x7017C))return Status::Unavailable;
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&0x403)!=0x403)return fail(s,Status::Power);
    if(wells&(3U<<16))return fail(s,Status::Topology); // Pipe D is outside this native owner.
    if((Display13Transcoder::control(io,0,s.transcoders)&0xC0000000)!=0xC0000000 || !(io.read(0x70180)&Request))return fail(s,Status::Topology);
    s.mbus=io.read(0x4438C);s.joined=s.mbus&Request;
    if(s.mbus==UINT32_MAX || (s.joined && (((s.mbus>>26)&7)!=0 || !(s.mbus&On))))return fail(s,Status::Topology);
    const auto primary=io.read(0x7027C),cursor=io.read(0x7017C);
    s.pipe[0].primary=decode(primary);s.pipe[0].cursor=decode(cursor);
    if(primary==UINT32_MAX || cursor==UINT32_MAX || !valid(s.pipe[0].primary) ||
       (s.pipe[0].cursor.end && !valid(s.pipe[0].cursor)) || overlap(s.pipe[0].primary,s.pipe[0].cursor))return fail(s,Status::Range);
    s.consumers=slices(s.pipe[0].primary)|slices(s.pipe[0].cursor);
    const auto allowed=s.joined ? 15U : 3U;
    if(s.consumers&~allowed)return fail(s,Status::Topology);
    // Inspect every control while its pipe well is on. This backend owns only
    // pipe A primary/cursor, so another live plane/pipe cannot be omitted from
    // the allocation or slice lifetime calculation.
    for(unsigned pipe=0;pipe<3;++pipe) {
        if(!(wells&(1U<<(2*(5+pipe)))))continue;
        const auto conf=Display13Transcoder::control(io,pipe,s.transcoders);
        if(conf==UINT32_MAX || (pipe && (conf&Request)))return fail(s,Status::Topology);
        for(unsigned plane=pipe ? 0 : 1;plane<5;++plane) {
            const auto ctl=io.read(0x70180+pipe*0x1000+plane*0x100);
            if(ctl==UINT32_MAX || (ctl&Request))return fail(s,Status::Topology);
        }
    }
    const auto ctl=io.read(0x70180),cursorCtl=io.read(0x70080);
    if(ctl==UINT32_MAX || cursorCtl==UINT32_MAX || ((ctl>>23)&31)!=8 ||
       ((cursorCtl&63) && !valid(s.pipe[0].cursor)))return fail(s,Status::Topology);
    for(unsigned i=0;i<4;++i) {
        if(!io.readable(regs[i]))return Status::Unavailable;
        const auto value=io.read(regs[i]);s.raw[i]=s.initial[i]=value;
        if(value==UINT32_MAX)return fail(s,Status::Readback);
        if(value&Request)s.requested|=1U<<i;
        if(value&On)s.powered|=1U<<i;
    }
    if((s.powered&s.consumers)!=s.consumers || (s.requested&s.consumers)!=s.consumers)return fail(s,Status::Power);
    // Reject a concurrent topology change before the first subsequent write.
    if(io.read(0x45404)!=wells || io.read(0x4438C)!=s.mbus ||
       io.read(0x7027C)!=primary || io.read(0x7017C)!=cursor)return fail(s,Status::Conflict);
    s.initialRequests=s.requested;s.selected=1;s.pipe[0].consumers=s.consumers;s.begun=true;return Status::Complete;
}
// ADL-P/N MDCLK source is CD2XCLK (ratio2); joined MBUS doubles the tracker
// service ratio. Preserve the separate tracker field and unrelated controls.
template<class IO> Status tracker(IO &io,State &s) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun)return Status::Unavailable;
    if(s.updating)return fail(s,Status::Conflict);
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&3)!=3)return fail(s,Status::Power);
    if(io.read(0x4438C)!=s.mbus)return fail(s,Status::Conflict);
    const uint32_t encoded=(s.joined ? 3U : 1U)<<16;
    for(unsigned i=0;i<4;++i) {
        auto value=io.read(regs[i]);if(value==UINT32_MAX)return fail(s,Status::Readback);
        const auto expected=(value&~MinTracker)|encoded;
        if((value&MinTracker)!=encoded)io.write(regs[i],expected);
        value=io.read(regs[i]);s.raw[i]=value;
        if(value==UINT32_MAX || (value&~On)!=(expected&~On))return fail(s,Status::Readback);
    }
    return Status::Complete;
}
// Call before plane/WM commit for the union(old,new), then after that commit
// for the new consumer mask. Never power off a slice still carrying a plane.
template<class IO> Status power(IO &io,State &s,uint32_t wanted,uint32_t consumers) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || (wanted|consumers)&~15U)return Status::Unavailable;
    if((tracked(s)&~consumers) || ((consumers|s.unretired|tracked(s))&~wanted))return fail(s,Status::Conflict);
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&3)!=3 || io.read(0x4438C)!=s.mbus)return fail(s,Status::Power);
    // Predecessor requests are retained until explicit resource ownership
    // transfer. A power enable does not itself authorize their teardown.
    wanted|=s.initialRequests;
    // Preflight all requests before changing any of them. An unexpected owner
    // change in a later slice must not first power off an earlier slice.
    uint32_t before[4] {};
    for(unsigned i=0;i<4;++i) {
        before[i]=io.read(regs[i]);
        if(before[i]==UINT32_MAX)return fail(s,Status::Readback);
        if((s.owned&(1U<<i)) && !(before[i]&Request))return fail(s,Status::Conflict);
    }
    for(unsigned i=0;i<4;++i) {
        auto value=io.read(regs[i]);if(value==UINT32_MAX)return fail(s,Status::Readback);
        const auto bit=1U<<i;const bool enable=wanted&bit;
        if((value&~On)!=(before[i]&~On))return fail(s,Status::Conflict);
        bool changed=false;
        if(enable && !(value&Request)) {
            io.write(regs[i],value|Request);s.owned|=bit;changed=true;
        } else if(!enable && (s.owned&bit)) {io.write(regs[i],value&~Request);changed=true;}
        // Linux gen9_dbuf_slice_set performs a posting read, then a full 10us
        // settling delay, even when the state bit appears to change earlier.
        value=io.read(regs[i]);
        if(value==UINT32_MAX || bool(value&Request)!=enable)return fail(s,Status::Readback);
        if(changed)io.delayUs(10);
        value=io.read(regs[i]);s.raw[i]=value;
        if(value==UINT32_MAX || bool(value&Request)!=enable)return fail(s,Status::Readback);
        if(bool(value&On)!=enable)return fail(s,Status::Timeout);
        if(enable)s.requested|=bit;else {s.requested&=~bit;s.owned&=~bit;}
        if(value&On)s.powered|=bit;else s.powered&=~bit;
    }
    s.consumers=consumers;return Status::Complete;
}
// Add a native bound path only after the caller proves its FB/plane identity.
// A fresh stopped bank must have empty DDB shadow entries; this read-only
// operation never hides a stale allocation or clears a foreign owner.
template<class IO> Status bindOffPipe(IO &io,State &s,unsigned index) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun)return Status::Unavailable;
    if(index>=3 || s.joined)return fail(s,Status::Topology);
    const auto bit=1U<<index,bank=0x1000*index;
    if(s.selected&bit)return Status::Complete;
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    if(!Display13FBIRQ::consumersStopped(io,bit,s.transcoders))return fail(s,Status::Conflict);
    if(!io.readable(0x7027C+bank) || !io.readable(0x7017C+bank))return Status::Unavailable;
    const auto primary=io.read(0x7027C+bank),cursor=io.read(0x7017C+bank);
    if(primary==UINT32_MAX || cursor==UINT32_MAX)return fail(s,Status::Readback);
    PipeAllocation next;
    if(primary || cursor) {
        const auto &old=s.released[index];
        if(!old.valid || primary!=hardwareRaw(s,index,old.primary) || cursor!=hardwareRaw(s,index,old.cursor))return fail(s,Status::Conflict);
        if(overlapsOther(s,index,old.primary) || overlapsOther(s,index,old.cursor))return fail(s,Status::Range);
        next.primary=old.primary;next.cursor=old.cursor;
    }
    s.pipe[index]=next;s.released[index]={};s.selected|=bit;syncMasks(s);return Status::Complete;
}
// Read-only admission for a retained, powered allocation. Global startup
// preflights every eligible pipe before it makes any shared slice request.
template<class IO> Status resumeProof(IO &io,State &s,unsigned index) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    if(s.updating&(1U<<index))return fail(s,Status::Conflict);
    auto &a=s.pipe[index];const auto bank=0x1000*index;
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    if(!valid(a.primary) || (a.cursor.end && !valid(a.cursor)) || overlap(a.primary,a.cursor) ||
       overlapsOther(s,index,a.primary) || overlapsOther(s,index,a.cursor))return fail(s,Status::Range);
    const auto mask=slices(a.primary)|slices(a.cursor);
    if(mask&~allowed(s,index))return fail(s,Status::Topology);
    if(!hardwareMatches(io,s,index,a.primary,a.cursor))return fail(s,Status::Conflict);
    return Status::Complete;
}
// Restore one pipe's consumer obligation before its enable. Shared slice
// requests and another pipe's pending/old consumers remain.
template<class IO> Status resume(IO &io,State &s,unsigned index=0) {
    const auto proof=resumeProof(io,s,index);
    if(proof!=Status::Complete)return proof;
    auto &a=s.pipe[index];
    const auto mask=slices(a.primary)|slices(a.cursor);
    const auto old=a.consumers;a.consumers=mask;syncMasks(s);
    const auto result=power(io,s,s.consumers|s.unretired,s.consumers);
    if(result!=Status::Complete) {a.consumers=old;syncMasks(s);}
    return result;
}
// A global engine restart precedes native path creation. After the last path
// was detached, there is no retained pipe allocation to resume. Prove the
// empty, stopped state without re-selecting a path or inventing its DDB owner.
// A retained path need not be pipe0. Restore the union of powered, qualified
// allocations. A stopped allocation whose pipe well is off is retained but
// deferred: native getPG and its per-pipe allocation will admit it later.
template<class IO> Status resumeEngine(IO &io,State &s) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun)return Status::Unavailable;
    if(s.selected) {
        if((s.selected&~7U) || s.updating)return fail(s,Status::Conflict);
        const auto wells=io.read(0x45404);
        if(wells==UINT32_MAX || (wells&3)!=3)return fail(s,Status::Power);
        uint32_t restored[3] {},previous[3] {};
        for(unsigned index=0;index<3;++index) {
            auto &a=s.pipe[index];previous[index]=restored[index]=a.consumers;
            if(!(s.selected&(1U<<index)))continue;
            if(!Display13FBIRQ::powered(wells,index)) {
                // Never inspect an unpowered plane bank or erase its old
                // obligations to manufacture an empty/stopped state.
                if(a.consumers || a.unretired ||
                   a.heldPrimary.start || a.heldPrimary.end || a.heldCursor.start || a.heldCursor.end)return fail(s,Status::Power);
                continue;
            }
            const auto proof=resumeProof(io,s,index);
            if(proof!=Status::Complete)return proof;
            restored[index]=slices(a.primary)|slices(a.cursor);
        }
        if(io.read(0x45404)!=wells)return fail(s,Status::Conflict);
        for(unsigned index=0;index<3;++index)s.pipe[index].consumers=restored[index];
        syncMasks(s);
        const auto result=power(io,s,s.consumers|s.unretired,s.consumers);
        if(result!=Status::Complete) {
            for(unsigned index=0;index<3;++index)s.pipe[index].consumers=previous[index];
            syncMasks(s);
        }
        return result;
    }
    if(s.updating || s.unretired || s.consumers || tracked(s))return fail(s,Status::Conflict);
    for(const auto &a:s.pipe)
        if(a.primary.start || a.primary.end || a.cursor.start || a.cursor.end ||
           a.heldPrimary.start || a.heldPrimary.end || a.heldCursor.start || a.heldCursor.end ||
           a.pendingRaw || a.pendingCursor)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::consumersStopped(io,7,s.transcoders))return fail(s,Status::Conflict);
    return power(io,s,0,0);
}
// STOP_ACK, disabled planes/cursor and idle DSB prove only this pipe stopped.
// The caller additionally masks/drains its IRQ consumers. Keep its CPU layout
// for reconnect, but release only slices no other current/pending/old owner uses.
template<class IO> Status stopPipe(IO &io,State &s,unsigned index) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index;
    if(s.updating&bit)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    if(!Display13FBIRQ::consumersStopped(io,bit,s.transcoders))return fail(s,Status::Conflict);
    const auto old=s.pipe[index];auto &a=s.pipe[index];
    a.consumers=a.unretired=0;a.heldPrimary=a.heldCursor={};syncMasks(s);
    const auto result=power(io,s,s.consumers|s.unretired,s.consumers);
    if(result!=Status::Complete) {a=old;syncMasks(s);}
    return result;
}
// Native path destruction is separate from stop/reconnect. A stopped bound
// layout cannot be stolen by another FB before that explicit ownership release.
template<class IO> Status detach(IO &io,State &s,unsigned index) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto &a=s.pipe[index];
    if(a.consumers || a.unretired || (s.updating&(1U<<index)))return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index) || !Display13FBIRQ::consumersStopped(io,1U<<index,s.transcoders))return fail(s,Status::Conflict);
    s.released[index]={true,a.primary,a.cursor};s.pipe[index]={};s.selected&=~(1U<<index);syncMasks(s);return Status::Complete;
}
// Global display-engine/clock teardown still proves every powered bank is
// stopped, including unbound/foreign consumers. No per-pipe ACK substitutes it.
template<class IO> Status stop(IO &io,State &s) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun)return Status::Unavailable;
    if(s.updating)return fail(s,Status::Conflict);
    const auto wells=io.read(0x45404);
    if(wells==UINT32_MAX || (wells&3)!=3 || (wells&(3U<<16)))return fail(s,Status::Power);
    if(s.transcoders.dsi0Pipe<3 && !Display13FBIRQ::consumersStopped(io,1U<<s.transcoders.dsi0Pipe,s.transcoders))return fail(s,Status::Conflict);
    for(unsigned p=0;p<3;++p)if(wells&(1U<<(10+2*p))) {
        const auto conf=Display13Transcoder::control(io,p,s.transcoders),cursor=io.read(0x70080+p*0x1000);
        if(conf==UINT32_MAX || cursor==UINT32_MAX || (conf&(Request|On)) || (cursor&63))return fail(s,Status::Conflict);
        for(unsigned n=0;n<5;++n) {
            const auto ctl=io.read(0x70180+p*0x1000+n*0x100);
            if(ctl==UINT32_MAX || (ctl&Request))return fail(s,Status::Conflict);
        }
        for(unsigned d=0;d<3;++d) {
            const auto ctl=io.read(Display13FBIRQ::DSBControl(p,d));
            if(ctl==UINT32_MAX || (ctl&(Request|1)))return fail(s,Status::Conflict);
        }
    }
    PipeAllocation old[3];
    for(unsigned p=0;p<3;++p) {old[p]=s.pipe[p];s.pipe[p].consumers=s.pipe[p].unretired=0;s.pipe[p].heldPrimary=s.pipe[p].heldCursor={};}
    syncMasks(s);const auto result=power(io,s,0,0);
    if(result!=Status::Complete) {for(unsigned p=0;p<3;++p)s.pipe[p]=old[p];syncMasks(s);}
    return result;
}
// Core preparePrimary accepts ABSOLUTE blocks and an inclusive end.
// The native adapter converts its relative ABI before calling this. Reserve before the
// native write and keep each old range until that same pipe's frame ACK.
// Per-pipe transactions can coexist; shared power IO remains caller-serialized.
template<class IO> Status preparePrimary(IO &io,State &s,uint32_t start,uint32_t end,unsigned index=0) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index,bank=0x1000*index;auto &a=s.pipe[index];
    if(s.updating&bit)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    const Range next {start,end+1};
    if(!end || end>=4096 || !valid(next) || hardwareRaw(s,index,next)==UINT32_MAX)return fail(s,Status::Range);
    const auto mask=slices(next);
    if(mask&~allowed(s,index))return fail(s,Status::Topology);
    if(!hardwareMatches(io,s,index,a.primary,a.cursor))return fail(s,Status::Conflict);
    if(overlap(next,a.cursor) || overlapsOther(s,index,next))return fail(s,Status::Range);
    const auto consumers=s.consumers|s.unretired|mask;
    const auto result=power(io,s,consumers,consumers);if(result!=Status::Complete)return result;
    a.pendingRaw=(end<<16)|start;a.pendingCursor=false;s.updating|=bit;syncMasks(s);return Status::Complete;
}
template<class IO> Status finishPrimary(IO &io,State &s,unsigned index=0) {
    if(s.faulted)return Status::Faulted;
    if(index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index,bank=0x1000*index;auto &a=s.pipe[index];
    if(!(s.updating&bit) || a.pendingCursor)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index) || io.read(0x4438C)!=s.mbus)return fail(s,Status::Power);
    if(!hardwareMatches(io,s,index,decode(a.pendingRaw),a.cursor))return fail(s,Status::Readback);
    const auto next=decode(a.pendingRaw);
    a.unretired|=a.consumers|slices(next)|slices(a.cursor);
    a.heldPrimary=cover(a.heldPrimary,cover(a.primary,next));a.heldCursor=cover(a.heldCursor,a.cursor);
    a.primary=next;a.consumers=slices(a.primary)|slices(a.cursor);s.updating&=~bit;syncMasks(s);return Status::Complete;
}
// Cursor prefix comes from the planned pipe allocation. Moving an active
// cursor still needs explicit stop/relocation; a register read is not its ACK.
template<class IO> Status prepareCursor(IO &io,State &s,unsigned index=0) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index,bank=0x1000*index;auto &a=s.pipe[index];
    if(s.updating&bit)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    const auto layout=plannedLayout(s.capacity ? s.capacity : s.selected,index);
    if(layout.status!=Status::Complete)return fail(s,layout.status);
    const auto next=layout.cursor;const auto raw=hardwareRaw(s,index,next),cursorRaw=hardwareRaw(s,index,a.cursor);
    if(!hardwareMatches(io,s,index,a.primary,a.cursor))return fail(s,Status::Conflict);
    if(overlap(next,a.primary) || overlapsOther(s,index,next))return fail(s,Status::Range);
    const auto ctl=io.read(0x70080+bank);
    if(ctl==UINT32_MAX)return fail(s,Status::Readback);
    if((ctl&63) && cursorRaw!=raw)return fail(s,Status::Conflict);
    const auto consumers=s.consumers|s.unretired|slices(next);
    const auto result=power(io,s,consumers,consumers);if(result!=Status::Complete)return result;
    a.pendingRaw=encoded(next);a.pendingCursor=true;s.updating|=bit;syncMasks(s);return Status::Complete;
}
template<class IO> Status finishCursor(IO &io,State &s,unsigned index=0) {
    if(s.faulted)return Status::Faulted;
    if(index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index,bank=0x1000*index;auto &a=s.pipe[index];
    if(!(s.updating&bit) || !a.pendingCursor)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index) || io.read(0x4438C)!=s.mbus)return fail(s,Status::Power);
    if(!hardwareMatches(io,s,index,a.primary,decode(a.pendingRaw)))return fail(s,Status::Readback);
    const auto next=decode(a.pendingRaw);
    a.unretired|=a.consumers|slices(next)|slices(a.primary);
    a.heldCursor=cover(a.heldCursor,cover(a.cursor,next));a.heldPrimary=cover(a.heldPrimary,a.primary);
    a.cursor=next;a.consumers=slices(a.primary)|slices(a.cursor);s.updating&=~bit;syncMasks(s);return Status::Complete;
}
// Caller passes HARDWARE-relative raw entries and a qualified WM/live latch
// for this exact pipe. One
// pipe's ACK never retires another pipe's old/pending allocation or slice.
template<class IO> Status retire(IO &io,State &s,uint32_t primaryRaw,uint32_t cursorRaw,
                                 uint32_t frameBefore,uint32_t frameAfter,unsigned index=0) {
    if(s.faulted)return Status::Faulted;
    if(!s.begun || index>=3 || !(s.selected&(1U<<index)))return Status::Unavailable;
    const auto bit=1U<<index,bank=0x1000*index;auto &a=s.pipe[index];
    if((s.updating&bit) || frameBefore==frameAfter || uint32_t(frameAfter-frameBefore)>=0x80000000U)return fail(s,Status::Conflict);
    if(!Display13FBIRQ::powered(io.read(0x45404),index))return fail(s,Status::Power);
    if(primaryRaw==UINT32_MAX || cursorRaw==UINT32_MAX || primaryRaw!=hardwareRaw(s,index,a.primary) || cursorRaw!=hardwareRaw(s,index,a.cursor) || io.read(0x7027C+bank)!=primaryRaw ||
       io.read(0x7017C+bank)!=cursorRaw || io.read(0x4438C)!=s.mbus)return fail(s,Status::Conflict);
    const auto old=a;a.unretired=0;a.heldPrimary=a.heldCursor={};syncMasks(s);
    const auto result=power(io,s,s.consumers|s.unretired,s.consumers);
    if(result!=Status::Complete) {a=old;syncMasks(s);}
    return result;
}
}
#endif
