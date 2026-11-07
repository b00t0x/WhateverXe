// Firmware profile, native boot/cache binding and resource qualification.
// Included only by kern_xe.cpp; retains its private owners and lock ordering.

bool XE::prepareDSIFirmware(IOService *provider) {
    auto source=provider ? OSDynamicCast(OSData,provider->getProperty("TGLVBT")) : nullptr;
    if(!source || !source->getLength() || source->getLength()>65536)return false;
    if(dsiFirmware)return dsiProfile.valid() && dsiFirmware->getLength()==source->getLength() &&
        !memcmp(dsiFirmware->getBytesNoCopy(),source->getBytesNoCopy(),source->getLength());
    auto copy=OSData::withBytes(source->getBytesNoCopy(),source->getLength());
    if(!copy)return false;
    auto parsed=XELP::DSI::parseProfile({static_cast<const uint8_t *>(copy->getBytesNoCopy()),copy->getLength()},true);
    if(!parsed.valid()){copy->release();return false;}
    // Explicit override for the captured MiniBook X panel tables. Hash the
    // declared VBT supplied by the provider, without Linux debugfs padding.
    if(checkKernelArgument("-wex-dsi-60hz")) {
        static const uint8_t u300[]={
            0xf3,0x03,0x0f,0xec,0x11,0x17,0x81,0x7b,
            0xf0,0x4d,0xf2,0xfa,0x32,0xa0,0x3b,0xfa,
            0xbd,0x8d,0x8a,0x9b,0x78,0xdf,0x6c,0x44,
            0x30,0x8c,0x79,0x06,0x1c,0x1b,0x06,0xa9};
        static const uint8_t n100[]={
            0xe0,0x3d,0x64,0x90,0x5f,0x8d,0x0a,0xc1,
            0x6c,0x7c,0x80,0x49,0xfc,0x45,0x4a,0x93,
            0x27,0xb2,0xbd,0xe3,0x6d,0xa2,0x51,0x81,
            0x3c,0xbd,0x9d,0x78,0x45,0x1d,0x35,0x40};
        auto digest=Crypto::hash(static_cast<const uint8_t *>(copy->getBytesNoCopy()),copy->getLength());
        if(digest) {
            if(!memcmp(digest,u300,sizeof(u300)) || !memcmp(digest,n100,sizeof(n100))) {
                // Keep the firmware blanking and use the 10 kHz EDID clock unit.
                const auto hz=uint64_t(parsed.mode.htotal())*parsed.mode.vtotal()*60;
                dsi60HzClockKHz=static_cast<uint32_t>((hz+5000)/10000*10);
            }
            Buffer::deleter(digest);
        }
    }
    dsiProfile=parsed;dsiFirmware=copy;return true;
}

// Keep profile parsing temporaries out of the already large bootstrap frame.
__attribute__((noinline)) void XE::auditDSIBoot(IOPCIDevice *pci,volatile const uint8_t *base,uint64_t length,
        const AlderLakeN::DisplayReadout &normal,bool admitted) {
    if(!checkKernelArgument("-wex-dsi-audit"))return;
    struct Scratch {XELP::DSI::Profile profile;XELP::DSI::Boot boot;XELP::DSI::BootAudit audit;};
    auto scratch=static_cast<Scratch *>(IOMalloc(sizeof(Scratch)));if(!scratch)return;
    // Pre-native, before the optional external-output handoff. Read only;
    // the scratch certificate must never become the live dsiBoot owner.
    auto vbt=OSDynamicCast(OSData,pci->getProperty("TGLVBT"));scratch->profile={};
    if(vbt && vbt->getLength() && vbt->getLength()<=65536)
        scratch->profile=XELP::DSI::parseProfile({static_cast<const uint8_t *>(vbt->getBytesNoCopy()),vbt->getLength()},true);
    struct ReadIO {
        volatile const uint8_t *base;uint64_t length;
        bool readable(uint32_t reg) const {return base && length>=4 && reg<=length-4;}
        uint32_t read(uint32_t reg) {return *reinterpret_cast<volatile const uint32_t *>(base+reg);}
        uint64_t readPTE(uint32_t reg) {return *reinterpret_cast<volatile const uint64_t *>(base+reg);}
    } io {base,length};
    XELP::DSI::auditBoot(io,normal,scratch->profile,xelpMemory,admitted,scratch->boot,scratch->audit);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBootAudit",&scratch->audit,sizeof(scratch->audit));
    IOFree(scratch,sizeof(Scratch));
}

bool XE::processDisplay13DSICache(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN31AppleIntelFramebufferController16hwUpdateRegCacheEP21AppleIntelFramebufferP21AppleIntelDisplayPath",
        "__ZN31AppleIntelFramebufferController9hwGetCRTCEP21AppleIntelFramebufferP21AppleIntelDisplayPath"};
    const uint32_t offsets[]={0x4C4E2,0x4C79A};
    const uint8_t entries[][16]={
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0xFF,0x05,0x94,0x2A},
        {0x55,0x48,0x89,0xE5,0x41,0x56,0x53,0x48,0x89,0xF3,0x49,0x89,0xFE,0x48,0xFF,0x05}};
    if(!display13FramebufferRequested || size<=0x4C866)return false;
    for(unsigned i=0;i<2;++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(fn!=address+offsets[i] || memcmp(reinterpret_cast<void *>(fn),entries[i],16))return false;
    }
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSIUpdateCache,orgDSICache[0]}, {symbols[1],wrapDSIGetCRTC,orgDSICache[1]}};
    const bool ready=patcher.routeMultiple(index,routes,2,address,size);patcher.clearError();return ready;
}

bool XE::updateDSIBootCache(IOService *controller,IOService *fb,OSObject *path) {
    auto &c=display13FBPower;
    if(!c.lock || !controller || !fb || !path || fb->getMetaClass()->getClassSize()<0x4B90 ||
       controller->getMetaClass()->getClassSize()<0x1C20 || path->getMetaClass()->getClassSize()<0x3730)return false;
    DSIModeScope scope(fb);if(!scope.lock)return false;
    IOLockLock(c.lock);Display13FBBootIO io{c.map};
    const auto binding=adlnInspectPathBinding(fb,path,adlnPlatform,adlnOutputWiring,false);
    const bool bound=dsiCacheHooksReady && !display13Offline && dsiResourcesReady && c.map &&
        adlnOutputWiring.dsi==1 && controller==c.controller && controller==adlnTopologyController &&
        binding.status==TGLFBBinding::Status::Complete && binding.pipe==0 && binding.ddi==0 &&
        c.transcoders.dsi0Pipe==0 && !c.memoryFaulted && !c.clockFaulted &&
        !c.bridge.state.faulted && !c.bridge.state.suspended &&
        Display13FBMemory::field<OSObject *>(path,0x3648)==c.dsiPort && c.dsiPort &&
        Display13FBMemory::field<uint32_t>(c.dsiPort,0x550)==1 &&
        Display13FBMemory::field<uint32_t>(path,0x34)==0x4A480 &&
        c.bootDSI.sameSource(dsiBoot.source) && c.bridge.outputHeld(io,0,0);
    bool ready=false;
    if(bound) {
        auto plane=Display13FBMemory::field<OSObject *>(path,0x32C8);
        auto scaler=Display13FBMemory::field<OSObject *>(path,0x32C0);
        bool addresses=true;
        for(unsigned i=0;i<6;++i)addresses=addresses &&
            Display13FBMemory::field<uint32_t>(plane,XELP::DSI::NativeCache::PlaneAddressFields[i])==XELP::DSI::NativeCache::PlaneRegisters[i];
        uint32_t scale=0;
        if(scaler) {
            addresses=addresses && scaler->getMetaClass()->getClassSize()>=0x50;
            if(addresses) {
                scale=Display13FBMemory::field<uint32_t>(scaler,0x30);
                addresses=Display13FBMemory::field<void *>(scaler,0x28)==c.access &&
                    Display13FBMemory::field<uint32_t>(scaler,0x18)==0 && (scale==0x68180 || scale==0x68280) &&
                    Display13FBMemory::field<uint32_t>(scaler,0x34)==scale-0x10 &&
                    Display13FBMemory::field<uint32_t>(scaler,0x38)==scale-0xC;
            }
        }
        XELP::DSI::NativeCache::View view;
        ready=addresses && (dsiPublishedTarget(fb) ? XELP::DSI::NativeCache::readActive(io,dsiBoot,scale,view) :
            XELP::DSI::NativeCache::read(io,dsiBoot,xelpMemory,scale,view));
        if(ready)XELP::DSI::NativeCache::publish(view,path,plane,scaler);
    }
    IOLockUnlock(c.lock);return ready;
}

void XE::wrapDSIUpdateCache(IOService *controller,IOService *fb,OSObject *path) {
    if(callbackXE->adlnOutputWiring.dsi && dsiCachePath(path)) {
        if(!callbackXE->updateDSIBootCache(controller,fb,path)) {
            auto &c=display13FBPower;
            if(c.lock){IOLockLock(c.lock);adlnFault(c.memoryFaulted,__LINE__);IOLockUnlock(c.lock);}
        }
        return;
    }
    FunctionCast(wrapDSIUpdateCache,callbackXE->orgDSICache[0])(controller,fb,path);
}

void XE::wrapDSIGetCRTC(IOService *controller,IOService *fb,OSObject *path) {
    if(callbackXE->adlnOutputWiring.dsi && dsiCachePath(path)) {
        // DSI brightness belongs to its panel backend. The donor's internal
        // FB branch divides by an ordinary PWM period that may be zero here.
        wrapDSIUpdateCache(controller,fb,path);return;
    }
    FunctionCast(wrapDSIGetCRTC,callbackXE->orgDSICache[1])(controller,fb,path);
}

bool XE::processDisplay13DSIBoot(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbol="__ZN31AppleIntelFramebufferController13probeBootPipeEPbPN17AppleIntelPortHAL3DDIE";
    const uint8_t entry[]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x49,0x89};
    // setupBootDisplay consumes the returned pipe, builtin bool and DDI before
    // allocating the real native path. Do not replace that allocation with a
    // fabricated path. Its DSI cache/stop adapters remain separate prerequisites.
    const uint8_t call[]={0xE8,0xA7,0x1D,0x00,0x00};
    if(!display13FramebufferRequested || size<=0x455E8)return false;
    const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbol,address,size);patcher.clearError();
    if(fn!=address+0x453D6 || memcmp(reinterpret_cast<void *>(fn),entry,sizeof(entry)) ||
       memcmp(reinterpret_cast<void *>(address+0x4362A),call,sizeof(call)))return false;
    KernelPatcher::RouteRequest route(symbol,wrapDSIBootProbe,orgDSIBootProbe);
    const bool ready=patcher.routeMultiple(index,&route,1,address,size);patcher.clearError();return ready;
}

uint32_t XE::wrapDSIBootProbe(IOService *controller,bool *builtin,uint32_t *ddi) {
    auto self=callbackXE;
    if(!self->adlnOutputWiring.dsi)
        return FunctionCast(wrapDSIBootProbe,self->orgDSIBootProbe)(controller,builtin,ddi);
    // Failure must not leak an old builtin/DDI result to setupBootDisplay.
    if(builtin)*builtin=false;if(ddi)*ddi=UINT32_MAX;
    auto &c=display13FBPower;
    if(!self->dsiBootProbeReady || display13Offline || !c.lock || !controller ||
       controller->getMetaClass()->getClassSize()<0x1C20 ||
       !__atomic_load_n(&self->dsiResourcesReady,__ATOMIC_ACQUIRE))return 0xFFFF;
    IOLockLock(c.lock);Display13FBBootIO io{c.map};
    auto port=c.dsiPort;
    const auto config=port && port->getMetaClass()->getClassSize()>=0x6B0 ?
        Display13FBMemory::field<uint32_t *>(port,0x548) : nullptr;
    const bool valid=c.map && self->adlnOutputWiring.dsi==1 && controller==c.controller &&
        controller==self->adlnTopologyController && c.transcoders.dsi0Pipe==0 &&
        !c.memoryFaulted && !c.clockFaulted && !c.bridge.state.faulted && !c.bridge.state.suspended &&
        c.access && Display13FBTopology::bound(self->adlnPlatform,config,0) &&
        self->adlnOutputWiring.matches(config) && config[0]==0 &&
        Display13FBMemory::field<uint32_t>(port,0x120)==0 &&
        Display13FBMemory::field<uint32_t>(port,0x550)==1 &&
        Display13FBMemory::field<void *>(port,0x578)==c.access &&
        self->dsiBoot.complete() && c.bootDSI.sameSource(self->dsiBoot.source) &&
        c.bridge.outputHeld(io,0,0) && XELP::DSI::bootUnchanged(io,self->dsiBoot,xelpMemory);
    if(valid){if(builtin)*builtin=true;if(ddi)*ddi=0;}
    IOLockUnlock(c.lock);return valid?0U:0xFFFFU;
}

// The Display13 native Mode hook has one owner for TC, Combo and DSI.
// Caller must have installed the source/mapping and native power references at
// the adoption boundary. A board record or firmware-on bits alone never admit.
uint32_t XE::setDSIPortMode(OSObject *port,uint32_t wanted) {
    auto self=callbackXE;auto &c=display13FBPower;
    if(!self->display13FBTCHooksReady || !self->display13TCBaseMode || !c.lock ||
       !port || port->getMetaClass()->getClassSize()<0x6B0)return kIOReturnNotReady;
    // Mode zero must be connected to an actual DSI stop/cleanup transaction.
    // Reject it for now, including when the native caller asks only for reset.
    if(wanted!=1)return kIOReturnUnsupported;
    IOLockLock(c.lock);
    auto config=Display13FBMemory::field<uint32_t *>(port,0x548);
    const auto previous=Display13FBMemory::field<uint32_t>(port,0x550);
    const bool bound=self->adlnOutputWiring.dsi==1 && self->adlnOutputWiring.matches(config) &&
        config[0]==0 && Display13FBTopology::bound(self->adlnPlatform,config,0) &&
        c.controller && c.controller==self->adlnTopologyController && c.access &&
        Display13FBMemory::field<void *>(port,0x578)==c.access &&
        Display13FBMemory::field<uint32_t>(port,0x120)==0 &&
        c.transcoders.dsi(0) && c.bootDSI.complete() && c.bootDSI.identity.pipe==0 &&
        c.bootDSI.identity.phy==0 && c.bootDSI.identity.transcoder==0 &&
        (!c.dsiPort || c.dsiPort==port) && previous<=1 &&
        !c.memoryFaulted && !c.clockFaulted && !c.bridge.state.faulted;
    Display13FBBootIO io{c.map};
    const bool ready=bound && c.bridge.outputHeld(io,0,0) && XELP::DSI::sourceUnchanged(io,c.bootDSI);
    uint32_t result=kIOReturnNotReady;
    if(ready) {
        // Exact donor base setPortMode(1) only records port+0x550. The HAL
        // setter and mode0's reset callback must never run on this path.
        result=reinterpret_cast<uint32_t (*)(OSObject *,uint32_t)>(self->display13TCBaseMode)(port,1);
        if(result==kIOReturnSuccess && Display13FBMemory::field<uint32_t>(port,0x550)!=1)result=kIOReturnNotReady;
        if(result==kIOReturnSuccess && !c.dsiPort){port->retain();c.dsiPort=port;}
    }
    IOLockUnlock(c.lock);return result;
}

// Called only after the real PCI/workloop IRQ source and native memory have
// been qualified. Keep the FB mutex across mode publication; never call the
// native timing setter or registry code with the display-owner lock held.
bool XE::adoptDSIBootPath(IOService *fb,OSObject *path) {
    auto &c=display13FBPower;auto &irq=display13FBIRQ;
    if(!c.lock || !fb || !path || fb->getMetaClass()->getClassSize()<0x4B90 ||
       path->getMetaClass()->getClassSize()<0x3730)return false;
    DSIModeScope scope(fb);if(!scope.lock)return false;
    const auto qualified=[&]() {
        if(adlnOutputWiring.dsi!=1 || !dsiResourcesReady || !dsiCacheHooksReady || !dsiBootProbeReady ||
           display13Offline || !c.map || !c.power || !c.controller || c.controller!=adlnTopologyController ||
           c.pipe[0].memoryFramebuffer!=fb || c.memoryFaulted || c.clockFaulted ||
           c.bridge.state.faulted || c.bridge.state.suspended || c.transcoders.dsi0Pipe!=0 || !irq.lock ||
           !Display13Power::objectAssociations(c.power,0x80,c.controller,0x1C20))return false;
        const auto binding=adlnInspectPathBinding(fb,path,adlnPlatform,adlnOutputWiring);
        if(binding.status!=TGLFBBinding::Status::Complete || binding.pipe!=0 || binding.ddi!=0 ||
           Display13FBMemory::field<OSObject *>(path,0x3648)!=c.dsiPort || !c.dsiPort ||
           Display13FBMemory::field<uint32_t>(c.dsiPort,0x550)!=1 ||
           Display13FBMemory::field<uint32_t>(path,0x3644)!=2 ||
           Display13FBMemory::field<uintptr_t>(fb,0x4348) ||
           !Display13FBMemory::field<uint8_t>(fb,0x44DE) || !Display13FBMemory::field<uint8_t>(fb,0x1E7))return false;
        Display13FBBootIO io{c.map};
        if(!c.bridge.outputHeld(io,0,0) || !dsiBoot.complete() ||
           !c.bootDSI.sameSource(dsiBoot.source) || !XELP::DSI::bootUnchanged(io,dsiBoot,xelpMemory))return false;
        const auto memory=display13FBInspectMemory(fb,true,0,false);
        if(memory.status!=Display13FBMemory::Status::Complete || memory.control!=dsiBoot.control ||
           memory.stride!=dsiBoot.stride || memory.size!=dsiBoot.size || memory.surface!=dsiBoot.surface)return false;
        const auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
        const bool enabled=irq.controller==c.controller && (irq.admittedPipes&1) && !irq.quiescing &&
            irq.state.begun && irq.state.enabled && !irq.state.faulted && irq.state.transcoders.dsi0Pipe==0;
        IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);return enabled;
    };
    IOLockLock(c.lock);const bool before=qualified();IOLockUnlock(c.lock);
    bool ready=before && bindDSIFixedMode(fb,dsiProfile);
    IOLockLock(c.lock);
    ready=ready && qualified();
    if(ready) {
        const auto key=reinterpret_cast<uintptr_t>(path);
        ready=c.bridge.bootAdopted ? c.bridge.bootPath==key :
            c.bridge.adoptBootPath(key,0,0).status==Display13Power::Status::Complete;
        ready=ready && Display13Power::publishObject(c.bridge,c.power,0x80,c.controller,0x1C20);
    }
    if(!ready) {
        // The setter may already have published its raw array pointer. Retain
        // all backing/objects until stop; only withdraw mode readiness here.
        if(dsiModeTarget(fb))dsiFixedMode.ready=false;
        adlnFault(c.memoryFaulted,__LINE__);
    }
    IOLockUnlock(c.lock);
    if(!ready && dsiModeTarget(fb))fb->setProperty("WEXDSIFixedModeReady",false);
    return ready;
}

// Called outside the owner lock, before native engine/probe/IRQ users start.
// The lock order matches clock clients: Combo clock -> display owner -> IRQ.
// Failure retains any already acquired boot power references for cleanup.
bool XE::bindDSIResources(IOService *controller) {
    auto &c=display13FBPower;auto &clock=comboClockTransaction;auto &irq=display13FBIRQ;
    if(!controller || controller->getMetaClass()->getClassSize()<0x1C20 ||
       !display13FBIRQReady || !dsiNativeWriterHooksReady || !clock.lock || !c.lock)return false;
    IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
    struct IO : Display13FBBootIO {
        explicit IO(IOMemoryMap *map):Display13FBBootIO{map} {}
        void write(uint32_t reg,uint32_t value) {
            if(reg==0x42084 && readable(reg))
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;
        }
    } io{c.map};
    const bool owned=adlnOutputWiring.dsi==1 && dsiProfile.valid() && dsiBoot.complete() &&
        c.controller==controller && controller==adlnTopologyController && !clock.thread &&
        !dsiResourcesReady && c.transcoders.dsi0Pipe==UINT32_MAX &&
        !c.engineEnabled && !c.dbuf.begun && !c.memoryFaulted && !c.clockFaulted &&
        c.bridge.outputHeld(io,0,0) && XELP::DSI::bootUnchanged(io,dsiBoot,xelpMemory);
    auto irqLock=Display13FBMemory::field<IOSimpleLock *>(controller,0x90);
    bool ready=false;
    if(owned && irqLock) {
        XELPComboClock::ClockSnapshot snapshot;snapshot.layout=XELPComboClock::Layout::Display13;
        snapshot.mux[0]=io.read(0x164280);
        for(unsigned p=0;p<snapshot.pllCount();++p)snapshot.enable[p]=io.read(0x46010+4*p);
        const auto saved=IOSimpleLockLockDisableInterrupt(irqLock);
        if(!irq.controller && !irq.handlers && !irq.softwareHandlers && !irq.quiescing &&
           !irq.admittedPipes && irq.state.transcoders.dsi0Pipe==UINT32_MAX && !irq.state.begun && !irq.state.enabled && !irq.state.faulted &&
           clock.allocation.retain(0,dsiBoot.source.identity.pll,reinterpret_cast<uintptr_t>(controller),snapshot)) {
            // Native X/Y tiled scanout and every external output use GGTT.
            // The live DSI GOP is linear, so this shared mode can be prepared
            // without disabling its plane. Retain PLL/power on any failure.
            if(XELP::DSI::prepareGGTT(io,dsiBoot,xelpMemory,dsiGGTT)) {
                c.clippedDSIBootSize=dsiBoot.clipped ? dsiBoot.size : 0;
                c.bootDSI=dsiBoot.source;c.transcoders.dsi0Pipe=0;
                irq.state.transcoders=c.transcoders;irq.base=c.map->getVirtualAddress();irq.lock=irqLock;
                __atomic_store_n(&irq.admittedPipes,1U,__ATOMIC_RELEASE);
                __atomic_store_n(&irq.controller,controller,__ATOMIC_RELEASE);
                __atomic_store_n(&dsiResourcesReady,true,__ATOMIC_RELEASE);ready=true;
            } else adlnFault(c.memoryFaulted,__LINE__);
        }
        IOSimpleLockUnlockEnableInterrupt(irqLock,saved);
    }
    // External hotplug may request a different CDCLK divider. Prepare it
    // before native display users start; this is not an opt-in host trial.
    bool prepareExternalClock=false;
    if(ready && !dsiPanelOnly && Display13FBTopology::multiOutputWiring(adlnOutputWiring)) {
        const auto current=Display13Clock::read(io,display13ClockVariant());
        ready=current.status==Display13Clock::Status::Complete;
        prepareExternalClock=ready && current.nativeHz()!=current.nativeMaxHz();
    }
    IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
    if(ready)dsiPublishIRQLive();
    if(ready && dsiCommandTest)ready=probeDSICommands(controller);
    if(ready && (dsiPanelTest || prepareExternalClock || dsi60HzClockKHz))ready=probeDSIPanel(controller,prepareExternalClock);
    if(ready) {
        IOLockLock(c.lock);
        using Light=XELP::DSI::Backlight;
        if(dsiBacklight.capture(dsiProfile,io.read(Light::Control),io.read(Light::Period),io.read(Light::Duty))) {
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+0xE58)=dsiBacklight.requested;
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+0xE60)=dsiBacklight.pwm.period;
        }
        IOLockUnlock(c.lock);publishDSIBacklight();
    }
    return ready;
}
