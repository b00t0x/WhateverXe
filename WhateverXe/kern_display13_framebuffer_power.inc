// Common IOFramebuffer power dispatch and wake notification contract.
// The controller owns shared power/clock/DBUF/IRQ state. Native HDMI/DP/eDP
// retain their transport lifecycle; DSI substitutes only its panel/host path.
// IOGraphics subscribers include offline framebuffers, not just active pipes.

uint32_t XE::wrapDisplay13SetPowerState(IOService *fb,uint32_t state) {
    bool notifyWake=false;
    bool nativePowerOn=false;
    recordDSIClient(fb,9,state,UINT32_MAX);
    // Subscribers can re-enter clients. Serialize this transport's callback,
    // without holding its FB mutex across notifications.
    auto busy=callbackXE->adlnOutputWiring.dsi && fb &&
        fb->getMetaClass()->getClassSize()>=0x4B90 && dsiModeTarget(fb) ? &callbackXE->dsiPowerCallbackBusy : nullptr;
    if(busy && __atomic_exchange_n(busy,1U,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
    struct PowerCallback {
        uint32_t *busy;
        ~PowerCallback(){if(busy)__atomic_store_n(busy,0U,__ATOMIC_RELEASE);}
    } callback{busy};
    // Keep WSAA out of the native queue wait whenever this callback cannot
    // finish hardware restoration or its notification obligation. This covers
    // early failures too, not only the post-resume cadence diagnostic. A
    // startup readiness retry has no sleep/wake obligation and must not
    // prevent the initial WindowServer acceleration handshake.
    const bool restoring=callbackXE->dsiRuntimePower.sleeping || callbackXE->dsiRuntimePower.failed ||
        callbackXE->dsiSystemWakeNotify || callbackXE->dsiPowerOffNotified;
    if(busy && state==2 && restoring)__atomic_store_n(&callbackXE->dsiWakeIncomplete,1U,__ATOMIC_RELEASE);
    auto result=[&]() -> uint32_t {
        if(callbackXE->adlnOutputWiring.dsi) {
            if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
            DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
            if(dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb))) {
                if(callbackXE->dsiPowerTest && state<=2 && dsiModeTarget(fb)) {
                    auto controller=Display13FBMemory::field<IOService *>(fb,0x1D0);
                    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
                    auto &power=callbackXE->dsiRuntimePower;
                    if(state<2 && !power.sleeping && !power.failed && dsiNativeUpdateAllowed(fb) &&
                       !callbackXE->dsiPowerOffNotified) {
                        if(!callbackXE->display13FramebufferEvent)return kIOReturnNotReady;
                        // Match prepareToExitWake's WillPowerOff notification
                        // before disabling the display and its completion IRQ.
                        IORecursiveLockUnlock(scope.lock);scope.lock=nullptr;
                        callbackXE->dsiPowerOffNotified=true;
                        ++callbackXE->dsiPowerOffEvents;
                        const auto event=reinterpret_cast<uint32_t (*)(IOService *,int,void *)>(callbackXE->display13FramebufferEvent);
                        const auto status=event(fb,7,nullptr);
                        uint32_t notice[]={1,7,status,callbackXE->dsiPowerOffEvents,callbackXE->dsiPowerOnEvents};
                        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPowerNotifications",notice,sizeof(notice));
                        if(status!=kIOReturnSuccess)return status;
                        scope.lock=Display13FBMemory::field<IORecursiveLock *>(fb,0x4220);
                        if(!scope.lock)return kIOReturnNotReady;
                        IORecursiveLockLock(scope.lock);
                        if(!dsiModeTarget(fb) || Display13FBMemory::singlePath<OSObject *>(fb)!=path ||
                           Display13FBMemory::field<IOService *>(fb,0x1D0)!=controller)return kIOReturnNotReady;
                    }
                    const bool ready=!power.failed && (state==2 ? (power.sleeping ? callbackXE->resumeDSIPath(controller,fb,path) : dsiNativeUpdateAllowed(fb)) :
                        (power.sleeping ? true : (dsiNativeUpdateAllowed(fb) && callbackXE->quiesceDSIPath(controller,fb,path,true))));
                    if(!ready)return kIOReturnNotReady;
                    if(state==2 && callbackXE->dsiSystemWakeNotify) {
                        notifyWake=true;
                    }
                    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4284)=state;
                    return kIOReturnSuccess;
                }
                // One-way runtime stop experiment through the real power callback.
                // Recovery is a reboot; state 2 never replays the donor's DDI path.
                if(callbackXE->dsiStopTest && state<2 && dsiModeTarget(fb)) {
                    const bool stopped=callbackXE->dsiMemoryReleased && callbackXE->dsiDDIReleased &&
                        callbackXE->dsiClockStop.complete() && callbackXE->dsiPanelStop.complete;
                    if(stopped)return kIOReturnSuccess;
                    if(!callbackXE->dsiNativeWriterHooksReady || !dsiNativeUpdateAllowed(fb))return kIOReturnNotReady;
                    auto controller=Display13FBMemory::field<IOService *>(fb,0x1D0);
                    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
                    if(!callbackXE->quiesceDSIPath(controller,fb,path))return kIOReturnNotReady;
                    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4284)=state;
                    return kIOReturnSuccess;
                }
                // Match only the donor's already-Wake no-op. Do not publish a new
                // native power state without executing a DSI power transaction.
                if(state!=2 || Display13FBMemory::field<uint32_t>(fb,0x4284)!=2)return kIOReturnUnsupported;
                return callbackXE->dsiNativeWriterHooksReady && dsiNativeUpdateAllowed(fb) ? kIOReturnSuccess : kIOReturnNotReady;
            }
        }
        if(state==2 && !prepareDisplay13Wake(fb))return kIOReturnNotReady;
        const bool enteringWake=state==2 && callbackXE->ownedDisplay13Framebuffer(fb) &&
            Display13FBMemory::field<uint32_t>(fb,0x4284)!=2;
        const auto status=FunctionCast(wrapDisplay13SetPowerState,callbackXE->orgDSIAttributes[2])(fb,state);
        nativePowerOn=enteringWake && status==kIOReturnSuccess;
        return status;
    }();
    // Preserve the notification obligation across a failed TC handshake or
    // subscriber event. Later power callbacks may retry it without rerunning
    // a completed DSI panel restart.
    // A native output can be the only wake entry on a controller without DSI.
    // Its Sleep exit owns its own notifications, but parked Doze/Wake peers
    // still need common completion. Only a real shared restore generation
    // establishes that obligation; initial startup must not fabricate a wake.
    bool nativeWake=false;
    if(result==kIOReturnSuccess && state==2 && !busy && callbackXE->ownedDisplay13Framebuffer(fb)) {
        auto &c=display13FBPower;
        if(c.lock) {
            IOLockLock(c.lock);nativeWake=c.restoreEpoch!=0;IOLockUnlock(c.lock);
        }
    }
    if(result==kIOReturnSuccess && state==2 && (notifyWake || callbackXE->dsiSystemWakeNotify || nativeWake)) {
        if(callbackXE->finishDisplay13SystemWake(fb,nativePowerOn))callbackXE->dsiSystemWakeNotify=false;
        else result=kIOReturnNotReady;
    } else if(result==kIOReturnSuccess && state==2 && busy && callbackXE->dsiPowerOffNotified) {
        // Display-only doze has no system DidWake obligation. Pair its off
        // notification only after the DSI path is usable again.
        const auto event=reinterpret_cast<uint32_t (*)(IOService *,int,void *)>(callbackXE->display13FramebufferEvent);
        result=event ? event(fb,8,nullptr) : kIOReturnNotReady;
        if(result==kIOReturnSuccess){callbackXE->dsiPowerOffNotified=false;++callbackXE->dsiPowerOnEvents;}
        uint32_t notice[]={1,8,result,callbackXE->dsiPowerOffEvents,callbackXE->dsiPowerOnEvents};
        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPowerNotifications",notice,sizeof(notice));
    }
    if(busy && state==2 && result==kIOReturnSuccess)__atomic_store_n(&callbackXE->dsiWakeIncomplete,0U,__ATOMIC_RELEASE);
    recordDSIClient(fb,9,state,result);
    return result;
}

bool XE::ownedDisplay13Framebuffer(IOService *fb) const {
    return fb && display13FBLifecycleHooksReady && display13FBABIReady && display13PagingLayoutKnown &&
        !strcmp(fb->getMetaClass()->getClassName(),"AppleIntelFramebuffer") &&
        fb->getMetaClass()->getClassSize()>=0x4B90 && adlnTopologyController &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==adlnTopologyController &&
        Display13FBMemory::field<uint32_t>(fb,0x1DC)<3 &&
        Display13FBMemory::field<IOService *>(adlnTopologyController,0xD60+8*Display13FBMemory::field<uint32_t>(fb,0x1DC))==fb;
}

bool XE::finishDisplay13SystemWake(IOService *caller,bool nativePowerOn) {
    if(!display13FramebufferEvent || !caller)return false;
    auto controller=Display13FBMemory::field<IOService *>(caller,0x1D0);
    if(!controller || controller!=adlnTopologyController || controller->getMetaClass()->getClassSize()<0x1C20)return false;
    auto &c=display13FBPower;
    if(!c.lock || !display13PagingLayoutKnown)return false;
    // Notifications can re-enter native clients. Do not hold a framebuffer
    // mutex, Intel workloop gate or owner lock across handleEvent. A second
    // completion caller leaves the obligation pending instead of duplicating it.
    if(__atomic_exchange_n(&displayWakeEventBusy,1U,__ATOMIC_ACQUIRE))return false;
    struct Completion {
        uint32_t *busy;
        ~Completion(){__atomic_store_n(busy,0U,__ATOMIC_RELEASE);}
    } completion{&displayWakeEventBusy};
    IOLockLock(c.lock);const auto epoch=c.restoreEpoch;IOLockUnlock(c.lock);
    // Port handshakes run without FB/shared locks. They apply to TC PHYs in
    // fixed HDMI/DP mode too; connector shape is not a wake admission rule.
    // DidWake releases parked clients, including offline connectors. It is
    // not a scanout-success report and must not depend on an unplugged peer
    // acquiring a link. Keep unsuccessful TC restoration pending/observable;
    // prepareDisplay13Wake requires it for that output if HPD is present.
    const bool portsReady=restoreDisplay13Ports(controller);
    if(displayWakeEventEpoch!=epoch) {
        displayWakeEventEpoch=epoch;displayWakeEventErrors=0;
        for(auto &phase:displayWakeEventPhase)phase=0;
    }
    using Event=uint32_t (*)(IOService *,int,void *);
    const auto event=reinterpret_cast<Event>(display13FramebufferEvent);
    uint32_t events[18]={4,display13PagingLayoutKnown};bool complete=true;
    for(unsigned index=0;index<3;++index) {
        IOLockLock(c.lock);
        auto fb=Display13FBMemory::field<IOService *>(controller,0xD60+8*index);
        if(fb)fb->retain();
        IOLockUnlock(c.lock);
        if(!fb)continue;
        if(fb->getMetaClass()->getClassSize()<0x4B90 ||
           Display13FBMemory::field<IOService *>(fb,0x1D0)!=controller ||
           Display13FBMemory::field<uint32_t>(fb,0x1DC)!=index){complete=false;fb->release();continue;}
        auto &phase=displayWakeEventPhase[index];
        const bool pending=display13PagingLayoutKnown && !(Display13FBMemory::field<uint8_t>(fb,0x199)&0x20);
        // Native peers already notified themselves if paging is active. The
        // DSI adapter bypassed those native calls and owns its own pair.
        const bool internal=adlnOutputWiring.dsi && index==0;
        const auto nativePower=Display13FBMemory::field<uint32_t>(fb,0x4284);
        events[14+index]=nativePower;
        // Native Sleep(0)->Doze/Wake runs prepareToExitSleep, which owns
        // DidWake(true/false). Do not preempt that callback merely because
        // its paging bit is still clear while another FB has already resumed.
        // Doze(1)->Wake does not run prepareToExitSleep; an otherwise parked
        // client in that state still needs the common notification below.
        if(!phase && !internal && pending && nativePower==0)phase=3;
        if(phase==3)events[17]|=1U<<index;
        if(!phase && !pending && !internal)phase=2;
        if(phase<2) {
            events[2]|=1U<<index;
            if(!phase) {
                events[4+2*index]=event(fb,4,reinterpret_cast<void *>(1));
                // handleEvent returns Success without delivery on the wrong
                // power thread. Conversely a subscriber error can follow a
                // delivered DidWake: never replay that scope-decrement event.
                if(Display13FBMemory::field<uint8_t>(fb,0x199)&0x20)phase=1;
                if(events[4+2*index]!=kIOReturnSuccess)displayWakeEventErrors|=1U<<index;
            }
            if(phase==1) {
                // prepareToEnterWake owns native DidPowerOn, including a
                // Doze peer that has not run its callback yet. Complete only
                // the missing DidWake in that case. An already-Wake parked
                // peer has no future transition; DSI also owns its own pair.
                const bool nativeOn=!internal && (nativePower==1 || (fb==caller && nativePowerOn));
                events[5+2*index]=nativeOn ? kIOReturnSuccess : event(fb,8,nullptr);
                phase=2;
                if(internal && events[5+2*index]==kIOReturnSuccess && dsiPowerOffNotified) {
                    dsiPowerOffNotified=false;++dsiPowerOnEvents;
                    uint32_t notice[]={1,8,events[5+2*index],dsiPowerOffEvents,dsiPowerOnEvents};
                    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPowerNotifications",notice,sizeof(notice));
                }
                if(events[5+2*index]!=kIOReturnSuccess)displayWakeEventErrors|=1U<<(index+3);
            }
        }
        // Phase 3 is delegated, not a claim that the native callback finished.
        if(phase==2)events[3]|=1U<<index;
        else if(phase!=3)complete=false;
        fb->release();
    }
    IOLockLock(c.lock);const bool sameEpoch=c.restoreEpoch==epoch;IOLockUnlock(c.lock);
    complete=complete && sameEpoch && !displayWakeEventErrors;
    events[10]=displayWakeEventEpoch;events[11]=displayWakeEventErrors;events[12]=portsReady;events[13]=complete;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDisplay13SystemWakeEvents",events,sizeof(events));
    return complete;
}

bool XE::processDisplay13NativeAttributes(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN21AppleIntelFramebuffer12setAttributeEjm",
        "__ZN21AppleIntelFramebuffer25setAttributeForConnectionEijm",
        "__ZN21AppleIntelFramebuffer24setFramebufferPowerStateEj",
        "__ZN31AppleIntelFramebufferController11hwSetStreamEP21AppleIntelFramebufferP21AppleIntelDisplayPathPK29IODetailedTimingInformationV2"};
    const uint32_t offsets[]={0x3331E,0x352D4,0x24DE8,0x50DB0};
    const uint8_t entries[][20]={
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x18,0x48,0xFF,0x05},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x18,0x48,0xFF,0x05},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x53,0x50,0x41,0x89,0xF6,0x49,0x89,0xFF,0x48,0xFF,0x05,0xF1},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xEC,0x68,0x01,0x00,0x00}};
    if(!display13FramebufferRequested || size<0x50DC4)return false;
    for(unsigned i=0;i<4;++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(fn!=address+offsets[i] || memcmp(reinterpret_cast<void *>(fn),entries[i],20))return false;
    }
    // Native prepareToExitSleep / prepareToEnterWake call the same imported
    // IOFramebuffer::handleEvent with DidWake(true) and DidPowerOn(nullptr).
    // Compare the donor import with the host symbol validated in its Lilu callback.
    if(size>0x26F1C) {
        const uint8_t wake[]={0xBA,1,0,0,0,0x4C,0x89,0xF7,0xBE,4,0,0,0,0xE8};
        const uint8_t on[]={0x4C,0x89,0xEF,0xBE,8,0,0,0,0x31,0xD2,0xE8};
        if(!memcmp(reinterpret_cast<const void *>(address+0x26F0A),wake,sizeof(wake)) &&
           !memcmp(reinterpret_cast<const void *>(address+0x26809),on,sizeof(on))) {
            int32_t wd,od;
            memcpy(&wd,reinterpret_cast<const void *>(address+0x26F18),sizeof(wd));
            memcpy(&od,reinterpret_cast<const void *>(address+0x26814),sizeof(od));
            const auto imported=address+0x26F1C+wd;
            if(wd && od && imported==address+0x26818+od) {
                const auto target=XELPCall::importedTarget(imported,hostFramebufferEvent,
                    [](uintptr_t from,void *to,size_t length) {
                        return (from>>47)==0x1FFFF && tglVM.nofault &&
                            tglVM.nofault(from,reinterpret_cast<uintptr_t>(to),length)==length;
                    });
                display13PagingLayoutKnown=hostFramebufferEvent && target==hostFramebufferEvent;
                if(display13PagingLayoutKnown)display13FramebufferEvent=imported;
                IORegistryEntry::getRegistryRoot()->setProperty("WEXDisplay13PagingContractReady",display13PagingLayoutKnown);
            }
        }
    }
    // A DSI writer replaces the native power callback and therefore owns its
    // notifications and failed-wake escape. Refuse incomplete initialization
    // here, before any GOP ownership or panel power is changed.
    const bool hostReady=display13FramebufferEvent && display13PagingLayoutKnown && hostSleepReady && hostWakeRecoveryReady;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDisplay13HostPowerReady",hostReady);
    if(adlnOutputWiring.dsi && !hostReady)return false;
    // Standard routes copy 17/17/16/20-byte prologues, before RIP counters.
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSISetAttribute,orgDSIAttributes[0]},
        {symbols[1],wrapDSISetConnectionAttribute,orgDSIAttributes[1]},
        {symbols[2],wrapDisplay13SetPowerState,orgDSIAttributes[2]},
        {symbols[3],wrapDSISetStream,orgDSIAttributes[3]}};
    const bool ready=patcher.routeMultiple(index,routes,arrsize(routes),address,size);patcher.clearError();return ready;
}
