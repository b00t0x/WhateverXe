// Native scanout allocation, ownership, publication and surface handoff.
// Included only by kern_xe.cpp; retains its private owners and lock ordering.

bool XE::processDSIBackingHooks(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={"__ZN18IGAccelDisplayPipe21allocateScanoutMemoryEP19IntelScaledModeDataj",
        "__ZN18IGAccelDisplayPipe23deallocateScanoutMemoryEP19IntelScaledModeDataj"};
    const uintptr_t offsets[]={0x7E5B2,0x7E772};
    const uint8_t entries[][16]={
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC},
        {0x55,0x48,0x89,0xE5,0x41,0x56,0x53,0x8B,0x46,0x38,0x85,0xC0,0x74,0x62,0x49,0x89}};
    bool known=size>0x7F80A;
    for(unsigned i=0;i<2;++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        known=known && fn==address+offsets[i] && !memcmp(reinterpret_cast<void *>(fn),entries[i],16);
    }
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSIBackingAllocate,orgDSIBacking[0]},
        {symbols[1],wrapDSIBackingFree,orgDSIBacking[1]}};
    // Short routes copy only the six-byte push/mov/push prologue. The free
    // entry has a relative branch at byte 12; never copy it into a trampoline.
    const bool ready=known && patcher.routeMultipleShort(index,routes,2,address,size);patcher.clearError();
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBackingHooksReady",ready);return ready;
}

uint32_t XE::wrapDSIBackingAllocate(OSObject *pipe,void *record,uint32_t flags) {
    auto &o=dsiPendingBacking;
    if(!__atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE))
        return FunctionCast(wrapDSIBackingAllocate,callbackXE->orgDSIBacking[0])(pipe,record,flags);
    if(__atomic_load_n(&o.thread,__ATOMIC_ACQUIRE)!=reinterpret_cast<uintptr_t>(current_thread()) || record!=o.record) {
        if(pipe && pipe==__atomic_load_n(&o.pipe,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
        return FunctionCast(wrapDSIBackingAllocate,callbackXE->orgDSIBacking[0])(pipe,record,flags);
    }
    if(!o.allocating || flags || o.pipe || !pipe || !pipe->metaCast("IGAccelDisplayPipe") ||
       pipe->getMetaClass()->getClassSize()<0x12B0 || Display13FBMemory::field<IOService *>(pipe,0x88)!=o.accelerator ||
       Display13FBMemory::field<uintptr_t>(pipe,0x1298) || Display13FBMemory::field<uintptr_t>(pipe,0x12A0) ||
       Display13FBMemory::field<uintptr_t>(pipe,0x12A8))return kIOReturnNotReady;
    pipe->retain();__atomic_store_n(&o.pipe,pipe,__ATOMIC_RELEASE);
    const auto result=FunctionCast(wrapDSIBackingAllocate,callbackXE->orgDSIBacking[0])(pipe,record,flags);
    o.nativeResult=result;const auto &r=*o.record;
    o.allocated=!result && r.refs==1 && r.memory && r.map &&
        Display13FBMemory::field<uintptr_t>(pipe,0x1298)==r.memory &&
        Display13FBMemory::field<uintptr_t>(pipe,0x12A0)==r.map && !Display13FBMemory::field<uintptr_t>(pipe,0x12A8);
    if(o.allocated) {
        // Native init/destroyScanoutResource can replace these pipe fields.
        // Pin the objects while still inside the native GPU lock, before
        // returning to the PTE inspector. Pins alone do not preserve GGTT.
        o.pinnedMap=reinterpret_cast<OSObject *>(r.map);o.pinnedMemory=reinterpret_cast<OSObject *>(r.memory);
        o.pinnedMap->retain();o.pinnedMemory->retain();
    }
    return o.allocated && callbackXE->validateDSINativeBacking() &&
        callbackXE->initializeDSINativeBacking() && callbackXE->switchDSINativeSurface() &&
        callbackXE->publishDSINativeBacking() ? result : kIOReturnNotReady;
}

uint32_t XE::wrapDSIBackingFree(OSObject *pipe,void *record,uint32_t flags) {
    auto &o=dsiPendingBacking;
    if(!__atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE))
        return FunctionCast(wrapDSIBackingFree,callbackXE->orgDSIBacking[1])(pipe,record,flags);
    if(__atomic_load_n(&o.thread,__ATOMIC_ACQUIRE)!=reinterpret_cast<uintptr_t>(current_thread()) || record!=o.record) {
        if(pipe && pipe==__atomic_load_n(&o.pipe,__ATOMIC_ACQUIRE))return kIOReturnNotReady;
        return FunctionCast(wrapDSIBackingFree,callbackXE->orgDSIBacking[1])(pipe,record,flags);
    }
    if(!o.freeing || flags || o.armed || !pipe || pipe!=o.pipe ||
       Display13FBMemory::field<IOService *>(pipe,0x88)!=o.accelerator)return kIOReturnNotReady;
    const auto &r=*o.record;
    if(r.refs>1 || Display13FBMemory::field<uintptr_t>(pipe,0x1298)!=r.memory ||
       Display13FBMemory::field<uintptr_t>(pipe,0x12A0)!=r.map || Display13FBMemory::field<uintptr_t>(pipe,0x12A8) ||
       (!r.refs && (r.map || r.memory || r.gpu || r.pitch)))return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSIBackingFree,callbackXE->orgDSIBacking[1])(pipe,record,flags);
    o.nativeResult=result;o.freed=!result && !r.refs && !r.map && !r.memory && !r.gpu && !r.pitch &&
        !Display13FBMemory::field<uintptr_t>(pipe,0x1298) && !Display13FBMemory::field<uintptr_t>(pipe,0x12A0);
    return o.freed ? result : kIOReturnNotReady;
}

bool XE::prepareDSINativeBacking(IOService *fb) {
    dsiBackingStage=0;
    if(adlnOutputWiring.dsi!=1 || !fb || fb->getMetaClass()->getClassSize()<0x4B90 ||
       !dsiModeTarget(fb) || !dsiBackingHooksReady || !dsiProfile.valid())return false;
    dsiBackingStage=1;
    DSIModeScope scope(fb);if(!scope.lock || !dsiNativeUpdateAllowed(fb))return false;
    auto &o=dsiPendingBacking;auto &c=display13FBPower;
    if(__atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE))return false;
    dsiBackingStage=2;
    IOLockLock(c.lock);
    auto controller=static_cast<IOService *>(c.controller);
    auto owner=reinterpret_cast<IOService *>(display13FBExpectedOwner(controller));
    const auto reserve=__atomic_load_n(&display13FBGTT.sharedPoolPrefix,__ATOMIC_ACQUIRE);
    auto machine=owner && owner->getMetaClass()->getClassSize()>=0x1500 ? Display13FBMemory::field<OSObject *>(owner,0x378) : nullptr;
    XELP::DSI::NativeBacking::Record request;
    const bool ready=adlnOutputWiring.dsi==1 && !c.dsiNativeWriters && owner && machine &&
        machine->metaCast("IGAccelDisplayMachine") && machine->getMetaClass()->getClassSize()>=0x130 &&
        Display13FBMemory::field<IOService *>(machine,0x128)==owner && adlnSharedBinding(controller,owner) &&
        reserve && reserve==Display13FBMemory::field<uint32_t>(owner,0x13C0) &&
        adlnSysMemOwner(controller,Display13FBMemory::pool(controller)) &&
        Display13FBMemory::allocatedSlot(controller,fb)==0 &&
        XELP::DSI::NativeBacking::request(dsiProfile,Display13FBMemory::field<uint32_t>(owner,0x1470),request) &&
        display13FBInspectMemory(fb,true,0,false).status==Display13FBMemory::Status::Complete;
    if(ready){fb->retain();owner->retain();}
    IOLockUnlock(c.lock);if(!ready)return false;
    // Prepare the real native dimensions before handing this allocation to
    // its later public mode owner. No mode-success or backing is published.
    if(!probeDSIModeGeometry(fb)) {owner->release();fb->release();return false;}
    dsiBackingStage=3;
    auto record=static_cast<XELP::DSI::NativeBacking::Record *>(IOMalloc(sizeof(request)));
    if(!record){owner->release();fb->release();return false;}
    memcpy(record,&request,sizeof(request));o.accelerator=owner;o.record=record;
    __atomic_store_n(&o.thread,reinterpret_cast<uintptr_t>(current_thread()),__ATOMIC_RELEASE);o.allocating=true;
    __atomic_store_n(&o.framebuffer,fb,__ATOMIC_RELEASE);
    dsiBackingStage=4;
    uint32_t index=Display13FBMemory::field<uint32_t>(fb,0x1DC),flags=0;
    // Native callPlatformFunction takes the GPU lock. Never hold c.lock here.
    const auto result=owner->callPlatformFunction("allocscanmem",false,record,&index,&flags,nullptr);
    o.allocating=false;
    dsiBackingStage=5;
    if(result || !o.allocated)return false; // Retain partial state for discard.
    IOLockLock(c.lock);
    const bool verified=c.controller==controller && adlnSharedBinding(controller,owner) &&
        o.pipe && o.pinnedMap && o.pinnedMemory &&
        Display13FBMemory::field<IOService *>(o.pipe,0x88)==owner &&
        Display13FBMemory::field<uintptr_t>(o.pipe,0x1298)==record->memory &&
        Display13FBMemory::field<uintptr_t>(o.pipe,0x12A0)==record->map &&
        !Display13FBMemory::field<uintptr_t>(o.pipe,0x12A8) &&
        reserve==__atomic_load_n(&display13FBGTT.sharedPoolPrefix,__ATOMIC_ACQUIRE) &&
        reserve==Display13FBMemory::field<uint32_t>(owner,0x13C0) &&
        !c.memoryFaulted && !c.clockFaulted &&
        XELP::DSI::NativeBacking::allocated(*record,dsiProfile,o.proof);
    IOLockUnlock(c.lock);if(verified)dsiBackingStage=6;return verified;
}

// The first P3 step uses the donor's CPU geometry helper. The selected Y
// layout is the same native tile-mode-3 allocation validated by the caller.
// Retain a bounded receipt of the remaining native transaction prerequisites.
bool XE::probeDSIModeGeometry(IOService *fb) {
    if(!dsiComputeGeometry || !dsiModeTarget(fb) || !dsiModeCoherent(fb))return false;
    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    auto &c=display13FBPower;IOLockLock(c.lock);
    auto controller=static_cast<IOService *>(c.controller);
    uint64_t receipt[16] {1,0,Display13FBMemory::field<uint8_t>(fb,0x4A18),
        Display13FBMemory::field<uint32_t>(fb,0x4350),Display13FBMemory::field<uint32_t>(fb,0x420C)};
    const bool bound=path && path->getMetaClass()->getClassSize()>=0x3730 && controller &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==controller &&
        Display13FBMemory::field<IOService *>(path,0x32B0)==fb && !c.dsiNativeWriters &&
        !c.memoryFaulted && !c.clockFaulted && !dsiStopAttempted;
    uint8_t savedFB[0x30],savedPath[0x20];
    memcpy(savedFB,reinterpret_cast<uint8_t *>(fb)+0x430C,sizeof(savedFB));
    if(bound)memcpy(savedPath,reinterpret_cast<uint8_t *>(path)+0xF0,sizeof(savedPath));
    const bool prepared=bound && XELP::DSI::AppleAdapter::prepareGeometry(fb,path,dsiProfile,dsiFixedMode.timing,
        reinterpret_cast<void (*)(void *,const XELP::DSI::AppleAdapter::Timing *)>(dsiComputeGeometry));
    receipt[1]=prepared;receipt[5]=Display13FBMemory::field<uint32_t>(fb,0x430C);
    receipt[6]=Display13FBMemory::field<uint32_t>(fb,0x4310);receipt[7]=Display13FBMemory::field<uint32_t>(fb,0x4314);
    receipt[8]=Display13FBMemory::field<uint32_t>(fb,0x4334);receipt[9]=Display13FBMemory::field<uint32_t>(fb,0x4338);
    if(controller) {
        receipt[10]=Display13FBMemory::field<OSObject *>(controller,0xC10)!=nullptr;
        receipt[11]=Display13FBMemory::field<uint8_t>(controller,0x1BB4);
        auto panel=Display13FBMemory::field<OSObject *>(controller,0xD0);
        receipt[12]=panel!=nullptr;
        if(panel && panel->getMetaClass()->getClassSize()>=0x70) {
            receipt[13]=Display13FBMemory::field<uint8_t>(panel,0x68);
            receipt[14]=Display13FBMemory::field<IOService *>(panel,0x50)==fb;
        }
        receipt[15]=Display13FBMemory::field<uint8_t>(controller,0xE53);
    }
    // Until FB+4348 publishes the native backing, leave public aperture
    // dimensions untouched. Otherwise IOGraphics could map the old GOP while
    // the identification plane reads the new tiled allocation.
    if(bound) {
        memcpy(reinterpret_cast<uint8_t *>(fb)+0x430C,savedFB,sizeof(savedFB));
        memcpy(reinterpret_cast<uint8_t *>(path)+0xF0,savedPath,sizeof(savedPath));
        const uint32_t bpp=static_cast<uint32_t>(receipt[3]);
        memcpy(reinterpret_cast<uint8_t *>(fb)+0x4350,&bpp,sizeof(bpp));
        *(reinterpret_cast<uint8_t *>(fb)+0x4A18)=static_cast<uint8_t>(receipt[2]);
    }
    IOLockUnlock(c.lock);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIGeometry",receipt,sizeof(receipt));
    return prepared;
}

// Called by allocateScanoutMemory's routed entry while the native display
// machine still holds the GPU lock. Keep object/PTE validation in that scope;
// retained map/memory references alone do not serialize GGTT replacement.
bool XE::validateDSINativeBacking() {
    auto &o=dsiPendingBacking;auto &c=display13FBPower;
    auto fb=o.framebuffer;auto owner=o.accelerator;auto record=o.record;
    if(!fb || !owner || !record || !o.allocating || !o.allocated || !o.pinnedMap || !o.pinnedMemory ||
       o.thread!=reinterpret_cast<uintptr_t>(current_thread()) || !c.lock)return false;
    auto controller=static_cast<IOService *>(c.controller);
    const auto reserve=__atomic_load_n(&display13FBGTT.sharedPoolPrefix,__ATOMIC_ACQUIRE);
    IOLockLock(c.lock);
    bool verified=false;
    const auto pipeBound=[&]() {
        return o.pipe && Display13FBMemory::field<IOService *>(o.pipe,0x88)==owner &&
            Display13FBMemory::field<uintptr_t>(o.pipe,0x1298)==record->memory &&
            Display13FBMemory::field<uintptr_t>(o.pipe,0x12A0)==record->map &&
            !Display13FBMemory::field<uintptr_t>(o.pipe,0x12A8);
    };
    if(pipeBound() && c.controller==controller && c.map && c.map->getVirtualAddress() && c.map->getLength()==0x1000000 &&
       adlnSharedBinding(controller,owner) && !c.memoryFaulted && !c.clockFaulted &&
       reserve && reserve==Display13FBMemory::field<uint32_t>(owner,0x13C0)) {
        Display13SurfaceIO io {c.map,0};
        o.proof=Display13FBMemory::inspectPendingSurface(io,controller,controller->getMetaClass()->getClassSize(),
            fb,fb->getMetaClass()->getClassSize(),reinterpret_cast<uintptr_t>(owner),reserve,
            (c.map->getLength()-0x800000)/8*4096,xelpMemory,record);
        verified=pipeBound() && XELP::DSI::NativeBacking::allocated(*record,dsiProfile,o.proof);
    }
    IOLockUnlock(c.lock);return verified;
}

// Mapping and writes remain inside the same native GPU lock as the allocator
// and full PTE proof. The GPU map is IGAccelMemoryMap, not IOMemoryMap: create
// a separate CPU mapping from its validated SysMemory descriptor.
bool XE::initializeDSINativeBacking() {
    auto &o=dsiPendingBacking;
    if(!o.allocating || !o.allocated || o.armed || !o.record || !o.pinnedMemory ||
       o.thread!=reinterpret_cast<uintptr_t>(current_thread()) ||
       !XELP::DSI::NativeBacking::allocated(*o.record,dsiProfile,o.proof))return false;
    o.initialized=false;
    auto descriptor=OSDynamicCast(IOMemoryDescriptor,Display13FBMemory::field<OSObject *>(o.pinnedMemory,0xD0));
    if(!descriptor || descriptor->getLength()<o.proof.bytes)return false;
    auto map=descriptor->createMappingInTask(kernel_task,0,kIOMapAnywhere,0,o.proof.bytes);
    if(!map)return false;
    const auto address=map->getVirtualAddress();
    if(address && map->getLength()==o.proof.bytes && !(address&4095)) {
        // Clear the entire allocation, including tile padding, before it can
        // become visible. Black is independent of the Y-tile pixel swizzle.
        memset(reinterpret_cast<void *>(address),0,o.proof.bytes);
        for(uint64_t offset=0;offset<o.proof.bytes;offset+=64)
            asm volatile("clflush (%0)"::"r"(address+offset):"memory");
        asm volatile("mfence":::"memory");
        o.initialized=true;
    }
    map->release();
    return o.initialized;
}

// The allocator's native GPU lock and FB mutex span this transaction. The
// owner lock protects shared clock/DDB/IRQ association; no IRQ spinlock spans
// the bounded frame wait. Publish the backing only after the plane has latched.
bool XE::switchDSINativeSurface() {
    using namespace XELP::DSI;
    auto &o=dsiPendingBacking;auto &c=display13FBPower;auto &irq=display13FBIRQ;
    auto fb=o.framebuffer;
    if(!fb || !o.initialized || !o.allocating || !o.allocated || o.armed || !o.record || !c.lock ||
       o.thread!=reinterpret_cast<uintptr_t>(current_thread()) || dsiSession.state!=SessionState::Unbound)return false;
    uint64_t receipt[18] {2,1};
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSISurface",receipt,sizeof(receipt));
    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    IOLockLock(c.lock);
    const auto owned=[&]() {
        return dsiResourcesReady && dsiNativeWriterHooksReady && dsiCacheHooksReady && dsiBackingHooksReady &&
            !dsiStopAttempted && !c.dsiNativeWriters && !c.memoryFaulted && !c.clockFaulted &&
            !c.bridge.state.faulted && !c.bridge.state.suspended && !c.dbuf.faulted && !c.dbuf.updating &&
            !c.pipe[0].destroyingPath && !c.pipe[0].wmProgramming && !c.pipe[0].wmCommitting &&
            c.pipe[0].memoryFramebuffer==fb && c.transcoders.dsi0Pipe==0 && path &&
            path->getMetaClass()->getClassSize()>=0x3730 &&
            Display13FBMemory::field<IOService *>(path,0x32B0)==fb &&
            Display13FBMemory::field<uint32_t>(path,0x3640)==0 &&
            Display13FBMemory::field<uint32_t>(path,0x3644)==2 &&
            Display13FBMemory::field<OSObject *>(path,0x3648)==c.dsiPort &&
            c.bridge.bootAdopted && c.bridge.bootPath==reinterpret_cast<uintptr_t>(path) &&
            adlnSharedBinding(c.controller,o.accelerator) && adlnWriterMappingBound() &&
            o.pipe && Display13FBMemory::field<uintptr_t>(o.pipe,0x1298)==o.record->memory &&
            Display13FBMemory::field<uintptr_t>(o.pipe,0x12A0)==o.record->map &&
            XELP::DSI::NativeBacking::allocated(*o.record,dsiProfile,o.proof);
    };
    struct IO : Display13FBBootIO {
        decltype(owned) &owns;
        IO(IOMemoryMap *m,decltype(owned) &f):Display13FBBootIO{m},owns(f){}
        bool ready(){return owns();}
        uint64_t nativeClockHz(){Display13FBClockIO clock{map};return Display13Clock::read(clock).nativeHz();}
        uint64_t nowUs(){uint64_t t=0,ns=0;clock_get_uptime(&t);absolutetime_to_nanoseconds(t,&ns);return ns/1000;}
        void delayUs(unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
        void write(uint32_t reg,uint32_t value) {
            const bool wm=(reg>=0x70240 && reg<=0x7025C && !(reg&3)) || reg==0x70268;
            bool plane=false;for(auto r:Surface::Registers)plane|=r==reg;
            if(readable(reg) && (wm || plane || reg==0x45270))
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;
        }
    } io{c.map,owned};
    bool irqReady=false;
    if(owned() && irq.lock && adlnIRQOwner(static_cast<IOService *>(c.controller))) {
        const auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
        irqReady=!irq.quiescing && !irq.state.faulted && irq.state.enabled && (irq.admittedPipes&1) &&
            irq.state.transcoders.dsi0Pipe==0;
        IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
    }
    receipt[2]=irqReady;
    Surface::Result result;
    if(irqReady && dsiBoot.complete() && c.bootDSI.sameSource(dsiBoot.source) &&
       c.bridge.outputHeld(io,0,0) && c.latency.status==Display13WM::Status::Complete) {
        const auto ddb=Display13DBUF::hardwareRaw(c.dbuf,0,c.dbuf.pipe[0].primary);
        const auto cursorDDB=Display13DBUF::hardwareRaw(c.dbuf,0,c.dbuf.pipe[0].cursor);
        const auto range=Display13DBUF::decode(ddb);const auto ipc=io.read(0x45004);
        auto wm=Display13WM::plan(dsiProfile.mode.width,dsiProfile.mode.htotal(),uint64_t(dsiProfile.mode.clockKHz)*1000,
            Display13DBUF::valid(range)?range.end-range.start:0,c.latency,
            (ipc&8) || Display13FBMemory::field<uint8_t>(c.controller,0x1B4C),true,dsiProfile.mode.height);
        const auto hz=io.nativeClockHz();const auto pixel=uint64_t(dsiProfile.mode.clockKHz)*1000;
        const Display13WM::Snapshot snapshot{&wm,nullptr,ddb,cursorDDB,static_cast<uint32_t>(o.proof.gpu),0,ipc,hz,false,false,
            Display13WM::pipeLineTime(dsiProfile.mode.htotal(),pixel),pixel,0,c.transcoders};
        Surface::Plane current{dsiBoot.control,io.read(0x701CC),dsiBoot.stride,dsiBoot.size,dsiBoot.surface,0,0};
        auto next=current;next.control=Display13WM::arbitrationControl((current.control&~(7U<<10))|(4U<<10));
        next.color|=1U<<13;next.stride=static_cast<uint32_t>(o.proof.pitch/128);next.surface=static_cast<uint32_t>(o.proof.gpu);
        Surface::Backing backing{reinterpret_cast<uintptr_t>(o.record),o.proof.gpu,o.proof.bytes,o.proof.pitch};
        Capabilities caps;caps.installed=Capabilities::Adoption;
        if(ddb!=UINT32_MAX && cursorDDB!=UINT32_MAX && Display13DBUF::valid(range) &&
           dsiSession.bind(caps,c.bootDSI,dsiProfile,1,reinterpret_cast<uintptr_t>(path),reinterpret_cast<uintptr_t>(&dsiBoot))) {
            result=Surface::change(io,dsiSession,c.bootDSI,dsiProfile,current,next,backing,snapshot);
            o.armed=result.writes!=0 || dsiSession.state==SessionState::Switching || dsiSession.state==SessionState::Faulted ||
                dsiSession.state==SessionState::Streaming;
            if(!o.armed)dsiSession={};
        }
        receipt[3]=ddb;receipt[4]=cursorDDB;receipt[5]=hz;receipt[6]=wm.allocation;
        receipt[7]=next.control;receipt[8]=next.stride;receipt[9]=next.surface;
    }
    receipt[10]=static_cast<uint32_t>(result.status);receipt[11]=result.writes;
    receipt[12]=static_cast<uint32_t>(result.wm.status);receipt[13]=static_cast<uint32_t>(result.latch.status);
    receipt[14]=static_cast<uint32_t>(dsiSession.state);receipt[15]=o.armed;
    receipt[16]=result.latch.frameBefore;receipt[17]=result.latch.frameAfter;
    // Surface::change already proves frame progress, the live surface and
    // source/owner stability before completing the session transaction.
    const bool switched=result.status==Surface::Status::Complete;
    IOLockUnlock(c.lock);
    receipt[1]=2;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSISurface",receipt,sizeof(receipt));
    return switched;
}

// Still inside allocateScanoutMemory's GPU lock and the FB mutex. Transfer
// the already latched allocation to the native FB without reallocating it.
// Returning stolen slot zero changes bookkeeping only; its GGTT reservation
// and the firmware retired-backing record remain held through Session stop.
bool XE::publishDSINativeBacking() {
    auto &o=dsiPendingBacking;auto &c=display13FBPower;auto fb=o.framebuffer;
    if(!fb || !o.allocating || o.thread!=reinterpret_cast<uintptr_t>(current_thread()) ||
       !o.armed || !o.allocated || o.published || dsiSession.state!=XELP::DSI::SessionState::Streaming ||
       !dsiComputeGeometry || !orgAlderLakeNScanoutFree)return false;
    IOLockLock(c.lock);
    auto controller=static_cast<IOService *>(c.controller);auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    const auto pool=Display13FBMemory::pool(controller);
    const auto count=Display13FBMemory::field<uint8_t>(controller,0xDF5);
    const auto used=Display13FBMemory::field<uint8_t>(controller,0xDF4);
    const auto mask=Display13FBMemory::field<uint32_t>(controller,0xDF0);
    uint64_t receipt[12] {1,1,count,used,mask};
    uint8_t savedFB[0x30],savedPath[0x20];
    memcpy(savedFB,reinterpret_cast<uint8_t *>(fb)+0x430C,sizeof(savedFB));
    if(!path) {IOLockUnlock(c.lock);return false;}
    memcpy(savedPath,reinterpret_cast<uint8_t *>(path)+0xF0,sizeof(savedPath));
    const auto oldBpp=Display13FBMemory::field<uint32_t>(fb,0x4350);
    const auto oldTiled=Display13FBMemory::field<uint8_t>(fb,0x4A18);
    Display13FBBootIO io{c.map};
    bool ready=dsiResourcesReady && !dsiStopAttempted && !c.memoryFaulted && !c.clockFaulted &&
        !c.dsiNativeWriters && !c.bridge.state.faulted && dsiModeCoherent(fb) &&
        Display13FBMemory::allocatedSlot(controller,fb)==0 && pool.state==1 && !pool.descriptor &&
        adlnSharedBinding(controller,o.accelerator) &&
        XELP::DSI::NativeBacking::allocated(*o.record,dsiProfile,o.proof) &&
        XELP::DSI::Surface::source(io,c.bootDSI) &&
        XELP::DSI::Surface::word(io,0x701AC,UINT32_MAX,static_cast<uint32_t>(o.proof.gpu)) &&
        XELP::DSI::AppleAdapter::prepareGeometry(fb,path,dsiProfile,dsiFixedMode.timing,
            reinterpret_cast<void (*)(void *,const XELP::DSI::AppleAdapter::Timing *)>(dsiComputeGeometry));
    IOLockUnlock(c.lock);
    if(ready) {
        // FB+4348 is still null, so the exact donor takes only the slot-zero
        // FreeSysMem/freeFBMemory branch; no GPU free is reachable here.
        FunctionCast(wrapAlderLakeNScanoutFree,orgAlderLakeNScanoutFree)(fb);
        IOLockLock(c.lock);
        receipt[5]=Display13FBMemory::field<uint8_t>(controller,0xDF4);
        receipt[6]=Display13FBMemory::field<uint32_t>(controller,0xDF0);
        receipt[7]=Display13FBMemory::field<uint8_t>(fb,0x4288);
        ready=Display13FBMemory::slotReturned(count,0,used,mask,receipt[5],receipt[6],receipt[7]) &&
            Display13FBMemory::poolReturned(pool,Display13FBMemory::pool(controller),0) &&
            !Display13FBMemory::field<void *>(fb,0x4348) && !c.memoryFaulted &&
            Display13FBMemory::field<uint32_t>(fb,0x4334)==o.proof.pitch;
        if(ready) {
            *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fb)+0x4348)=o.record;
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4330)=static_cast<uint32_t>(o.proof.gpu);
            c.pipe[0].scanout=o.proof;
            __atomic_store_n(&c.pipe[0].publishedScanout,reinterpret_cast<uintptr_t>(o.record),__ATOMIC_RELEASE);
            o.published=true;receipt[1]=2;
        }
        IOLockUnlock(c.lock);
        if(ready)ready=updateDSIBootCache(controller,fb,path);
    }
    if(!o.published) {
        IOLockLock(c.lock);
        memcpy(reinterpret_cast<uint8_t *>(fb)+0x430C,savedFB,sizeof(savedFB));
        memcpy(reinterpret_cast<uint8_t *>(path)+0xF0,savedPath,sizeof(savedPath));
        memcpy(reinterpret_cast<uint8_t *>(fb)+0x4350,&oldBpp,sizeof(oldBpp));
        *(reinterpret_cast<uint8_t *>(fb)+0x4A18)=oldTiled;
        IOLockUnlock(c.lock);
    }
    receipt[8]=ready;receipt[9]=o.published;receipt[10]=o.proof.gpu;receipt[11]=o.proof.pitch;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPublicBacking",receipt,sizeof(receipt));
    return ready;
}

// The FB mutex spans native mode completion. These donor CPU leaves preserve
// transaction/notification semantics; the ordinary DDI modesetter is bypassed
// because the DSI link and the new primary plane are already running.
bool XE::publishDSINativeMode(IOService *fb) {
    auto &o=dsiPendingBacking;auto &c=display13FBPower;
    if(!dsiPublishedTarget(fb) || !dsiModeCoherent(fb) || !dsiModeCPU[0] || !dsiModeCPU[1] || !dsiModeCPU[2])return false;
    if(o.modeReady)return dsiNativeUpdateAllowed(fb);
    uint64_t receipt[12] {1,1};
    IOLockLock(c.lock);auto controller=static_cast<IOService *>(c.controller);
    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    const bool ready=!dsiStopAttempted && dsiResourcesReady && !c.memoryFaulted && !c.clockFaulted &&
        !c.bridge.state.faulted && !c.dbuf.faulted && !c.watermarkFaulted && !c.dsiNativeWriters && dsiArmedBackingOwned(fb,path) &&
        dsiSession.state==XELP::DSI::SessionState::Streaming &&
        !Display13FBMemory::field<OSObject *>(controller,0xD0) &&
        !Display13FBMemory::field<uintptr_t>(fb,0x4340) && Display13FBMemory::field<uint32_t>(fb,0x4338)==1;
    IOLockUnlock(c.lock);
    receipt[2]=ready;receipt[3]=ready && updateDSIBootCache(controller,fb,path);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPublicMode",receipt,sizeof(receipt));
    if(!receipt[2] || !receipt[3])return false;
    uint32_t tile=2;
    receipt[2]=o.accelerator->callPlatformFunction("EFITilingMode",true,&tile,nullptr,nullptr,nullptr);
    if(!receipt[2]) {
        *reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(controller)+0xE53)=0;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4250)=XELP::DSI::AppleAdapter::FixedModeID;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4254)=0;
        receipt[3]=reinterpret_cast<uint32_t (*)(IOService *)>(dsiModeCPU[0])(fb);
        if(!receipt[3] && !Display13FBMemory::field<uintptr_t>(fb,0x4340)) {
            reinterpret_cast<void (*)(IOService *,uint32_t)>(dsiModeCPU[1])(fb,0);
            reinterpret_cast<void (*)(IOService *)>(dsiModeCPU[2])(fb);
            IOLockLock(c.lock);
            receipt[4]=Display13FBMemory::field<uint32_t>(fb,0x4000);
            receipt[5]=Display13FBMemory::field<uint32_t>(fb,0x4008);
            receipt[6]=Display13FBMemory::field<uint32_t>(fb,0x400C);
            receipt[7]=Display13FBMemory::field<uint32_t>(fb,0x4200);
            o.modeReady=!receipt[4] && receipt[5]==UINT32_MAX && receipt[6]==UINT32_MAX && receipt[7]==1 &&
                !c.memoryFaulted && !c.clockFaulted && !c.bridge.state.faulted && !dsiStopAttempted &&
                dsiArmedBackingOwned(fb,path) && dsiPublishedTarget(fb);
            receipt[8]=o.modeReady;receipt[9]=Display13FBMemory::field<uint32_t>(fb,0x402C);
            receipt[10]=Display13FBMemory::field<uint32_t>(fb,0x4334);receipt[11]=Display13FBMemory::field<uint32_t>(fb,0x4314);
            IOLockUnlock(c.lock);
        }
    }
    if(o.modeReady) {
        // setupClocks normally copies the detailed timing's Hz into this CPU
        // path field before WM calculation. DSI retains its live PLL, so supply
        // that fixed-mode cache without entering the native DDI clock setup.
        memcpy(reinterpret_cast<uint8_t *>(path)+0x118,&dsiFixedMode.timing.pixelClock,sizeof(uint64_t));
        // Still under the native FB mutex: admit the existing DDB transaction,
        // then require its latch before returning mode success to any client.
        // GOP adoption may have no cursor reservation and a full primary DDB.
        adlnSetupDBUFPipe(controller,fb,0,false);
        IOLockLock(c.lock);
        const auto &allocation=c.dbuf.pipe[0];const auto &pipe=c.pipe[0];
        o.modeReady=!c.dbuf.faulted && !c.watermarkFaulted && !c.memoryFaulted && !c.clockFaulted &&
            !c.bridge.state.faulted && !dsiStopAttempted && !pipe.wmPending &&
            allocation.primary.start==40 && allocation.primary.end==2048 &&
            allocation.cursor.start==0 && allocation.cursor.end==40 &&
            pipe.wmLatch.status==Display13WM::Status::Complete && dsiPublishedTarget(fb);
        uint64_t layout[] {1,allocation.primary.start,allocation.primary.end,allocation.cursor.start,allocation.cursor.end,
            pipe.wmPending,static_cast<uint32_t>(pipe.wmLatch.status),static_cast<uint32_t>(pipe.wmLatch.check),
            c.watermarkFaulted,c.dbuf.faulted,pipe.watermarkClockHz,o.modeReady};
        IOLockUnlock(c.lock);
        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIModeDBUF",layout,sizeof(layout));
    }
    receipt[8]=o.modeReady;
    receipt[1]=2;IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPublicMode",receipt,sizeof(receipt));
    return o.modeReady;
}

// A stopped stream may retain a native backing after partial cleanup. Its
// ownership is distinct from the GOP pool, which stays reserved until later.
bool XE::dsiArmedBackingOwned(IOService *fb,OSObject *path) {
    auto &o=dsiPendingBacking;
    if(!dsiPendingTarget(fb) || !o.armed || o.allocating || o.freeing || !o.record || !o.allocated ||
       !o.pipe || !o.pinnedMap || !o.pinnedMemory || !path)return false;
    const auto handle=reinterpret_cast<uintptr_t>(o.record),lease=reinterpret_cast<uintptr_t>(path);
    const bool session=dsiSession.lease==lease &&
        (dsiSession.currentBacking==handle || dsiSession.pendingBacking==handle) &&
        (dsiSession.state==XELP::DSI::SessionState::Streaming || dsiSession.state==XELP::DSI::SessionState::Faulted);
    const bool cleanup=dsiSession.state==XELP::DSI::SessionState::Released && dsiCleanup.lease==lease &&
        (dsiCleanup.currentBacking==handle || dsiCleanup.pendingBacking==handle);
    return (session || cleanup) && dsiSession.identity.same(dsiBoot.source.identity) &&
        adlnSharedBinding(display13FBPower.controller,o.accelerator) &&
        Display13FBMemory::field<IOService *>(o.pipe,0x88)==o.accelerator &&
        Display13FBMemory::field<uintptr_t>(o.pipe,0x1298)==o.record->memory &&
        Display13FBMemory::field<uintptr_t>(o.pipe,0x12A0)==o.record->map;
}

// Caller has completed host/IRQ/clock stop and dropped owner/clock locks, but
// keeps FB serialization and the provider gate across native GPU cleanup.
bool XE::retireDSINativeBacking(IOService *fb,OSObject *path) {
    if(!dsiPendingTarget(fb))return true;
    if(!dsiStop.complete() || !dsiIRQParked || !dsiClockStop.complete() || !dsiDDIReleased ||
       !dsiArmedBackingOwned(fb,path) || !releaseDSILogical(fb))return false;
    if(dsiSession.state!=XELP::DSI::SessionState::Released) {
        const XELP::DSI::StopProof proof{dsiSession.ticket(),dsiBoot.source.identity,true,true,true,true};
        if(!dsiSession.stopped(proof))return false;
        dsiCleanup=dsiSession.takeCleanup();
    }
    auto &o=dsiPendingBacking;auto &c=display13FBPower;
    const bool published=o.published;auto record=o.record;
    IOLockLock(c.lock);
    if(published && !dsiPublishedTarget(fb)) {IOLockUnlock(c.lock);return false;}
    o.modeReady=false;
    if(published) {
        *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fb)+0x4348)=nullptr;
        __atomic_store_n(&c.pipe[0].publishedScanout,uintptr_t(0),__ATOMIC_RELEASE);
        c.pipe[0].scanout={};o.published=false;
    }
    IOLockUnlock(c.lock);o.armed=false;
    if(discardDSINativeBacking(fb))return true;
    IOLockLock(c.lock);
    if(published) {*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(fb)+0x4348)=record;o.published=true;}
    o.armed=true;IOLockUnlock(c.lock);return false;
}

// The first real fixed-mode request establishes an identification surface
// while the native FB mutex is held. Retain any armed backing until stop.
// Backing publication shares the native allocation serializer. Do this once after allocation starts;
// an unavailable accelerator may be retried by a later mode request. Never
// report a WindowServer mode before its subsequent CPU completion succeeds.
void XE::probeDSINativeBacking(IOService *fb) {
    if(dsiBackingAttempted)return;
    dsiBackingAttempted=true;
    uint64_t receipt[16] {1,1};
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBackingTrial",receipt,sizeof(receipt));
    const bool verified=prepareDSINativeBacking(fb);
    auto &o=dsiPendingBacking;
    const bool pending=dsiPendingTarget(fb);
    receipt[1]=2;receipt[2]=dsiBackingStage;receipt[3]=verified;receipt[4]=pending;
    if(pending) {
        receipt[5]=o.allocated;receipt[6]=o.nativeResult;receipt[7]=static_cast<uint64_t>(o.proof.status);
        receipt[8]=o.proof.bytes;receipt[9]=o.proof.pitch;receipt[10]=o.proof.gpu;receipt[11]=o.proof.verifiedPages;
        receipt[12]=o.pinnedMap && o.pinnedMemory;
    }
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBackingTrial",receipt,sizeof(receipt));
    if(pending && !o.armed)receipt[13]=discardDSINativeBacking(fb);
    receipt[14]=dsiPendingTarget(fb);receipt[15]=pending && !o.armed && !receipt[13];receipt[1]=3;
    if(!pending)dsiBackingAttempted=false;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBackingTrial",receipt,sizeof(receipt));
}

// Only a never-armed pending surface may take this path. After SURF is armed,
// retain it through Session's stop proof; freeing an unlatched target is unsafe.
bool XE::discardDSINativeBacking(IOService *fb) {
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return false;
    IOService *owner=nullptr;OSObject *pipe=nullptr,*mapping=nullptr,*memory=nullptr;void *record=nullptr;
    {
        DSIModeScope scope(fb);if(!scope.lock)return false;
        auto &o=dsiPendingBacking;
        if(!dsiPendingTarget(fb) || o.armed || o.allocating || o.freeing || !o.record)return false;
        if(!adlnSharedBinding(display13FBPower.controller,o.accelerator))return false;
        if(o.pipe) {
            __atomic_store_n(&o.thread,reinterpret_cast<uintptr_t>(current_thread()),__ATOMIC_RELEASE);o.freeing=true;o.freed=false;
            uint32_t index=Display13FBMemory::field<uint32_t>(fb,0x1DC),flags=0;
            o.accelerator->callPlatformFunction("freescanmem",false,o.record,&index,&flags,nullptr);
            o.freeing=false;
            // DisplayMachine's free discards the pipe method's return value.
            // Only our observed native record AND pipe cleanup prove success.
            if(!o.freed)return false;
        } else if(o.record->refs || o.record->map || o.record->memory || o.record->gpu || o.record->pitch)return false;
        owner=o.accelerator;pipe=o.pipe;record=o.record;mapping=o.pinnedMap;memory=o.pinnedMemory;
        __atomic_store_n(&o.framebuffer,static_cast<IOService *>(nullptr),__ATOMIC_RELEASE);
        __atomic_store_n(&o.pipe,static_cast<OSObject *>(nullptr),__ATOMIC_RELEASE);
        o.accelerator=nullptr;o.record=nullptr;o.pinnedMap=o.pinnedMemory=nullptr;__atomic_store_n(&o.thread,uintptr_t(0),__ATOMIC_RELEASE);o.allocated=o.freed=o.published=o.modeReady=false;o.nativeResult=0;o.proof={};o.initialized=false;
    }
    IOFree(record,sizeof(XELP::DSI::NativeBacking::Record));
    if(mapping)mapping->release();if(memory)memory->release();if(pipe)pipe->release();owner->release();fb->release();return true;
}
