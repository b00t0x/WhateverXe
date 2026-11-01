// Display 13 RGB watermarks (linear and uncompressed, unrotated Y-tiled). Equations follow Linux v6.12 i915
// skl_watermark.c (MIT): skl_wm_method1/2, skl_compute_plane_wm and
// skl_compute_transition_wm, plus native programming and direct-writer latch
// conditions. CPU fixtures alone do not qualify hardware scanout.
#ifndef KERN_DISPLAY13_WM_HPP
#define KERN_DISPLAY13_WM_HPP
#include <stdint.h>
#include "kern_display13_fb_irq.hpp"
namespace Display13WM {
constexpr uint32_t Enabled=1U<<31;
// Caller holds the display owner lock. Shared clock/engine changes need all
// pipes idle; a power release only tests the pipes in its actual domain mask.
// Pending latches count for power release, but cold clock invalidation may
// retire a stopped pipe's old certificate after the hardware stop is proven.
template<class Pipe,unsigned N> uint32_t busyPipes(const Pipe (&pipes)[N],bool pending=false) {
    static_assert(N<=32,"pipe mask overflow");
    uint32_t mask=0;
    for(unsigned i=0;i<N;++i)
        if(pipes[i].wmProgramming || pipes[i].wmCommitting || (pending && pipes[i].wmPending))mask|=1U<<i;
    return mask;
}
enum class Status : uint32_t {Unavailable,Complete,Input,WM0,SAGV,MailboxBusy,MailboxTimeout,MailboxError,Readback,Power,CommitTimeout,Conflict};
// CPU certificates are prerequisites only. The caller still owns each active
// FB mutex and validates hardware at current/target Hz before a shared crawl.
template<class Pipe,unsigned N> bool clockConsumersReady(const Pipe (&pipes)[N],uint32_t active,uint64_t hz) {
    static_assert(N<32,"pipe mask overflow");
    if(!hz || !active || (active&~((1U<<N)-1)) || busyPipes(pipes))return false;
    for(unsigned p=0;p<N;++p)if(active&(1U<<p))
        if(!pipes[p].memoryFramebuffer || pipes[p].destroyingPath || pipes[p].wmPending ||
           pipes[p].wmLatch.status!=Status::Complete || pipes[p].watermarkClockHz!=hz)return false;
    return true;
}
struct Latency {
    Status status {Status::Unavailable};
    uint32_t raw[2] {},us[6] {},sagvUs {},mailboxStatus {},writes {},delayUs {};
};
// Linux gen12_get_dram_info explicitly disables the older WM0 DIMM adjustment.
// Callers pass a newly queried PCODE reply; the Linux table is only a fixture.
inline Latency decodeLatency(uint32_t first,uint32_t second,uint32_t sagvUs) {
    Latency l;l.raw[0]=first;l.raw[1]=second;l.sagvUs=sagvUs;
    if(!sagvUs || sagvUs>65535) {l.status=Status::SAGV;return l;}
    for(unsigned i=0;i<6;++i)l.us[i]=(l.raw[i/4]>>(8*(i%4)))&255;
    unsigned valid=6;
    for(unsigned i=1;i<6;++i)if(!l.us[i]) {
        valid=i;for(unsigned j=i+1;j<6;++j)l.us[j]=0;break;
    }
    if(!l.us[0])for(unsigned i=0;i<valid;++i)l.us[i]+=3;
    l.status=Status::Complete;return l;
}
constexpr uint32_t Mailbox=0x138124,Data=0x138128,Data1=0x13812C;
// Caller owns the common PCODE/display lock. Refuse a busy predecessor without
// touching its data. Linux's read uses 500us fast + 20ms slow polling.
template<class IO> bool readMailbox(IO &io,Latency &l,uint32_t command,uint32_t input,uint32_t &reply) {
    if(!io.readable(Mailbox) || !io.readable(Data) || !io.readable(Data1)) {l.status=Status::Unavailable;return false;}
    auto status=io.read(Mailbox);l.mailboxStatus=status;
    if(status==UINT32_MAX) {l.status=Status::Readback;return false;}
    if(status&Enabled) {l.status=Status::MailboxBusy;return false;}
    io.write(Data,input);io.write(Data1,0);io.write(Mailbox,Enabled|command);l.writes+=3;
    unsigned elapsed=0;
    for(;;) {
        status=io.read(Mailbox);l.mailboxStatus=status;
        if(status==UINT32_MAX) {l.status=Status::Readback;return false;}
        if(!(status&Enabled))break;
        if(elapsed>=20500) {l.status=Status::MailboxTimeout;return false;}
        const auto delay=elapsed<500 ? 10U : 1000U;
        io.delayUs(delay);elapsed+=delay;l.delayUs+=delay;
    }
    if(status&255) {l.status=Status::MailboxError;return false;}
    reply=io.read(Data);
    if(reply==UINT32_MAX) {l.status=Status::Readback;return false;}
    l.status=Status::Complete;return true;
}
template<class IO> Latency queryLatency(IO &io) {
    Latency l;uint32_t first=0,second=0,sagv=0;
    if(!readMailbox(io,l,6,0,first))return l;l.raw[0]=first;
    if(!readMailbox(io,l,6,1,second))return l;l.raw[1]=second;
    if(!readMailbox(io,l,0x23,0,sagv))return l;l.sagvUs=sagv;
    auto decoded=decodeLatency(first,second,sagv);
    decoded.mailboxStatus=l.mailboxStatus;decoded.writes=l.writes;decoded.delayUs=l.delayUs;
    return decoded;
}
inline uint64_t ceilDiv(uint64_t n,uint64_t d) {return n/d+(n%d!=0);}
// Display>=9 PIPE_WM_LINETIME is a pixel-rate quantity in 1/8us units,
// rounded up and saturated, not masked and not divided by CDCLK. ADL-N
// does not have the BXT/GLK IPC divide-by-two workaround.
inline uint32_t pipeLineTime(uint32_t htotal,uint64_t pixelHz) {
    if(!htotal || htotal>16384 || !pixelHz || pixelHz>2000000000ULL)return 0;
    const auto value=ceilDiv(uint64_t(htotal)*8000000,pixelHz);
    return value>511 ? 511 : static_cast<uint32_t>(value);
}
inline uint64_t fixedDiv(uint64_t n,uint64_t d) {
    // Avoid shifting a large latency*pixel-rate numerator into overflow.
    return (n/d)*65536+((n%d)*65536)/d;
}
struct Level {
    uint32_t blocks {},lines {},minimum {};
    bool enable {};
    uint32_t encoded() const {return enable ? Enabled|(lines<<14)|blocks : 0;}
};
struct Plan {
    Status status {Status::Unavailable};
    Level normal[6],transition,sagv,sagvTransition;
    uint32_t allocation {},width {},htotal {},height {};
    uint64_t pixelHz {};
    bool yTiled {};
};
// Linear 32bpp RGB8888 or RGB2101010. The +1 block in blocks-per-line and method1, one-line
// minimum, 10% DDB margin and strict allocation inequality are all required.
inline Level linearLevel(uint32_t width,uint32_t htotal,uint64_t pixelHz,uint32_t latencyUs,uint32_t allocation) {
    Level l;
    if(!latencyUs || !width || width>16384 || htotal<width || htotal>16384 ||
       !pixelHz || pixelHz>2000000000ULL || latencyUs>65793 || !allocation || allocation>4096)return l;
    const auto bpl=ceilDiv(uint64_t(width)*4,512)+1;
    const auto lineTime=ceilDiv(uint64_t(htotal)*1000000,pixelHz);
    const auto method1=fixedDiv(uint64_t(latencyUs)*pixelHz*4,512000000)+65536;
    const auto method2=ceilDiv(uint64_t(latencyUs)*pixelHz,uint64_t(htotal)*1000000)*bpl*65536;
    const bool shortLine=uint64_t(htotal)*4<512 && uint64_t(width)*4<512;
    const auto selected=shortLine || latencyUs>=lineTime ? method2 : method1;
    auto blocks=ceilDiv(selected,65536)+1;
    if(blocks<bpl)blocks=bpl;
    const auto lines=ceilDiv(selected,bpl*65536);
    const auto minimum=blocks+ceilDiv(blocks,10)+1;
    if(lines>255 || blocks>4095 || minimum>allocation)return l;
    l.blocks=static_cast<uint32_t>(blocks);l.lines=static_cast<uint32_t>(lines);
    l.minimum=static_cast<uint32_t>(minimum);l.enable=true;return l;
}
// The native primary switches from GOP linear to Y-tiled RGB. Display13
// uses a four-scanline tile minimum and fractional blocks per scanline.
// The writer uses the native VA; the owner has an independent uncached map.
// Bind both by their physical BAR, while checking the writer against native VA.
inline bool writerMapping(uint64_t writerBase,uint64_t nativeBase,uint64_t nativePhysical,uint64_t nativeLength,
                          uint64_t ownerPhysical,uint64_t ownerLength) {
    return writerBase && writerBase==nativeBase && nativePhysical && nativePhysical==ownerPhysical &&
        nativeLength==0x1000000 && ownerLength==nativeLength;
}
inline bool supportedRGBControl(uint32_t ctl) {
    const auto tile=(ctl>>10)&7,format=(ctl>>23)&31;
    // ICL+ RGB2101010 (4) and RGB8888 (8) both use four bytes per pixel.
    // Native dual-display reconfiguration may select the 30-bit format.
    return ctl!=UINT32_MAX && (format==4 || format==8) && (tile==0 || tile==4) &&
        !(ctl&((1U<<15)|(1U<<4)|3U)); // No CCS/media compression or rotation.
}
// Wa_22012358565: ADL-P Display13 needs one programmable slot for a
// four-byte RGB primary. Preserve format, tiling and all unrelated fields.
inline uint32_t arbitrationControl(uint32_t control) {
    return (control&Enabled) && supportedRGBControl(control) ?
        (control&~(7U<<28))|(1U<<28) : control;
}
inline uint32_t cursorWidth(uint32_t control) {
    if(control==UINT32_MAX)return 0;
    switch(control&0x27) {
        case 7:case 39:return 64;
        case 2:case 34:return 128;
        case 3:case 35:return 256;
        default:return 0;
    }
}
inline bool cursorWidthSupported(uint32_t width) {
    return width==64 || width==128 || width==256;
}
inline bool cursorMatches(uint32_t control,uint32_t width,bool allowDisabled=false) {
    if(control==UINT32_MAX || !cursorWidthSupported(width))return false;
    return (!(control&0x27) && allowDisabled) || cursorWidth(control)==width;
}
inline Level yTiledLevel(uint32_t width,uint32_t htotal,uint64_t pixelHz,uint32_t latencyUs,uint32_t allocation) {
    Level l;
    if(!latencyUs || !width || width>16384 || htotal<width || htotal>16384 ||
       !pixelHz || pixelHz>2000000000ULL || latencyUs>65793 || !allocation || allocation>4096)return l;
    const auto tileBlocks=ceilDiv(uint64_t(width)*4*4,512)+1;
    const auto bpl=tileBlocks*65536/4;
    auto selected=ceilDiv(uint64_t(latencyUs)*pixelHz,uint64_t(htotal)*1000000)*bpl;
    if(selected<4*bpl)selected=4*bpl;
    auto blocks=ceilDiv(selected,65536)+1;
    if(blocks<ceilDiv(bpl,65536))blocks=ceilDiv(bpl,65536);
    const auto lines=ceilDiv(selected,bpl);
    const auto extra=lines%4 ? 8-lines%4 : 4;
    auto minimum=ceilDiv((lines+extra)*bpl,65536);
    if(minimum<blocks)minimum=blocks;
    ++minimum;
    if(lines>255 || blocks>4095 || minimum>allocation)return l;
    l.blocks=static_cast<uint32_t>(blocks);l.lines=static_cast<uint32_t>(lines);
    l.minimum=static_cast<uint32_t>(minimum);l.enable=true;return l;
}
inline Level transition(Level wm0,uint32_t allocation,bool ipc,uint32_t yTileMinimum=0) {
    Level l;if(!ipc || !wm0.enable)return l;
    auto selected=wm0.blocks-1;
    if(selected<2*yTileMinimum)selected=2*yTileMinimum;
    const auto blocks=selected+15; // Selected ceil + minimum4 + amount10 +1.
    auto minimum=blocks+1;if(minimum<wm0.minimum)minimum=wm0.minimum;
    if(blocks>4095 || minimum>allocation)return l;
    l.blocks=blocks;l.minimum=minimum;l.enable=true;return l;
}
inline Plan plan(uint32_t width,uint32_t htotal,uint64_t pixelHz,uint32_t allocation,const Latency &latency,bool ipc,bool yTiled=false,uint32_t height=0) {
    Plan p;p.height=height;p.width=width;p.htotal=htotal;p.pixelHz=pixelHz;p.allocation=allocation;p.yTiled=yTiled;
    if(latency.status!=Status::Complete)return p;
    auto level=yTiled ? yTiledLevel : linearLevel;
    for(unsigned i=0;i<6;++i)p.normal[i]=level(width,htotal,pixelHz,latency.us[i],allocation);
    if(!p.normal[0].enable) {p.status=Status::WM0;return p;}
    const auto tileMinimum=yTiled ? static_cast<uint32_t>(ceilDiv(uint64_t(width)*16,512)+1) : 0;
    p.transition=transition(p.normal[0],allocation,ipc,tileMinimum);
    p.sagv=level(width,htotal,pixelHz,latency.sagvUs+latency.us[0],allocation);
    p.sagvTransition=transition(p.sagv,allocation,ipc,tileMinimum);
    // A complete calculation may legitimately disable higher/transition/SAGV
    // levels that exceed this allocation. SAGV admission must inspect enable.
    p.status=Status::Complete;return p;
}
struct Program {
    Status status {Status::Unavailable};
    uint32_t writes {},readbacks {},lastReg {},expected {},observed {};
};
// Caller holds the selected native framebuffer mutex and display owner
// lock, verifies DDB/plane identity, and supplies the native register writer.
// Normal0..5, transition, and the two SAGV registers all precede the surface
// trigger. Posting reads establish programming, never scanout retirement.
template<class IO> Program program(IO &io,const Plan &p,bool cursor,bool wm0Only,unsigned pipeIndex=0) {
    Program r;
    if(pipeIndex>=3) {r.status=Status::Input;return r;}
    if(p.status!=Status::Complete || !p.normal[0].enable)return r;
    if(!p.sagv.enable) {r.status=Status::SAGV;return r;}
    const uint32_t base=(cursor ? 0x70140 : 0x70240)+0x1000*pipeIndex;
    uint32_t regs[9] {},values[9] {};
    for(unsigned i=0;i<6;++i) {regs[i]=base+4*i;values[i]=wm0Only && i ? 0 : p.normal[i].encoded();}
    regs[6]=base+0x28;values[6]=wm0Only ? 0 : p.transition.encoded();
    regs[7]=base+0x18;values[7]=p.sagv.encoded();
    regs[8]=base+0x1C;values[8]=p.sagvTransition.encoded();
    if(!io.readable(0x45404))return r;
    const auto wells=io.read(0x45404);
    if(!Display13FBIRQ::powered(wells,pipeIndex)) {r.status=Status::Power;return r;}
    for(auto reg:regs)if(!io.readable(reg))return r;
    for(unsigned i=0;i<9;++i) {
        r.lastReg=regs[i];r.expected=values[i];
        io.write(regs[i],values[i]);++r.writes;
        r.observed=io.read(regs[i]);++r.readbacks;
        if(r.observed!=values[i]) {r.status=Status::Readback;return r;}
    }
    r.status=Status::Complete;return r;
}
struct Snapshot {
    const Plan *primary {},*cursor {};
    uint32_t primaryDDB {},cursorDDB {},primarySurface {},cursorSurface {},ipc {};
    uint64_t nativeClockHz {};
    bool wm0Only {},cursorActive {};
    uint32_t pipeLineTime {};
    uint64_t pipePixelHz {};
    unsigned pipeIndex {}; // Display pipe, not physical HDMI DDI or framebuffer index.
    Display13Transcoder::Bindings transcoders;
};
// FlushTriggerPackets writes these native packets in append order. A flip
// queued after WM enable may supersede the surface sampled at enable time.
// Take intent before the flush; never turn the post-flush MMIO value into proof.
// c004fc1a hwUpdateCursorMemory gives each actual pipe two 256KiB
// buffers from controller+DC0; hwSetCursorState writes the selected native
// framebuffer+4AD0 buffer directly, outside FlushTriggerPackets.
struct DirectCursorSurface {
    bool valid {},applied {};
    uint32_t surface {};
};
inline DirectCursorSurface directCursorSurface(uint32_t base,unsigned pipe,unsigned index,
                                               uint32_t first,uint32_t second,uint32_t observed) {
    DirectCursorSurface r;
    const auto expected=uint64_t(base)+(uint64_t(pipe)<<19);
    if(pipe>=3 || index>=2 || !base || (base&4095) || expected+0x40000>UINT32_MAX ||
       first!=expected || second!=expected+0x40000)return r;
    r.valid=true;r.surface=index ? second : first;r.applied=observed==r.surface;
    return r;
}
struct TriggerPacket { uint32_t reg,value,dirty; };
struct TriggerSurfaces {
    bool valid {};
    uint32_t primary {},cursor {},changed {};
};
inline TriggerSurfaces triggerSurfaces(const TriggerPacket *packets,uint32_t count,
                                      uint32_t primary,uint32_t cursor,unsigned pipeIndex=0) {
    TriggerSurfaces r {false,primary,cursor,0};
    if(pipeIndex>=3 || count>64 || (count && !packets))return r;
    const uint32_t bank=pipeIndex*0x1000;
    for(unsigned i=0;i<count;++i) {
        const auto &p=packets[i];
        if(p.reg!=0x7019C+bank && p.reg!=0x70084+bank)continue;
        if(p.value==UINT32_MAX || (p.value&4095))return r;
        if(p.reg==0x7019C+bank) {r.primary=p.value;r.changed|=1;}
        else {r.cursor=p.value;r.changed|=2;}
    }
    r.valid=true;return r;
}
// These linear RGB/cursor WM and PIPE_WM_LINETIME equations do not consume
// CDCLK. An unchanged hardware-verified plan is valid at another clock when
// every consumer still fits its bandwidth. A clock request alone is no proof.
inline bool clockFits(const Snapshot &s,uint64_t nativeHz) {
    return s.primary && s.primary->status==Status::Complete && nativeHz &&
        s.primary->pixelHz && s.primary->pixelHz<=nativeHz/2 && s.pipePixelHz &&
        s.pipePixelHz<=nativeHz/2 && s.pipeLineTime==pipeLineTime(s.primary->htotal,s.pipePixelHz) &&
        (!s.cursorActive || (s.cursor && s.cursor->status==Status::Complete &&
            s.cursor->pixelHz && s.cursor->pixelHz<=nativeHz/2));
}
enum class LatchCheck : uint32_t {None,Controls,OtherPlane,DSB,Clock,Registers,Live,Frame,AwaitingScanout,AwaitingFlip};
struct Latch {
    Status status {Status::Unavailable};
    uint32_t frameBefore {},frameAfter {},waitUs {},budgetUs {},lastReg {},expected {},observed {};
    // Reuse the values already read by the proof. No diagnostic MMIO reads,
    // retries, or relaxed acceptance; preserve the first failed check in IORegistry.
    LatchCheck check {LatchCheck::None};
    uint32_t pipeControl {},planeControl {},cursorControl {},checkRegister {},checkValue {};
    uint64_t clockHz {};
};
template<class IO> bool compare(IO &io,Latch &r,uint32_t reg,uint32_t expected) {
    if(!io.readable(reg)) {r.status=Status::Unavailable;return false;}
    r.lastReg=reg;r.expected=expected;r.observed=io.read(reg);
    if(r.observed!=expected) {r.status=Status::Readback;return false;}
    return true;
}
template<class IO> bool verifyWM(IO &io,Latch &r,const Plan &p,bool cursor,bool wm0Only,unsigned pipeIndex=0) {
    if(pipeIndex>=3) {r.status=Status::Input;return false;}
    const uint32_t base=(cursor ? 0x70140 : 0x70240)+0x1000*pipeIndex;
    for(unsigned i=0;i<6;++i)
        if(!compare(io,r,base+4*i,wm0Only && i ? 0 : p.normal[i].encoded()))return false;
    return compare(io,r,base+0x28,wm0Only ? 0 : p.transition.encoded()) &&
        compare(io,r,base+0x18,p.sagv.encoded()) && compare(io,r,base+0x1C,p.sagvTransition.encoded());
}
// Called after the native direct-writer trigger queue has actually flushed.
// Linux intel_vblank.c identifies the ctg+ frame counter increment with the
// VBlank register latch. A new frame AFTER the posting reads, stable WM/DDB,
// and matching live surfaces qualify retirement; a shadow read alone cannot.
// Owner lock and pipe/engine leases remain held. IRQ needs neither this mutex
// nor the workloop, so hardware can advance while delayUs sleeps.
template<class IO> Latch latch(IO &io,const Snapshot &s) {
    Latch r;
    if(s.pipeIndex>=3 || !s.transcoders.valid()) {r.status=Status::Input;return r;}
    const uint32_t bank=s.pipeIndex*0x1000;
    const auto timing=s.transcoders.timing(s.pipeIndex);
    if(!s.primary || s.primary->status!=Status::Complete || !s.primary->normal[0].enable ||
       !s.primary->sagv.enable || !clockFits(s,s.nativeClockHz) || !s.pipeLineTime || s.pipeLineTime>511 ||
       (s.cursorActive && (!s.cursor || s.cursor->status!=Status::Complete || !s.cursor->sagv.enable)))return r;
    const uint32_t required[] {0x45404,0x70008+bank,0x70180+bank,0x70080+bank,0x70190+bank,timing,timing+0xC,0x70040+bank};
    for(auto reg:required)
        if(!io.readable(reg))return r;
    const auto wells=io.read(0x45404);
    if(!Display13FBIRQ::powered(wells,s.pipeIndex)) {r.status=Status::Power;return r;}
    const auto ht=io.read(timing),vt=io.read(timing+0xC);
    const auto htotal=((ht>>16)&0x3FFF)+1,vtotal=((vt>>16)&0x3FFF)+1;
    if(ht==UINT32_MAX || vt==UINT32_MAX || htotal!=s.primary->htotal || !s.primary->height || s.primary->height>8192 || vtotal<s.primary->height || !s.primary->pixelHz) {r.status=Status::Input;return r;}
    const auto period=ceilDiv(uint64_t(htotal)*vtotal*1000000,s.pipePixelHz);
    if(period<1000 || period>100000) {r.status=Status::Input;return r;}
    r.budgetUs=static_cast<uint32_t>(3*period+1000);
    r.frameBefore=io.read(0x70040+bank);
    if(r.frameBefore==UINT32_MAX) {r.status=Status::Readback;return r;}
    const auto start=io.nowUs();
    for(;;) {
        const auto power=io.read(0x45404);
        if(!Display13FBIRQ::powered(power,s.pipeIndex)) {r.status=Status::Power;return r;}
        const auto pipe=Display13Transcoder::control(io,s.pipeIndex,s.transcoders),plane=io.read(0x70180+bank),cursor=io.read(0x70080+bank);
        r.check=LatchCheck::Controls;r.pipeControl=pipe;r.planeControl=plane;r.cursorControl=cursor;
        // Native enablePlane/scaler can flush WM before enabling scanout.
        // Reconnect may retain a nonzero SURF from the previous path, so a
        // surface address alone cannot distinguish this from initial setup.
        // A disabled primary has no live latch to prove, even if the cursor
        // is already active. Keep WM/DDB pending and the shared clock
        // unqualified until a later enabled plane passes the full proof.
        const bool preparing=plane!=UINT32_MAX && !(plane&Enabled);
        if(pipe==UINT32_MAX || plane==UINT32_MAX || cursor==UINT32_MAX || (pipe&0xC0000000)!=0xC0000000 ||
           (!preparing && (!(plane&Enabled) || !supportedRGBControl(plane) || (((plane>>10)&7)==4)!=s.primary->yTiled)) ||
           bool(cursor&0x27)!=s.cursorActive || (s.cursorActive && !cursorMatches(cursor,s.cursor->width))) {r.status=Status::Conflict;return r;}
        // No autonomous DSB writer is admitted by this direct-MMIO proof.
        for(unsigned p=1;p<5;++p) {
            const auto reg=0x70180+bank+0x100*p;
            if(!io.readable(reg))return r;
            const auto ctrl=io.read(reg);
            r.check=LatchCheck::OtherPlane;r.checkRegister=reg;r.checkValue=ctrl;
            if(ctrl==UINT32_MAX || (ctrl&Enabled)) {r.status=Status::Conflict;return r;}
        }
        for(unsigned d=0;d<3;++d) {
            const auto reg=Display13FBIRQ::DSBControl(s.pipeIndex,d);
            if(!io.readable(reg))return r;
            const auto ctrl=io.read(reg);
            r.check=LatchCheck::DSB;r.checkRegister=reg;r.checkValue=ctrl;
            if(ctrl==UINT32_MAX || (ctrl&(Enabled|1))) {r.status=Status::Conflict;return r;}
        }
        r.check=LatchCheck::Clock;r.clockHz=io.nativeClockHz();
        if(r.clockHz!=s.nativeClockHz) {r.status=Status::Conflict;return r;}
        if(preparing) {r.check=LatchCheck::AwaitingScanout;return r;}
        r.check=LatchCheck::Registers;
        if(!compare(io,r,timing,ht) || !compare(io,r,timing+0xC,vt) || !compare(io,r,0x45270+4*s.pipeIndex,s.pipeLineTime) ||
           !compare(io,r,0x70190+bank,((s.primary->height-1)<<16)|(s.primary->width-1)) ||
           !compare(io,r,0x7027C+bank,s.primaryDDB) || !compare(io,r,0x7017C+bank,s.cursorDDB) ||
           !compare(io,r,0x45004,s.ipc) || !compare(io,r,0x7019C+bank,s.primarySurface) ||
           !verifyWM(io,r,*s.primary,false,s.wm0Only,s.pipeIndex))return r;
        if(s.cursorActive && (!compare(io,r,0x70084+bank,s.cursorSurface) || !verifyWM(io,r,*s.cursor,true,s.wm0Only,s.pipeIndex)))return r;
        r.frameAfter=io.read(0x70040+bank);
        r.waitUs=static_cast<uint32_t>(io.nowUs()-start);
        if(r.frameAfter==UINT32_MAX) {r.status=Status::Readback;return r;}
        if(r.frameAfter!=r.frameBefore) {
            r.check=LatchCheck::Frame;
            if(uint32_t(r.frameAfter-r.frameBefore)>=0x80000000U) {r.status=Status::Conflict;return r;}
            // The frame counter can advance at VBlank before the live-surface
            // readback catches up (observed for a native direct cursor swap).
            // Keep every programmed/control/clock check on each poll; qualify
            // retirement only after both actual live surfaces match. A stable
            // mismatch at the existing deadline leaves retirement pending.
            // A later native flush may prove it; do not fault the owner or
            // infer success from the programmed surface. WM/DDB resources
            // and clock qualification remain held until a complete proof.
            r.check=LatchCheck::Live;
            const bool live=compare(io,r,0x701AC+bank,s.primarySurface) &&
                (!s.cursorActive || compare(io,r,0x700AC+bank,s.cursorSurface));
            if(live) {r.status=Status::Complete;return r;}
            if(r.waitUs>=r.budgetUs) {
                if(r.status==Status::Readback && r.observed!=UINT32_MAX) {
                    r.status=Status::Unavailable;r.check=LatchCheck::AwaitingFlip;
                }
                return r;
            }
        }
        if(r.waitUs>=r.budgetUs) {r.status=Status::CommitTimeout;return r;}
        io.delayUs(1000);
    }
}
}
#endif
