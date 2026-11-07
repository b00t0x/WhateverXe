// Opt-in command/panel experiments and read-only IRQ diagnostics.
// Included only by kern_xe.cpp; retains its private owners and lock ordering.

// One optional bootstrap NOP/drain trial before native engine and IRQ users.
// This exercises the same packet engine used by panel sequences, without
// changing panel power/reset, timing, brightness, or any framebuffer state.
bool XE::probeDSICommands(IOService *controller) {
    auto &c=display13FBPower;auto &clock=comboClockTransaction;
    if(!controller || !c.lock || !clock.lock)return false;
    IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
    const auto owned=[&]() {
        return controller==c.controller && controller==adlnTopologyController &&
            dsiResourcesReady && !dsiStopAttempted && adlnOutputWiring.dsi==1 &&
            !c.engineEnabled && !c.memoryFaulted && !c.clockFaulted &&
            !c.bridge.state.faulted && !c.bridge.state.suspended && !clock.thread &&
            clock.allocation.external[0]==reinterpret_cast<uintptr_t>(controller) &&
            clock.allocation.binding[0]==dsiBoot.source.identity.pll+1;
    };
    struct IO : Display13FBBootIO {
        decltype(owned) &owns;
        IO(IOMemoryMap *m,decltype(owned) &q):Display13FBBootIO{m},owns(q) {}
        bool ready() {
            if(!owns() || !readable(0x45404))return false;
            const auto wells=read(0x45404);return wells!=UINT32_MAX && (wells&0x403)==0x403;
        }
        void write(uint32_t reg,uint32_t value) {
            if(reg==0x6B100 || reg==0x6B104)
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;
        }
        void delayUs(unsigned us){IODelay(us);}
    } io{c.map,owned};
    XELP::DSI::CommandPort<IO> port{io};uint64_t record[20] {2};
    // Active video can defer an LP packet until its blanking interval. The
    // cold panel-sequence engine keeps its Linux 100 us default. Bound this
    // running-link trial to three frames per credit wait, rounded to 10 us.
    const auto &mode=dsiProfile.mode;
    const uint64_t period=mode.clockKHz ? (uint64_t(mode.htotal())*mode.vtotal()*1000+mode.clockKHz-1)/mode.clockKHz : 0;
    record[16]=period ? ((3*period+9)/10)*10 : 0;
    uint32_t credits=0,lp=0;
    record[1]=io.ready() && !dsiCommandHost.enabled && record[16]>=100 && record[16]<=100000 && dsiProfile.valid() &&
        c.bridge.outputHeld(io,0,0) && XELP::DSI::bootUnchanged(io,dsiBoot,xelpMemory);
    if(record[1]) {
        const bool empty=port.credits(credits) && port.read(0x6B0D8,lp) &&
            (credits&0x1FFF)==0x1040 && !(lp&((1U<<17)|(1U<<16)|1U));
        record[2]=credits;record[3]=lp;record[4]=empty;
        if(empty) {
            record[17]=io.read(0x70040);
            dsiCommandHost.enabled=true;const uint8_t nop=0;
            record[5]=dsiCommandHost.send(port,0x05,{&nop,1});
            record[6]=record[5] && dsiCommandHost.drain(port,static_cast<unsigned>(record[16]));
            record[18]=io.read(0x70040);
        }
        record[7]=port.credits(credits)?credits:UINT32_MAX;
        record[8]=port.read(0x6B0D8,lp)?lp:UINT32_MAX;
        record[9]=io.ready() && XELP::DSI::bootUnchanged(io,dsiBoot,xelpMemory);
    }
    record[10]=static_cast<uint32_t>(dsiCommandHost.error);record[11]=dsiCommandHost.pending;
    record[12]=dsiCommandHost.headers;record[13]=dsiCommandHost.payloadWords;
    record[14]=dsiCommandHost.waitedUs;record[15]=port.writes;record[19]=dsiBoot.source.conf;
    const bool complete=record[1] && record[4] && record[6] && record[9] &&
        !record[10] && !record[11] && !port.failed && (record[7]&0x1FFF)==0x1040 &&
        record[8]!=UINT32_MAX && !(record[8]&((1U<<17)|(1U<<16)|1U));
    IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSICommandTrial",record,sizeof(record));
    return complete;
}

// Bootstrap-only panel sequence experiment. By default retain host/PHY/PLL;
// the optional host trial rebuilds them before panel INIT_OTP. Neither path
// releases shared resources or enables runtime power transitions.
bool XE::probeDSIPanel(IOService *controller,bool prepareExternalClock) {
    const bool restart=dsiHostTest || prepareExternalClock || dsi60HzClockKHz;
    auto &c=display13FBPower;auto &clock=comboClockTransaction;auto &irq=display13FBIRQ;
    if(!controller || !c.lock || !clock.lock || !irq.lock)return false;
    IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
    const auto owned=[&]() {
        return controller==c.controller && controller==adlnTopologyController &&
            dsiResourcesReady && !dsiStopAttempted && adlnOutputWiring.dsi==1 &&
            !c.engineEnabled && !c.memoryFaulted && !c.clockFaulted &&
            !c.bridge.state.faulted && !c.bridge.state.suspended && !clock.thread &&
            clock.allocation.external[0]==reinterpret_cast<uintptr_t>(controller) &&
            clock.allocation.binding[0]==dsiBoot.source.identity.pll+1 &&
            irq.controller==controller && !irq.state.begun && !irq.state.enabled &&
            !irq.quiescing && !irq.handlers && !irq.softwareHandlers;
    };
    struct IO : Display13FBBootIO {
        decltype(owned) &owns;IOSimpleLock *irqLock;
        bool restart {};unsigned pll {};uint32_t writes {},gpios {};XELP::DSI::Panel::GPIOResult lastGPIO;
        IO(IOMemoryMap *m,decltype(owned) &q,IOSimpleLock *l,bool cold,unsigned id):Display13FBBootIO{m},owns(q),irqLock(l),restart(cold),pll(id) {}
        bool ready() {
            if(!owns() || !readable(0x45404))return false;
            const auto wells=read(0x45404);return wells!=UINT32_MAX && (wells&0x403)==0x403;
        }
        void write(uint32_t reg,uint32_t value) {
            if(restart && XELP::DSI::Restart::writable(reg,pll)) {
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;++writes;return;
            }
            switch(reg) {
                case 0x6B100:case 0x6B104:case 0xC4030:case 0xC7204:case 0xC7304:
                case 0x70180:case 0x7019C:case 0x7B008:
                    *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;++writes;break;
            }
        }
        bool gpio(unsigned role,unsigned value) {
            // Only this short RMW is under the IRQ lock. All VBT delays and
            // FIFO/frame polling execute with interrupts enabled.
            const auto saved=IOSimpleLockLockDisableInterrupt(irqLock);
            lastGPIO=XELP::DSI::Panel::gpio(*this,role,value);
            IOSimpleLockUnlockEnableInterrupt(irqLock,saved);
            if(lastGPIO.complete)++gpios;
            return lastGPIO.complete;
        }
        void delayUs(unsigned us){IODelay(us);}
    } io{c.map,owned,irq.lock,restart,dsiBoot.source.identity.pll};
    Display13Clock::Change bootCDClock;
    // Adopt and stop the firmware's original link first. Only the restart
    // uses the new clock; panel commands and blanking stay firmware-derived.
    auto target=dsiProfile;
    if(dsi60HzClockKHz)target.mode.clockKHz=dsi60HzClockKHz;
    const auto targetPlan=XELP::DSI::Restart::plan(target,dsiBoot.source.referenceKHz);
    struct Lifecycle {
        const XELP::DSI::Restart::Plan &targetPlan;
        bool enabled,prepareExternal;XELPComboClock::ClockAllocation &allocation;uintptr_t owner;
        XELP::DSI::Restart::State &state;Display13Clock::Change &cdclk;
        bool stop(IO &io,const XELP::DSI::Readout &source,const XELP::DSI::Profile &profile) {
            return !enabled || XELP::DSI::Restart::stop(io,source,profile,allocation,owner,state);
        }
        bool start(IO &io,XELP::DSI::Readout &source) {
            if(!enabled)return true;
            // Native external modes request the maximum CDCLK. Prepare that
            // shared clock while the bootstrap cycle has stopped DSI and all
            // other consumers, before native mode setting/IRQ users start.
            // The DSI link PLL remains a separate retained allocation.
            if(prepareExternal) {
                auto &c=display13FBPower;
                Display13FBClockIO clockIO {c.map};
                const auto current=Display13Clock::read(clockIO,display13ClockVariant());
                cdclk.before=current;cdclk.requestedHz=current.nativeMaxHz();
                const bool stopped=io.ready() && state.stopped && !state.started &&
                    !c.engineEnabled && Display13FBIRQ::consumersStopped(io,7,c.transcoders);
                if(!stopped || current.status!=Display13Clock::Status::Complete)return false;
                cdclk=Display13Clock::set(clockIO,current.nativeMaxHz(),{true,true,0},display13ClockVariant());
                if(cdclk.status!=Display13Clock::ChangeStatus::Complete) {
                    adlnFault(c.clockFaulted,__LINE__);return false;
                }
                c.clock=cdclk.after;c.clockStopped=false;c.resumeClockHz=cdclk.after.nativeHz();
                auto controller=reinterpret_cast<IOService *>(owner);
                *reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(controller)+0xE98)=cdclk.after.nativeHz();
                *reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(controller)+0xEA0)=cdclk.after.nativeHz();
            }
            state.config=targetPlan;
            if(!XELP::DSI::Restart::start(io,allocation,owner,state))return false;
            source=XELP::DSI::Restart::expected(source,state);return source.complete();
        }
    } lifecycle{targetPlan,restart,!dsiPanelOnly,clock.allocation,reinterpret_cast<uintptr_t>(controller),dsiHostRestart,bootCDClock};
    uint64_t record[32] {1};
    record[1]=(!restart || targetPlan.valid) && io.ready() && !dsiPanelTrial.attempted && dsiProfile.valid() &&
        !dsiCommandHost.faulted && !dsiCommandHost.pending &&
        c.bridge.outputHeld(io,0,0) && XELP::DSI::bootUnchanged(io,dsiBoot,xelpMemory);
    if(record[1]) {
        record[20]=io.read(0xC7204);record[22]=io.read(0xC4030);
        record[24]=io.read(0xC7200);record[28]=io.read(0x70040);
        XELP::DSI::Panel::cycle(io,dsiBoot.source,dsiProfile,dsiBoot.control,dsiBoot.surface,dsiPanelTrial,lifecycle);
        record[21]=io.ready()?io.read(0xC7204):UINT32_MAX;
        record[23]=io.ready()?io.read(0xC4030):UINT32_MAX;
        record[25]=io.ready()?io.read(0xC7200):UINT32_MAX;
        record[26]=io.ready()?io.read(0x6B0D0):UINT32_MAX;
        record[27]=io.ready()?io.read(0x6B0D8):UINT32_MAX;
        record[29]=io.ready()?io.read(0x70040):UINT32_MAX;
        // Publish the new clock/transport witness only after the complete
        // cycle and old backing/geometry have been revalidated together.
        auto nextBoot=dsiBoot;
        if(dsiPanelTrial.complete())nextBoot.source=dsiPanelTrial.source;
        record[30]=io.ready() && dsiPanelTrial.complete() && XELP::DSI::bootUnchanged(io,nextBoot,xelpMemory);
        if(record[30] && !nextBoot.source.matches(target))record[30]=0;
        if(record[30]){dsiBoot=nextBoot;c.bootDSI=nextBoot.source;dsiProfile.mode.clockKHz=target.mode.clockKHz;}
    }
    const auto &r=dsiPanelTrial;const auto &h=r.host;
    record[2]=static_cast<uint32_t>(r.stage);record[3]=static_cast<uint32_t>(r.error);
    record[4]=r.reg;record[5]=r.value;record[6]=io.writes;record[7]=r.waitUs;
    record[8]=static_cast<uint32_t>(h.error);record[9]=h.pending;record[10]=h.sequenceId;
    record[11]=h.operationOffset;record[12]=h.headers;record[13]=h.payloadWords;
    record[14]=h.waitedUs;record[15]=h.completedOperations;record[16]=io.gpios;
    record[17]=io.lastGPIO.role;record[18]=io.lastGPIO.value;record[19]=io.lastGPIO.complete;
    const bool complete=record[1] && r.complete() && record[30];record[31]=complete;
    const auto &host=dsiHostRestart;const auto &p=host.config;
    uint64_t hostRecord[20] {1,restart,static_cast<uint32_t>(host.stage),static_cast<uint32_t>(host.error),
        host.reg,host.value,host.writes,host.waitUs,host.stopped,host.started,host.pll,
        p.pll.cfg0(),p.pll.cfg1(),p.link.clockTiming,p.link.dataTiming,p.hsTimeout,p.lpTimeout,p.taTimeout,
        record[30],complete};
    uint64_t scan[6] {};
    if(dsi60HzClockKHz && complete && checkKernelArgument("-wex-dsi-audit")) {
        // Count physical pipe-A frame boundaries, even before WindowServer starts.
        scan[0]=1;scan[1]=dsiBoot.source.afeKHz;scan[2]=dsiProfile.mode.clockKHz;
        uint32_t first=io.read(0x70040),edge=first;uint64_t start=0;
        for(unsigned us=0;us<140000 && io.ready();us+=100) {
            const auto frame=io.read(0x70040);
            if(frame==UINT32_MAX || first==UINT32_MAX)break;
            if(!start && frame!=first){start=mach_absolute_time();edge=frame;}
            if(start && uint32_t(frame-edge)>=5) {
                scan[3]=uint32_t(frame-edge);
                absolutetime_to_nanoseconds(mach_absolute_time()-start,&scan[4]);
                scan[5]=XELP::DSI::sourceUnchanged(io,dsiBoot.source);
                break;
            }
            io.delayUs(100);
        }
    }
    IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
    if(scan[0])IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIScanTiming",scan,sizeof(scan));
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPanelTrial",record,sizeof(record));
    if(restart)IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIHostRestart",hostRecord,sizeof(hostRecord));
    if(restart && !dsiPanelOnly)
        IORegistryEntry::getRegistryRoot()->setProperty("N97FBDSIBootCDClock",&bootCDClock,sizeof(bootCDClock));
    return complete;
}

namespace {
// PowerInit retains the native controller (including its IRQ lock) until reboot.
// Read only the counters already collected by the IRQ handler. Never refresh
// the register cache, power a well, or call a native driver from this serializer.
static bool dsiIRQLiveSerialize(void *,void *,OSSerialize *serializer) {
    auto &c=display13FBIRQ;
    if(!__atomic_load_n(&c.controller,__ATOMIC_ACQUIRE) || !c.lock)return false;
    uint64_t record[32] {};
    const auto saved=IOSimpleLockLockDisableInterrupt(c.lock);
    adlnIRQRecord(record,3);
    IOSimpleLockUnlockEnableInterrupt(c.lock,saved);
    auto data=OSData::withBytes(record,sizeof(record));if(!data)return false;
    const bool ok=data->serialize(serializer);data->release();return ok;
}

static void dsiPublishIRQLive() {
    auto property=OSSerializer::forTarget(nullptr,dsiIRQLiveSerialize);
    if(property) {
        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIIRQLive",property);
        property->release();
    }
}
}
