// Native writer admission, brightness, display power and path retirement.
// Included only by kern_xe.cpp; retains its private owners and lock ordering.

// Caller holds the native FB mutex through the admitted writer. quiesce uses
// the same mutex, so a stop cannot interleave after this check. This is one
// update boundary, not a certificate for other native writers or Session Stop.
bool XE::dsiNativeUpdateAllowed(IOService *fb) {
    if(!callbackXE->adlnOutputWiring.dsi)return true;
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return false;
    const auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    if(!dsiLifecycleTarget(fb,path))return true;
    auto &c=display13FBPower;if(!c.lock || dsiFixedMode.changingThread || dsiFixedMode.presentationFault)return false;
    IOLockLock(c.lock);
    const bool ready=(!dsiPendingTarget(fb) || (dsiPublishedTarget(fb) && dsiPendingBacking.modeReady)) && !callbackXE->dsiStopAttempted && callbackXE->dsiResourcesReady &&
        !c.memoryFaulted && !c.clockFaulted && !c.bridge.state.faulted && !c.bridge.state.suspended &&
        !c.dbuf.faulted && !c.watermarkFaulted && c.pipe[0].memoryFramebuffer==fb && !c.pipe[0].destroyingPath &&
        c.transcoders.dsi0Pipe==0 && path && path->getMetaClass()->getClassSize()>=0x3730 &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==c.controller &&
        Display13FBMemory::field<IOService *>(path,0x32B0)==fb && Display13FBMemory::field<uint32_t>(path,0x3640)==0 &&
        Display13FBMemory::field<uint32_t>(path,0x3644)==2 && c.dsiPort && c.dsiPort->getMetaClass()->getClassSize()>=0x580 &&
        Display13FBMemory::field<OSObject *>(path,0x3648)==c.dsiPort && Display13FBMemory::field<uint32_t>(c.dsiPort,0x550)==1;
    IOLockUnlock(c.lock);return ready;
}

// Owner lock held. DSI reserves pipe A even after its mapping is
// withdrawn. Late shared callbacks must not revive it or fault peer outputs.
// An unbound/ambiguous callback cannot establish a different output owner.
bool XE::dsiSharedWriterStopped(IOService *fb,unsigned pipe) {
    return callbackXE->adlnOutputWiring.dsi && ((dsiPendingTarget(fb) && !dsiPendingBacking.modeReady) ||
        (callbackXE->dsiStopAttempted && (pipe==0 || pipe>=3 || dsiModeTarget(fb))));
}

// Cursor image/state bodies can wait for a frame, so do not extend their
// native FB-lock intervals. Count admitted DSI calls instead. Stop takes the
// same FB mutex before testing the count and cannot start until it is zero.
int XE::beginDSINativeWriter(IOService *controller,IOService *fb) {
    if(!callbackXE->adlnOutputWiring.dsi)return 0;
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return -1;
    DSIModeScope scope(fb);if(!scope.lock)return -1;
    if(!dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb)))return 0;
    if(!callbackXE->dsiNativeWriterHooksReady || !dsiNativeUpdateAllowed(fb))return -1;
    auto &c=display13FBPower;IOLockLock(c.lock);
    const bool admitted=controller==c.controller && c.dsiNativeWriters!=UINT32_MAX;
    if(admitted)++c.dsiNativeWriters;
    IOLockUnlock(c.lock);return admitted ? 1 : -1;
}

void XE::endDSINativeWriter(int token) {
    if(token!=1)return;
    auto &c=display13FBPower;IOLockLock(c.lock);
    if(c.dsiNativeWriters)--c.dsiNativeWriters;
    else adlnFault(c.memoryFaulted,__LINE__);
    IOLockUnlock(c.lock);
}

uint32_t XE::wrapDSICursorImage(IOService *controller,IOService *fb,void *info) {
    const int token=beginDSINativeWriter(controller,fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSICursorImage,callbackXE->orgDSICursor[0])(controller,fb,info);
    endDSINativeWriter(token);return result;
}

uint32_t XE::wrapDSICursorState(IOService *controller,IOService *fb,int32_t x,int32_t y,bool visible,bool commit) {
    const int token=beginDSINativeWriter(controller,fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSICursorState,callbackXE->orgDSICursor[1])(controller,fb,x,y,visible,commit);
    endDSINativeWriter(token);return result;
}

// Transaction bodies may wait and may call armUpdate, cursor or color hooks.
// Count the complete body without changing its native lock/wait intervals.
int XE::beginDSIFramebufferWriter(IOService *fb) {
    if(!callbackXE->adlnOutputWiring.dsi)return 0;
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return -1;
    return beginDSINativeWriter(Display13FBMemory::field<IOService *>(fb,0x1D0),fb);
}

uint32_t XE::wrapDSIFlipTransaction(IOService *fb,void *transaction,uint64_t options,void *params) {
    const int token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSIFlipTransaction,callbackXE->orgDSITransaction[0])(fb,transaction,options,params);
    endDSINativeWriter(token);return result;
}

uint32_t XE::wrapDSIGammaTransaction(IOService *fb,void *transaction,void *params) {
    const int token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSIGammaTransaction,callbackXE->orgDSITransaction[1])(fb,transaction,params);
    endDSINativeWriter(token);return result;
}

uint32_t XE::wrapDSICscTransaction(IOService *fb,void *transaction,void *matrix,uint64_t options,bool final) {
    const int token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSICscTransaction,callbackXE->orgDSITransaction[2])(fb,transaction,matrix,options,final);
    endDSINativeWriter(token);return result;
}

uint32_t XE::wrapDSIPlaneGammaTransaction(IOService *fb,void *transaction,bool post) {
    const int token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSIPlaneGammaTransaction,callbackXE->orgDSITransaction[3])(fb,transaction,post);
    endDSINativeWriter(token);return result;
}

uint32_t XE::wrapDSISetGammaTable(IOService *fb,uint32_t channels,uint32_t count,uint32_t bits,void *data,bool sync) {
    const int token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
    const auto result=FunctionCast(wrapDSISetGammaTable,callbackXE->orgDSITransaction[4])(fb,channels,count,bits,data,sync);
    endDSINativeWriter(token);return result;
}

// Power/backlight use DSI owners. Block only the donor branches that enter
// unsupported native transport controls; let ordinary OS attributes reach
// their native implementations under the existing writer lifetime.
uint32_t XE::wrapDSISetAttribute(IOService *fb,uint32_t attribute,uintptr_t value) {
    recordDSIClient(fb,7,attribute,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        int token=0;
        if(callbackXE->adlnOutputWiring.dsi) {
            if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
            DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
            if(dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb))) {
                if(attribute==0x706F7772 || attribute==0x70777273) { // powr / IOGraphics pwrs relay.
                    if(value>UINT32_MAX)return kIOReturnUnsupported;
                    return wrapDSISetPowerState(fb,static_cast<uint32_t>(value));
                }
                if(attribute==0x626B7376)return callbackXE->saveDSIBrightness(fb,value);
                if(XELP::DSI::AppleAdapter::transportAttribute(attribute))return kIOReturnUnsupported;
                token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
            }
        }
        const auto result=FunctionCast(wrapDSISetAttribute,callbackXE->orgDSIAttributes[0])(fb,attribute,value);
        endDSINativeWriter(token);return result;
    }();
    recordDSIClient(fb,7,attribute,result);
    recordDSIAttribute(fb,7,attribute,value,result);
    return result;
}

// Bounded CPU-only history: the last attribute alone hides earlier rejected UI
// requests behind a successful gamma flush. No MMIO or object pointers are exposed.
void XE::recordDSIAttribute(IOService *fb,unsigned event,uint32_t attribute,uintptr_t value,uint32_t result) {
    auto s=callbackXE;auto &c=display13FBPower;
    if(!s->adlnOutputWiring.dsi || !dsiModeTarget(fb) || !c.lock)return;
    DSIModeScope scope(fb);if(!scope.lock)return;
    static uint64_t history[2+16*8] {1,0};
    IOLockLock(c.lock);
    auto controller=Display13FBMemory::field<IOService *>(fb,0x1D0);
    if(controller && controller==c.controller && controller==s->adlnTopologyController) {
        const auto sequence=++history[1];auto row=history+2+((sequence-1)%16)*8;
        row[0]=sequence;row[1]=event;row[2]=attribute;row[3]=value<=UINT32_MAX?value:UINT64_MAX;row[4]=result;
        row[5]=Display13FBMemory::field<uint32_t>(controller,0xE58);
        row[6]=Display13FBMemory::field<uint32_t>(controller,0xE5C);
        row[7]=Display13FBMemory::field<uint32_t>(fb,0x44E0);
    }
    IOLockUnlock(c.lock);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIAttributeHistory",history,sizeof(history));
}

void XE::publishDSIBacklight() {
    auto &c=display13FBPower;if(!c.lock)return;
    IOLockLock(c.lock);const auto &b=dsiBacklight;
    uint64_t receipt[]={1,b.initialized,b.faulted,b.requested,b.applied,b.pwm.period,b.minimum,
        b.control,b.requests,b.writes,dsiRuntimePower.sleeping};
    IOLockUnlock(c.lock);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIBacklight",receipt,sizeof(receipt));
}

// Exact donor setAttribute('bksv') stores its 32-bit value at controller+E5C.
// This is user preference bookkeeping, not a hardware brightness command.
uint32_t XE::saveDSIBrightness(IOService *fb,uintptr_t value) {
    auto &c=display13FBPower;
    if(value>UINT32_MAX)return kIOReturnBadArgument;
    if(!c.lock || !dsiModeTarget(fb))return kIOReturnNotReady;
    IOLockLock(c.lock);
    const bool owned=dsiResourcesReady && c.controller && c.controller==adlnTopologyController &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==c.controller && c.pipe[0].memoryFramebuffer==fb;
    if(owned)*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(c.controller)+0xE5C)=static_cast<uint32_t>(value);
    IOLockUnlock(c.lock);return owned?kIOReturnSuccess:kIOReturnNotReady;
}

// The caller holds the same framebuffer mutex as stop/resume. Deferred requests
// update only software; the panel-on sequence applies PWM before its enable GPIO.
uint32_t XE::setDSIBrightness(IOService *fb,uintptr_t value) {
    auto &c=display13FBPower;
    if(value>XELP::DSI::Backlight::Maximum)return kIOReturnBadArgument;
    if(!c.lock || !dsiModeTarget(fb))return kIOReturnNotReady;
    const bool awake=dsiNativeUpdateAllowed(fb);
    IOLockLock(c.lock);
    const bool owned=dsiResourcesReady && dsiBacklight.initialized && !dsiRuntimePower.failed &&
        !dsiBacklight.faulted && !c.memoryFaulted && !c.clockFaulted && !c.watermarkFaulted &&
        !c.bridge.state.faulted && !c.bridge.state.suspended && !c.dbuf.faulted &&
        c.pipe[0].memoryFramebuffer==fb && !c.pipe[0].destroyingPath &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==c.controller &&
        c.controller==adlnTopologyController && adlnWriterMappingBound();
    const bool deferred=owned && dsiPowerTest && dsiRuntimePower.sleeping && dsiStopAttempted &&
        dsiStop.complete() && dsiPanelStop.complete && dsiIRQParked && !dsiMemoryReleased && !dsiDDIReleased;
    struct IO:Display13FBBootIO {
        bool owned;IO(IOMemoryMap *m,bool o):Display13FBBootIO{m},owned(o){}
        bool ready(){return owned;}
        void write(uint32_t reg,uint32_t value) {
            if(owned && reg==XELP::DSI::Backlight::Duty && readable(reg))
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;
        }
    } io{c.map,owned && awake && !dsiStopAttempted};
    const bool accepted=owned && (deferred || (io.ready() && c.bridge.outputHeld(io,0,0))) && dsiBacklight.request(value);
    const bool complete=accepted && (deferred || dsiBacklight.apply(io));
    // The donor getter returns this 16-bit requested value with range 0..65535.
    if(complete)*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(c.controller)+0xE58)=dsiBacklight.requested;
    IOLockUnlock(c.lock);publishDSIBacklight();
    return complete?kIOReturnSuccess:kIOReturnNotReady;
}

uint32_t XE::wrapDSISetConnectionAttribute(IOService *fb,int32_t connection,uint32_t attribute,uintptr_t value) {
    recordDSIClient(fb,8,attribute,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        int token=0;
        if(callbackXE->adlnOutputWiring.dsi) {
            if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
            DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
            if(dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb))) {
                if(connection!=0)return kIOReturnUnsupported;
                if(attribute==0x626B6C74)return callbackXE->setDSIBrightness(fb,value);
                if(XELP::DSI::AppleAdapter::transportConnectionAttribute(attribute))return kIOReturnUnsupported;
                token=beginDSIFramebufferWriter(fb);if(token<0)return kIOReturnNotReady;
            }
        }
        const auto result=FunctionCast(wrapDSISetConnectionAttribute,callbackXE->orgDSIAttributes[1])(fb,connection,attribute,value);
        endDSINativeWriter(token);return result;
    }();
    recordDSIClient(fb,8,attribute,result);
    recordDSIAttribute(fb,8,attribute,value,result);
    return result;
}

uint32_t XE::wrapDSISetPowerState(IOService *fb,uint32_t state) {
    recordDSIClient(fb,9,state,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(callbackXE->adlnOutputWiring.dsi) {
            if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
            DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
            if(dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb))) {
                if(callbackXE->dsiPowerTest && state<=2 && dsiModeTarget(fb)) {
                    auto controller=Display13FBMemory::field<IOService *>(fb,0x1D0);
                    auto path=Display13FBMemory::singlePath<OSObject *>(fb);
                    auto &power=callbackXE->dsiRuntimePower;
                    const bool ready=!power.failed && (state==2 ? (power.sleeping ? callbackXE->resumeDSIPath(controller,fb,path) : dsiNativeUpdateAllowed(fb)) :
                        (power.sleeping ? true : (dsiNativeUpdateAllowed(fb) && callbackXE->quiesceDSIPath(controller,fb,path,true))));
                    if(!ready)return kIOReturnNotReady;
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
        return FunctionCast(wrapDSISetPowerState,callbackXE->orgDSIAttributes[2])(fb,state);
    }();
    recordDSIClient(fb,9,state,result);
    return result;
}

uint32_t XE::wrapDSISetStream(IOService *controller,IOService *fb,OSObject *path,const void *timing) {
    if(callbackXE->adlnOutputWiring.dsi) {
        if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
        DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
        if(dsiLifecycleTarget(fb,path))return kIOReturnNotReady;
    }
    return FunctionCast(wrapDSISetStream,callbackXE->orgDSIAttributes[3])(controller,fb,path,timing);
}

bool XE::processDSINativeAttributes(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
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
    // Standard routes copy 17/17/16/20-byte prologues, before RIP counters.
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSISetAttribute,orgDSIAttributes[0]},
        {symbols[1],wrapDSISetConnectionAttribute,orgDSIAttributes[1]},
        {symbols[2],wrapDSISetPowerState,orgDSIAttributes[2]},
        {symbols[3],wrapDSISetStream,orgDSIAttributes[3]}};
    const bool ready=patcher.routeMultiple(index,routes,arrsize(routes),address,size);patcher.clearError();return ready;
}

bool XE::processDSINativeTransactions(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN21AppleIntelFramebuffer22PerformFlipTransactionEP30IOAccelDisplayPipeTransaction2yP21FlipTransactionParams",
        "__ZN21AppleIntelFramebuffer23PerformGammaTransactionEP30IOAccelDisplayPipeTransaction2P20GammaTransactionArgs",
        "__ZN21AppleIntelFramebuffer21PerformCscTransactionEP30IOAccelDisplayPipeTransaction2P29IOAccelDisplayPipeColorMatrixyb",
        "__ZN21AppleIntelFramebuffer28PerformPlaneGammaTransactionEP30IOAccelDisplayPipeTransaction2b",
        "__ZN21AppleIntelFramebuffer13setGammaTableEjjjPvb"};
    const uint32_t offsets[]={0x2B602,0x2C800,0x2D03C,0x2D838,0x36E72};
    // Whole prologues; no relocated RIP-relative counter in these 17 bytes.
    const uint8_t entries[][17]={
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x48},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x38},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x38},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x68},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x58}};
    if(!display13FramebufferRequested || size<0x36E83)return false;
    for(unsigned i=0;i<5;++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(fn!=address+offsets[i] || memcmp(reinterpret_cast<void *>(fn),entries[i],17))return false;
    }
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSIFlipTransaction,orgDSITransaction[0]},
        {symbols[1],wrapDSIGammaTransaction,orgDSITransaction[1]},
        {symbols[2],wrapDSICscTransaction,orgDSITransaction[2]},
        {symbols[3],wrapDSIPlaneGammaTransaction,orgDSITransaction[3]},
        {symbols[4],wrapDSISetGammaTable,orgDSITransaction[4]}};
    const bool ready=patcher.routeMultiple(index,routes,arrsize(routes),address,size);patcher.clearError();return ready;
}

bool XE::processDSINativeWriters(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN31AppleIntelFramebufferController16hwSetCursorImageEP21AppleIntelFramebufferP20IOHardwareCursorInfo",
        "__ZN31AppleIntelFramebufferController16hwSetCursorStateEP21AppleIntelFramebufferiibb"};
    const uint32_t offsets[]={0x558F6,0x55DA8};
    const uint8_t entries[][20]={
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x28,0x49,0x89,0xD4},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC,0x38,0x45,0x89,0xCE}};
    if(!display13FramebufferRequested || size<=0x55DBC)return false;
    for(unsigned i=0;i<2;++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(fn!=address+offsets[i] || memcmp(reinterpret_cast<void *>(fn),entries[i],20))return false;
    }
    KernelPatcher::RouteRequest routes[]={
        {symbols[0],wrapDSICursorImage,orgDSICursor[0]},
        {symbols[1],wrapDSICursorState,orgDSICursor[1]}};
    const bool ready=patcher.routeMultiple(index,routes,arrsize(routes),address,size);patcher.clearError();return ready;
}

// The whole freeDisplayPathResources body also touches global audio and can
// reset a normal DDI port. Keep only its audited CPU leaves for the DSI path.
bool XE::processDSIPathDisposal(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN31AppleIntelFramebufferController9freePlaneEP15AppleIntelPlane",
        "__ZN31AppleIntelFramebufferController10freeScalerEP16AppleIntelScaler",
        "__ZN21AppleIntelFramebuffer17detachDisplayPathEP21AppleIntelDisplayPath",
        "__ZN21AppleIntelFramebuffer17removeDisplayPathEP21AppleIntelDisplayPath"};
    const uint32_t offsets[]={0x5751C,0x5739A,0x67FB4,0x68EA4};
    const uint8_t entries[][16]={
        {0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0xC9,0x96,0x08,0x00,0x48,0x85,0xF6,0x74,0x50},
        {0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0xEB,0x97,0x08,0x00,0x48,0x85,0xF6,0x74,0x20},
        {0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0x19,0xC5,0x07,0x00,0x48,0x8B,0x05,0x0A,0xC5},
        {0x55,0x48,0x89,0xE5,0x41,0x56,0x53,0x48,0xFF,0x05,0xEE,0xB8,0x07,0x00,0x48,0x8B}};
    if(size<=0x138BB8)return false;
    mach_vm_address_t functions[4] {};
    for(unsigned i=0;i<4;++i) {
        functions[i]=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(functions[i]!=address+offsets[i] || memcmp(reinterpret_cast<void *>(functions[i]),entries[i],16))return false;
    }
    const auto map=patcher.solveSymbol<mach_vm_address_t>(index,"__ZN31AppleIntelFramebufferController12fPipeToFBMapE",address,size);
    patcher.clearError();if(map!=address+0x138BB0)return false;
    dsiFreePlane=functions[0];dsiFreeScaler=functions[1];dsiDetachPath=functions[2];dsiRemovePath=functions[3];dsiPipeMap=map;
    return true;
}

// Caller retains FB/path, owns its recursive mutex, and has detached its DDB
// reservation. Gate ownership excludes queued native work across PG put and
// CPU disposal. A failed put leaves the native path arrays and objects intact.
bool XE::disposeDSIPath(IOService *fb,OSObject *path) {
    auto &c=display13FBPower;
    if(!c.lock || !fb || !path || fb->getMetaClass()->getClassSize()<0x4B90 ||
       path->getMetaClass()->getClassSize()<0x3730 || !dsiFreePlane || !dsiFreeScaler ||
       !dsiDetachPath || !dsiRemovePath || !dsiPipeMap ||
       !adlnWorkLoopInGate || !adlnWorkLoopTryClose || !adlnWorkLoopOpen)return false;
    IOLockLock(c.lock);auto loop=c.workLoop;if(loop)loop->retain();IOLockUnlock(c.lock);
    if(!loop)return false;
    const bool inGate=reinterpret_cast<bool (*)(const IOWorkLoop *)>(adlnWorkLoopInGate)(loop);
    const bool acquired=!inGate && reinterpret_cast<bool (*)(IOWorkLoop *)>(adlnWorkLoopTryClose)(loop);
    if(!inGate && !acquired){loop->release();return false;}
    uint64_t record[14] {1};IOLockLock(c.lock);
    auto controller=static_cast<IOService *>(c.controller);
    auto port=c.dsiPort;
    auto plane=Display13FBMemory::field<OSObject *>(path,0x32C8);
    auto scaler=Display13FBMemory::field<OSObject *>(path,0x32C0);
    auto planeScaler=plane && plane->getMetaClass()->getClassSize()>=0x1C8 ? Display13FBMemory::field<OSObject *>(plane,0x88) : nullptr;
    auto pipeMap=*reinterpret_cast<IOService ***>(dsiPipeMap);
    const auto scalerOwned=[&](OSObject *o) {
        return !o || (o->getMetaClass()->getClassSize()>=0x58 &&
            Display13FBMemory::field<IOService *>(o,0x10)==fb && Display13FBMemory::field<uint8_t>(o,0x20)==1 &&
            Display13FBMemory::field<uint32_t>(o,0x18)==0);
    };
    bool planeTailEmpty=true;for(unsigned i=1;i<5;++i)if(Display13FBMemory::field<uintptr_t>(path,0x32C8+8*i))planeTailEmpty=false;
    const auto attached=Display13FBMemory::field<uint32_t>(fb,0x4A00);
    const auto irqIdle=[&]() {
        auto &irq=display13FBIRQ;
        if(!controller || controller->getMetaClass()->getClassSize()<0x1C20 || !adlnIRQOwner(controller) || !irq.lock)return false;
        const auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
        const bool idle=!irq.state.faulted && !irq.quiescing && !irq.blockedPipes &&
            !(irq.state.active&1) && !(irq.admittedPipes&1) && irq.scopes.total==irq.handlers &&
            !irq.scopes.display && !irq.softwareHandlers &&
            !(__atomic_load_n(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+0xE08),__ATOMIC_ACQUIRE)&Display13FBIRQ::pipeEvents(0)) &&
            !__atomic_load_n(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+0xE0C),__ATOMIC_ACQUIRE) &&
            !(__atomic_load_n(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+0xE10),__ATOMIC_ACQUIRE)&3U);
        IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);return idle;
    };
    const bool irqBefore=irqIdle();record[12]=irqBefore;
    const bool ready=irqBefore && !c.dsiNativeWriters && adlnOutputWiring.dsi==1 && controller && controller==adlnTopologyController &&
        controller->getMetaClass()->getClassSize()>=0x1C20 && c.workLoop==loop &&
        Display13FBMemory::field<IOWorkLoop *>(controller,0xE00)==loop &&
        Display13FBMemory::field<IOService *>(fb,0x1D0)==controller &&
        Display13FBMemory::field<IOService *>(path,0x32B0)==fb &&
        Display13FBMemory::field<uint32_t>(path,0x3640)==0 && Display13FBMemory::field<uint32_t>(path,0x3644)==1 &&
        c.pipe[0].memoryFramebuffer==fb && c.pipe[0].destroyingPath && !c.pipe[0].publishedScanout && !c.pipe[0].scanout.record &&
        !c.memoryFaulted && !c.clockFaulted && !c.bridge.state.faulted && !c.bridge.state.suspended && !c.dbuf.faulted &&
        !(c.dbuf.selected&1) && !c.dbuf.pipe[0].consumers && !c.dbuf.pipe[0].unretired &&
        dsiStop.complete() && dsiClockStop.complete() && dsiIRQParked && dsiDDIReleased && dsiMemoryReleased && !dsiPGReleased &&
        c.bridge.bootAdopted && c.bridge.bootPath==reinterpret_cast<uintptr_t>(path) && c.bridge.bootPort==0 &&
        !c.bridge.bootPGProtected && c.bridge.pgRefs[0]==1 && !c.bridge.ddiRefs[0] &&
        Display13FBMemory::field<uint8_t>(fb,0x4288)==0xFF && !Display13FBMemory::field<uintptr_t>(fb,0x4348) &&
        port && port->getMetaClass()->getClassSize()>=0x580 && Display13FBMemory::field<OSObject *>(path,0x3648)==port &&
        Display13FBMemory::field<uint32_t>(port,0x550)==1 && Display13FBMemory::field<uint32_t>(port,0x554)<=3 &&
        !Display13FBMemory::field<uintptr_t>(path,0x32B8) && planeTailEmpty &&
        plane && plane->getMetaClass()->getClassSize()>=0x1C8 && Display13FBMemory::field<IOService *>(plane,0x68)==fb &&
        Display13FBMemory::field<OSObject *>(plane,0x70)==path && Display13FBMemory::field<uint32_t>(plane,0x78)==0 &&
        scalerOwned(scaler) && scalerOwned(planeScaler) &&
        Display13FBMemory::field<uint32_t>(fb,0x49E8)==1 && Display13FBMemory::field<OSObject *>(fb,0x49F0)==path &&
        attached<=1 && Display13FBMemory::field<OSObject *>(fb,0x4A08)==(attached ? path : nullptr) && pipeMap && pipeMap[0]==fb;
    record[1]=ready;record[2]=c.bridge.pgRefs[0];record[3]=c.dbuf.selected;record[4]=attached;
    auto power=c.power;IOLockUnlock(c.lock);
    const bool put=ready && power && adlnPowerReference(power,2,1);
    IOLockLock(c.lock);
    if(put)dsiPGReleased=c.power==power && c.controller==controller && !c.bridge.state.faulted &&
        !c.bridge.pgRefs[0] && !c.bridge.pgLease[0].owner;
    record[5]=put;record[6]=dsiPGReleased;record[7]=c.bridge.pgRefs[0];
    bool removed=false;const bool irqAfter=put && irqIdle();record[13]=irqAfter;
    if(irqAfter && dsiPGReleased && !c.memoryFaulted && !c.dbuf.faulted && !c.clockFaulted &&
       c.pipe[0].memoryFramebuffer==fb && *reinterpret_cast<IOService ***>(dsiPipeMap)==pipeMap && pipeMap[0]==fb) {
        // These leaves only update CPU objects; no power or MMIO callback.
        reinterpret_cast<void (*)(IOService *,OSObject *)>(dsiFreePlane)(controller,plane);
        reinterpret_cast<void (*)(IOService *,OSObject *)>(dsiFreeScaler)(controller,scaler);
        if(Display13FBMemory::field<uint32_t>(port,0x554)==3)
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(port)+0x554)=2;
        // The normal setPortMode(0) could reset DDI. Hardware is already stopped.
        if(!Display13FBMemory::field<uint32_t>(port,0x554))
            *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(port)+0x550)=0;
        *reinterpret_cast<OSObject **>(reinterpret_cast<uint8_t *>(path)+0x3648)=nullptr;
        reinterpret_cast<void (*)(IOService *,OSObject *)>(dsiDetachPath)(fb,path);
        reinterpret_cast<void (*)(IOService *,OSObject *)>(dsiRemovePath)(fb,path);
        const auto freedScaler=[](OSObject *o) {return !o || (!Display13FBMemory::field<uintptr_t>(o,0x10) && !Display13FBMemory::field<uint8_t>(o,0x20));};
        removed=!Display13FBMemory::field<uint32_t>(fb,0x49E8) && !Display13FBMemory::field<uintptr_t>(fb,0x49F0) &&
            !Display13FBMemory::field<uint32_t>(fb,0x4A00) && !Display13FBMemory::field<uintptr_t>(fb,0x4A08) && !pipeMap[0] &&
            !Display13FBMemory::field<uint32_t>(path,0x3644) && !Display13FBMemory::field<uintptr_t>(path,0x32B0) &&
            !Display13FBMemory::field<uintptr_t>(path,0x3648) && !Display13FBMemory::field<uintptr_t>(plane,0x68) &&
            freedScaler(scaler) && freedScaler(planeScaler);
    }
    record[8]=removed;record[9]=Display13FBMemory::field<uint32_t>(fb,0x49E8);
    record[10]=Display13FBMemory::field<uint32_t>(fb,0x4A00);record[11]=Display13FBMemory::field<uint32_t>(path,0x3644);
    IOLockUnlock(c.lock);
    if(removed) {
        removed=releaseDSIFixedMode(fb);
        if(removed) {
            IOLockLock(c.lock);
            removed=c.dsiPort==port;
            if(removed)c.dsiPort=nullptr;
            IOLockUnlock(c.lock);
            if(removed)port->release();
        }
    }
    record[8]=removed;
    if(acquired)reinterpret_cast<void (*)(IOWorkLoop *)>(adlnWorkLoopOpen)(loop);
    loop->release();adlnPublishMemory("N97FBDSIPathDispose",0,record,sizeof(record));return removed;
}

// Host/IRQ/PLL retirement; the native DDI reference follows the stopped clock.
// Keep the real stopped source separate from Session cleanup: it is not a full
// Session StopProof. The one-shot journal retains a partial stop for diagnosis.
bool XE::quiesceDSIPath(IOService *controller,IOService *fb,OSObject *path,bool retain) {
    auto &c=display13FBPower;auto &clock=comboClockTransaction;auto &irq=display13FBIRQ;
    if(!c.lock || !clock.lock || !controller || controller->getMetaClass()->getClassSize()<0x1C20 ||
       !fb || fb->getMetaClass()->getClassSize()<0x4B90 || !path || path->getMetaClass()->getClassSize()<0x3730 ||
       !adlnWorkLoopInGate || !adlnWorkLoopTryClose || !adlnWorkLoopOpen)return false;
    auto mutex=Display13FBMemory::field<IORecursiveLock *>(fb,0x4220);if(!mutex)return false;
    IORecursiveLockLock(mutex);
    IOLockLock(c.lock);
    auto loop=controller==c.controller ? c.workLoop : nullptr;
    if(loop)loop->retain();
    IOLockUnlock(c.lock);
    const bool boundLoop=loop && Display13FBMemory::field<IOWorkLoop *>(controller,0xE00)==loop;
    const bool inGate=boundLoop && reinterpret_cast<bool (*)(const IOWorkLoop *)>(adlnWorkLoopInGate)(loop);
    const bool acquired=boundLoop && !inGate && reinterpret_cast<bool (*)(IOWorkLoop *)>(adlnWorkLoopTryClose)(loop);
    if(!inGate && !acquired) {if(loop)loop->release();IORecursiveLockUnlock(mutex);return false;}
    IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
    uint64_t record[51] {6};record[38]=c.dsiNativeWriters;bool masked=false,maskOK=false,parked=false,restored=false,enabled=false,modeWithdrawn=false;uint32_t master=0;
    const auto qualified=[&]() {
        if(adlnOutputWiring.dsi!=1 || !dsiResourcesReady || !dsiNativeWriterHooksReady || c.dsiNativeWriters || (dsiPendingTarget(fb) && !dsiArmedBackingOwned(fb,path)) || !dsiProfile.valid() ||
           controller!=c.controller || controller!=adlnTopologyController || c.workLoop!=loop ||
           Display13FBMemory::field<IOWorkLoop *>(controller,0xE00)!=loop ||
           c.pipe[0].memoryFramebuffer!=fb || Display13FBMemory::singlePath<OSObject *>(fb)!=path ||
           c.transcoders.dsi0Pipe!=0 || c.bridge.state.faulted || c.bridge.state.suspended || c.clockFaulted ||
           c.pipe[0].destroyingPath || c.dbuf.updating || c.pipe[0].wmProgramming || c.pipe[0].wmCommitting ||
           Display13FBMemory::field<uint32_t>(path,0x35F0) || !adlnWriterMappingBound() ||
           !c.dsiPort || Display13FBMemory::field<OSObject *>(path,0x3648)!=c.dsiPort ||
           Display13FBMemory::field<uint32_t>(c.dsiPort,0x550)!=1 ||
           Display13FBMemory::field<uint32_t>(path,0x3644)!=2 ||
           clock.thread || clock.allocation.external[0]!=reinterpret_cast<uintptr_t>(controller) ||
           clock.allocation.binding[0]!=dsiBoot.source.identity.pll+1 ||
           !c.bootDSI.sameSource(dsiBoot.source))return false;
        const auto binding=adlnInspectPathBinding(fb,path,adlnPlatform,adlnOutputWiring);
        Display13FBBootIO io{c.map};
        return binding.status==TGLFBBinding::Status::Complete && binding.pipe==0 && binding.ddi==0 &&
            c.bridge.outputHeld(io,0,0);
    };
    struct IO : Display13FBBootIO {
        decltype(qualified) &owns;IOSimpleLock *irqLock;bool panel,retain;
        IO(IOMemoryMap *m,decltype(qualified) &q,IOSimpleLock *l,bool p,bool r):Display13FBBootIO{m},owns(q),irqLock(l),panel(p),retain(r) {}
        bool ready(){return owns();}
        void write(uint32_t reg,uint32_t value) {
            if(readable(reg) && (reg==0x70080 || reg==0x70084 || reg==0x70088 || reg==0x70180 || reg==0x7019C ||
               reg==0x7B008 || reg==0x6B0D8 || reg==0x6B400 || reg==0x164280 || reg==0x64000 ||
               reg==0x46010+4*display13FBPower.bootDSI.identity.pll ||
               (retain && XELP::DSI::Restart::writable(reg,display13FBPower.bootDSI.identity.pll)) ||
               (panel && (reg==0x6B100 || reg==0x6B104 || reg==0xC4030 || reg==0xC7204 || reg==0xC7304))))
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+reg)=value;
        }
        bool gpio(unsigned role,unsigned value) {
            if(!panel || !irqLock)return false;
            const auto saved=IOSimpleLockLockDisableInterrupt(irqLock);
            const auto r=XELP::DSI::Panel::gpio(*this,role,value);
            IOSimpleLockUnlockEnableInterrupt(irqLock,saved);return r.complete;
        }
        void delayUs(unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
    } io{c.map,qualified,irq.lock,dsiStopTest || dsiPowerTest || retain,retain};
    auto &power=dsiRuntimePower;
    record[1]=qualified();
    bool snapshot=!retain;
    if(retain && record[1] && !dsiStopAttempted && !power.sleeping && !c.memoryFaulted && !c.dbuf.faulted && !c.watermarkFaulted &&
       dsiPublishedTarget(fb) && dsiPendingBacking.modeReady && dsiArmedBackingOwned(fb,path)) {
        auto &p=power.plane;
        p.control=io.read(0x70180);p.surface=io.read(0x7019C);p.stride=io.read(0x70188);p.size=io.read(0x70190);
        p.position=io.read(0x7018C);p.offset=io.read(0x701A4);p.color=io.read(0x701CC);
        power.cursorControl=io.read(0x70080);power.cursorPosition=io.read(0x70088);power.cursorBase=io.read(0x70084);
        // The published aperture is not the current native flip surface.
        // As in NativeCache::readActive, retain the native mode/display pipe
        // and its transaction ownership; do not free/rebind it or substitute
        // the initial backing. Writers are excluded through wake. Restore only
        // the latched native plane image, with the supported fixed geometry.
        snapshot=XELP::DSI::Surface::shape(p,dsiProfile) && XELP::DSI::Surface::matches(io,p,true) &&
            power.cursorControl!=UINT32_MAX && power.cursorBase!=UINT32_MAX && power.cursorPosition!=UINT32_MAX &&
            Display13Cursor::controlWrite(0x70080,power.cursorControl)==power.cursorControl;
        if(snapshot){power.host=XELP::DSI::Restart::State{};power.panel=XELP::DSI::Panel::Result{};dsiPanelStop={};dsiStop={};}
    }
    record[1]=record[1] && snapshot;
    if(record[1] && !dsiStopAttempted && adlnIRQOwner(controller) && irq.lock &&
       irq.lock==Display13FBMemory::field<IOSimpleLock *>(controller,0x90) && irq.base==c.map->getVirtualAddress()) {
        Display13FBIRQIO irqIO{irq.base};
        auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
        const auto queued=[&](unsigned offset) {
            return __atomic_load_n(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+offset),__ATOMIC_ACQUIRE);
        };
        master=irqIO.read(Display13FBIRQ::Master);enabled=irq.state.enabled;
        record[2]=irq.handlers;record[3]=irq.scopes.display;record[4]=irq.softwareHandlers;
        record[5]=queued(0xE08);record[6]=queued(0xE0C);record[7]=queued(0xE10);
        const bool quiet=irq.scopes.total==irq.handlers && !irq.scopes.display && !irq.softwareHandlers &&
            !(record[5]&Display13FBIRQ::pipeEvents(0)) && !record[6] && !(record[7]&3);
        const bool ready=quiet && !irq.quiescing && !irq.blockedPipes && !irq.state.faulted &&
            irq.state.begun && (irq.state.configured&1) && (irq.admittedPipes&1) && irq.state.transcoders.dsi0Pipe==0 &&
            master!=UINT32_MAX && bool(master&Display13FBIRQ::Enable)==enabled;
        if(ready) {
            // DE permission is assigned on actual HW-handler entry. New shared
            // GT-only handlers can still run but cannot consume display banks.
            irq.quiescing=true;irq.blockedPipes=1;irq.state.enabled=false;masked=true;
            irqIO.write(Display13FBIRQ::Master,0);
            const auto after=irqIO.read(Display13FBIRQ::Master);
            maskOK=after!=UINT32_MAX && !(after&Display13FBIRQ::Enable);
            if(!maskOK)irq.state.faulted=true;
        }
        IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
        if(maskOK) {
            dsiStopAttempted=true;
            if(!retain)dsiPanelStop={};
            if(dsiStopTest || dsiPowerTest || retain)dsiStop=XELP::DSI::Quiesce::run(io,c.bootDSI,dsiProfile,dsiPanelStop);
            else dsiStop=XELP::DSI::Quiesce::run(io,c.bootDSI,dsiProfile);
        }
        if(masked) {
            saved=IOSimpleLockLockDisableInterrupt(irq.lock);
            record[8]=irq.handlers;record[9]=irq.scopes.display;record[10]=irq.softwareHandlers;
            record[11]=queued(0xE08);record[12]=queued(0xE0C);record[13]=queued(0xE10);
            if(dsiStop.complete() && irq.scopes.total==irq.handlers && !irq.scopes.display &&
               !irq.softwareHandlers && !record[12] && !(record[13]&3)) {
                // Pass the real DE-permitted scope count. A GT-only stack does
                // not own a display consumer, even when it spans this stop.
                const auto status=Display13FBIRQ::parkStoppedPipe(irqIO,irq.state,0,irq.scopes.display,
                    irq.softwareHandlers,static_cast<uint32_t>(record[11]));
                record[14]=static_cast<uint32_t>(status);parked=status==Display13FBIRQ::Status::Complete;
            }
            IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
            if(parked) {
                if(retain)XELP::DSI::Restart::stopQuiesced(io,c.bootDSI,dsiProfile,dsiStop,clock.allocation,
                    reinterpret_cast<uintptr_t>(controller),power.host);
                else dsiClockStop=XELP::DSI::Clock::retire(io,c.bootDSI,dsiStop,true,clock.allocation,
                    reinterpret_cast<uintptr_t>(controller));
            }
            saved=IOSimpleLockLockDisableInterrupt(irq.lock);
            if(!retain && dsiClockStop.complete()) {
                // The host and its PLL are now stopped. Withdraw both maps at
                // the same masked IRQ boundary, so an external clock client
                // no longer tries to re-adopt the obsolete running DSI source.
                c.transcoders.dsi0Pipe=UINT32_MAX;irq.state.transcoders=c.transcoders;c.dbuf.transcoders=c.transcoders;
                __atomic_fetch_and(&irq.admittedPipes,~1U,__ATOMIC_RELEASE);
                __atomic_store_n(&dsiResourcesReady,false,__ATOMIC_RELEASE);
            }
            irq.quiescing=false;irq.blockedPipes=0;
            const auto wells=irqIO.read(Display13FBIRQ::Driver);
            if(wells==UINT32_MAX || (wells&3)!=3)irq.state.faulted=true;
            if(!irq.state.faulted) {
                irqIO.write(Display13FBIRQ::Master,master&Display13FBIRQ::Enable);
                const auto after=irqIO.read(Display13FBIRQ::Master);
                restored=after!=UINT32_MAX && bool(after&Display13FBIRQ::Enable)==enabled;
                irq.state.enabled=restored && enabled;
                if(!restored) {irq.state.faulted=true;irqIO.write(Display13FBIRQ::Master,0);}
            }
            adlnIRQCache(controller,irqIO);IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
        }
    }
    if(masked)dsiIRQParked=parked && restored;
    record[15]=masked;record[16]=parked;record[17]=restored;record[18]=dsiStopAttempted;
    record[19]=static_cast<uint32_t>(dsiStop.stage);record[20]=static_cast<uint32_t>(dsiStop.error);
    record[21]=dsiStop.writes;record[22]=dsiStop.waitUs;record[23]=dsiIRQParked;
    record[24]=static_cast<uint32_t>(dsiClockStop.status);record[25]=dsiClockStop.reg;record[26]=dsiClockStop.after;
    record[27]=dsiClockStop.writes;record[28]=dsiClockStop.waitUs;record[29]=dsiClockStop.complete();
    // The donor's disableClocks(port,peer) puts this DDI reference at 6db04;
    // PG1 belongs to freeDisplayPathResources and must remain until memory /
    // DDB / path destruction. Match that ownership without running DDI/DP code.
    const bool stopped=!retain && parked && restored && dsiClockStop.complete();
    record[30]=c.bridge.ddiRefs[0];
    if(stopped && c.bridge.bootAdopted && c.bridge.bootPath==reinterpret_cast<uintptr_t>(path) &&
       c.bridge.bootPort==0 && !c.bridge.bootDDIProtected && c.bridge.ddiRefs[0]==1 && c.power) {
        auto power=c.power;
        IOLockUnlock(c.lock);const bool put=adlnPowerReference(power,4,0);IOLockLock(c.lock);
        dsiDDIReleased=put && c.power==power && !c.bridge.ddiRefs[0] && !c.bridge.ddiLease[0].owner;
    }
    record[31]=c.bridge.ddiRefs[0];record[32]=dsiDDIReleased;
    const bool stoppedPath=stopped && dsiDDIReleased;
    if(stoppedPath && !c.memoryFaulted && !c.dbuf.faulted) {
        // Keep the real FB mutex and workloop gate while the shared native
        // retirement path acquires its own allocator serializer. Neither the
        // Combo mutex nor the owner lock may span native allocator callbacks.
        // PG1 remains held for PTE/DDB validation; the stopped DSI PLL is free.
        IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
        const bool released=retireDSINativeBacking(fb,path) && adlnFreeStoppedPipe(fb);
        IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
        Display13FBDBUFIO dbufIO{c.map};
        // A native no-resource return can precede path destruction. Retire
        // that path's DDB consumers too, even if no allocator call was needed.
        const auto dbufStatus=released && !c.memoryFaulted ? Display13DBUF::stopPipe(dbufIO,c.dbuf,0) : Display13DBUF::Status::Unavailable;
        record[35]=released;record[36]=static_cast<uint32_t>(dbufStatus);
        const bool dbufStopped=dbufStatus==Display13DBUF::Status::Complete;
        bool irqRestored=false;
        if(released && irq.controller==controller && irq.lock && irq.base) {
            Display13FBIRQIO irqIO{irq.base};const auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
            const auto wells=irqIO.read(Display13FBIRQ::Driver);
            const bool powered=wells!=UINT32_MAX && (wells&3)==3;
            const auto after=powered ? irqIO.read(Display13FBIRQ::Master) : UINT32_MAX;
            irqRestored=!irq.state.faulted && !irq.quiescing && !irq.blockedPipes && !(irq.state.active&1) &&
                after!=UINT32_MAX && bool(after&Display13FBIRQ::Enable)==irq.state.enabled;
            if(!irqRestored){irq.state.faulted=true;irq.state.enabled=false;if(powered)irqIO.write(Display13FBIRQ::Master,0);}
            adlnIRQCache(controller,irqIO);IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
        }
        record[37]=irqRestored;
        dsiMemoryReleased=dbufStopped && irqRestored && c.controller==controller && c.pipe[0].memoryFramebuffer==fb &&
            Display13FBMemory::singlePath<OSObject *>(fb)==path && !c.memoryFaulted &&
            Display13FBMemory::field<uint8_t>(fb,0x4288)==0xFF && !Display13FBMemory::field<uintptr_t>(fb,0x4348) &&
            !c.pipe[0].scanout.record && !__atomic_load_n(&c.pipe[0].publishedScanout,__ATOMIC_ACQUIRE) &&
            !c.dbuf.pipe[0].consumers && !c.dbuf.pipe[0].unretired;
    }
    record[39]=dsiStop.reg;record[40]=dsiStop.value;
    record[41]=dsiPanelStop.stage;record[42]=dsiPanelStop.complete;
    record[43]=static_cast<uint32_t>(dsiPanelStop.host.error);record[44]=dsiPanelStop.host.sequenceId;
    record[45]=dsiPanelStop.host.operationOffset;record[46]=dsiPanelStop.host.headers;
    record[47]=dsiPanelStop.host.payloadWords;record[48]=dsiPanelStop.host.completedOperations;
    record[49]=dsiPanelStop.host.waitedUs;record[50]=dsiPanelStop.host.pending;
    record[33]=dsiMemoryReleased;record[34]=c.memoryFaulted;
    const bool complete=retain ? (parked && restored && power.host.stopped && !power.host.started &&
        power.host.error==XELP::DSI::Restart::Error::None && dsiPanelStop.complete && qualified() &&
        dsiArmedBackingOwned(fb,path) && !c.memoryFaulted && !c.dbuf.faulted) : (stoppedPath && dsiMemoryReleased);
    if(retain && complete)power.sleeping=true;
    if(complete && !retain) {
        // Exact donor disableDisplay publishes path state 1 only after its
        // clock-disable call (4dbba -> 4dbc6). Mirror that CPU completion now,
        // never on a partial host/IRQ/PLL stop or by replaying the donor body.
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(path)+0x3644)=1;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(path)+0x170)=0;
        *reinterpret_cast<uint8_t *>(reinterpret_cast<uint8_t *>(fb)+0x4270)=0;
        if(dsiModeTarget(fb)){dsiFixedMode.ready=false;modeWithdrawn=true;}
    }
    // Native backing and the GOP pool have been retired before a full stop
    // leaves PG/path disposal to releaseDisplay13Path; only partial stops
    // need the global retention guard. Never clear a pre-existing real fault.
    if(masked && !complete){if(retain)power.failed=true;adlnFault(c.memoryFaulted,__LINE__);}
    IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
    if(modeWithdrawn)fb->setProperty("WEXDSIFixedModeReady",false);
    if(acquired)reinterpret_cast<void (*)(IOWorkLoop *)>(adlnWorkLoopOpen)(loop);
    loop->release();IORecursiveLockUnlock(mutex);
    adlnPublishMemory("N97FBDSIHostStop",0,record,sizeof(record));
    if(retain) {
        const auto &h=power.host;
        uint64_t receipt[] {1,0,complete,power.cycles,power.sleeping,snapshot,
            power.plane.control,power.plane.surface,power.cursorControl,power.cursorBase,
            static_cast<uint32_t>(h.stage),static_cast<uint32_t>(h.error),h.reg,h.value,h.writes,h.waitUs,
            parked,restored,dsiPanelStop.complete,dsiMemoryReleased,dsiDDIReleased};
        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIRuntimePower",receipt,sizeof(receipt));
    }
    return complete;
}

// Runtime display wake keeps the native scanout allocation and every display
// lease from the matching off transaction. System power loss is not covered.
bool XE::resumeDSIPath(IOService *controller,IOService *fb,OSObject *path) {
    auto &c=display13FBPower;auto &clock=comboClockTransaction;auto &irq=display13FBIRQ;auto &power=dsiRuntimePower;
    if(!c.lock || !clock.lock || !controller || controller->getMetaClass()->getClassSize()<0x1C20 ||
       !fb || fb->getMetaClass()->getClassSize()<0x4B90 || !path || path->getMetaClass()->getClassSize()<0x3730 ||
       !adlnWorkLoopInGate || !adlnWorkLoopTryClose || !adlnWorkLoopOpen)return false;
    auto mutex=Display13FBMemory::field<IORecursiveLock *>(fb,0x4220);if(!mutex)return false;
    IORecursiveLockLock(mutex);IOLockLock(c.lock);
    auto loop=controller==c.controller ? c.workLoop : nullptr;if(loop)loop->retain();IOLockUnlock(c.lock);
    const bool boundLoop=loop && Display13FBMemory::field<IOWorkLoop *>(controller,0xE00)==loop;
    const bool inGate=boundLoop && reinterpret_cast<bool (*)(const IOWorkLoop *)>(adlnWorkLoopInGate)(loop);
    const bool acquired=boundLoop && !inGate && reinterpret_cast<bool (*)(IOWorkLoop *)>(adlnWorkLoopTryClose)(loop);
    if(!inGate && !acquired){if(loop)loop->release();IORecursiveLockUnlock(mutex);return false;}
    IORecursiveLockLock(clock.lock);IOLockLock(c.lock);
    const auto owned=[&]() {
        if(!power.sleeping || power.failed || !dsiStopAttempted || !dsiIRQParked || !dsiResourcesReady ||
           !dsiStop.complete() || !dsiPanelStop.complete || !dsiNativeWriterHooksReady || !dsiProfile.valid() ||
           controller!=c.controller || controller!=adlnTopologyController || c.workLoop!=loop ||
           Display13FBMemory::field<IOWorkLoop *>(controller,0xE00)!=loop || adlnOutputWiring.dsi!=1 ||
           c.pipe[0].memoryFramebuffer!=fb || Display13FBMemory::singlePath<OSObject *>(fb)!=path ||
           c.transcoders.dsi0Pipe!=0 || c.dsiNativeWriters || c.memoryFaulted || c.clockFaulted || c.watermarkFaulted ||
           c.bridge.state.faulted || c.bridge.state.suspended || c.dbuf.faulted || c.dbuf.updating ||
           c.pipe[0].destroyingPath || c.pipe[0].wmProgramming || c.pipe[0].wmCommitting ||
           !c.dsiPort || Display13FBMemory::field<OSObject *>(path,0x3648)!=c.dsiPort ||
           Display13FBMemory::field<uint32_t>(c.dsiPort,0x550)!=1 || Display13FBMemory::field<uint32_t>(path,0x3644)!=2 ||
           Display13FBMemory::field<uint32_t>(path,0x35F0) || !adlnWriterMappingBound() ||
           clock.thread || clock.allocation.external[0]!=reinterpret_cast<uintptr_t>(controller) ||
           clock.allocation.binding[0]!=dsiBoot.source.identity.pll+1 || !c.bootDSI.sameSource(dsiBoot.source) ||
           !dsiPublishedTarget(fb) || !dsiPendingBacking.modeReady || !dsiArmedBackingOwned(fb,path))return false;
        Display13FBBootIO io{c.map};return c.bridge.outputHeld(io,0,0);
    };
    struct IO : Display13FBBootIO {
        decltype(owned) &owns;IOSimpleLock *lock;unsigned pll;
        IO(IOMemoryMap *m,decltype(owned) &o,IOSimpleLock *l,unsigned p):Display13FBBootIO{m},owns(o),lock(l),pll(p) {}
        bool ready(){return owns();}
        void write(uint32_t r,uint32_t v) {
            if(readable(r) && (XELP::DSI::Restart::writable(r,pll) || r==0x6B100 || r==0x6B104 ||
               r==0xC4030 || r==0xC7204 || r==0xC7304 || r==0x70180 || r==0x7019C || r==0x7B008 ||
               r==0x70080 || r==0x70088 || r==0x70084 || r==XELP::DSI::Backlight::Duty))
                *reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress()+r)=v;
        }
        bool gpio(unsigned role,unsigned value) {
            const auto saved=IOSimpleLockLockDisableInterrupt(lock);const auto r=XELP::DSI::Panel::gpio(*this,role,value);
            IOSimpleLockUnlockEnableInterrupt(lock,saved);return r.complete;
        }
        void delayUs(unsigned us){if(us>=1000)IOSleep((us+999)/1000);else IODelay(us);}
    } io{c.map,owned,irq.lock,dsiBoot.source.identity.pll};
    uint64_t receipt[24] {1,1,0,power.cycles,power.sleeping},irqReceipt[24] {1};
    bool masked=false,restored=false,rearmed=false,cursor=false,complete=false;uint32_t master=0;
    auto stoppedPlane=power.plane;stoppedPlane.control&=~(1U<<31);
    receipt[5]=owned() && power.host.stopped && !power.host.started && !power.panel.attempted &&
        power.host.error==XELP::DSI::Restart::Error::None && XELP::DSI::Surface::matches(io,stoppedPlane,false);
    if(receipt[5] && adlnIRQOwner(controller) && irq.lock && irq.lock==Display13FBMemory::field<IOSimpleLock *>(controller,0x90) &&
       irq.base==c.map->getVirtualAddress()) {
        Display13FBIRQIO irqIO{irq.base};auto saved=IOSimpleLockLockDisableInterrupt(irq.lock);
        const auto queued=[&](unsigned offset) {
            return __atomic_load_n(reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(controller)+offset),__ATOMIC_ACQUIRE);
        };
        master=irqIO.read(Display13FBIRQ::Master);
        const bool quiet=irq.scopes.total==irq.handlers && !irq.scopes.display && !irq.softwareHandlers &&
            !(queued(0xE08)&Display13FBIRQ::pipeEvents(0)) && !queued(0xE0C) && !(queued(0xE10)&3);
        if(quiet && !irq.quiescing && !irq.blockedPipes && !irq.state.faulted && irq.state.begun && irq.state.enabled &&
           !(irq.state.active&1) && (irq.state.configured&1) && (irq.admittedPipes&1) && irq.state.transcoders.dsi0Pipe==0 &&
           master!=UINT32_MAX && (master&Display13FBIRQ::Enable)) {
            irq.quiescing=true;irq.blockedPipes=1;irq.state.enabled=false;masked=true;
            irqIO.write(Display13FBIRQ::Master,0);
            const auto after=irqIO.read(Display13FBIRQ::Master);
            if(after==UINT32_MAX || (after&Display13FBIRQ::Enable))irq.state.faulted=true;
        }
        const bool canStart=masked && !irq.state.faulted;
        IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
        if(canStart) {
            struct Lifecycle {
                XELPComboClock::ClockAllocation &allocation;uintptr_t owner;XELP::DSI::Restart::State &state;
                bool stop(IO &,const XELP::DSI::Readout &,const XELP::DSI::Profile &){return false;}
                bool start(IO &io,XELP::DSI::Readout &source) {
                    if(!XELP::DSI::Restart::start(io,allocation,owner,state))return false;
                    source=XELP::DSI::Restart::expected(source,state);return source.complete();
                }
            } lifecycle{clock.allocation,reinterpret_cast<uintptr_t>(controller),power.host};
            XELP::DSI::Panel::cycle(io,c.bootDSI,dsiProfile,power.plane.control,power.plane.surface,power.panel,lifecycle,&dsiPanelStop,
                dsiBacklight.initialized?&dsiBacklight:nullptr);
            if(power.panel.complete() && owned() && XELP::DSI::Surface::matches(io,power.plane,true)) {
                io.write(0x70080,power.cursorControl);io.write(0x70088,power.cursorPosition);io.write(0x70084,power.cursorBase);
                const auto before=io.read(0x70040);
                for(unsigned us=0;us<=50000;us+=1000) {
                    const auto after=io.read(0x70040);
                    if(before!=UINT32_MAX && after!=UINT32_MAX && after!=before) {
                        cursor=XELP::DSI::Surface::word(io,0x70080,UINT32_MAX,power.cursorControl) &&
                            XELP::DSI::Surface::word(io,0x70088,UINT32_MAX,power.cursorPosition) &&
                            XELP::DSI::Surface::word(io,0x70084,UINT32_MAX,power.cursorBase);break;
                    }
                    if(us<50000)io.delayUs(1000);
                }
                if(cursor){dsiBoot.source=power.panel.source;c.bootDSI=power.panel.source;}
            }
        }
        if(masked) {
            saved=IOSimpleLockLockDisableInterrupt(irq.lock);
            const auto wells=irqIO.read(Display13FBIRQ::Driver);
            if(wells==UINT32_MAX || (wells&3)!=3)irq.state.faulted=true;
            if(!irq.state.faulted) {
                irqIO.write(Display13FBIRQ::Master,master&Display13FBIRQ::Enable);
                const auto after=irqIO.read(Display13FBIRQ::Master);
                restored=after!=UINT32_MAX && (after&Display13FBIRQ::Enable);irq.state.enabled=restored;
                if(!restored)irq.state.faulted=true;
                if(restored && cursor && owned()) {
                    const auto actual=Display13FBIRQ::readActivePipes(irqIO,irq.admittedPipes,irq.state.transcoders);
                    irqReceipt[1]=irq.admittedPipes;irqReceipt[2]=irq.state.active;irqReceipt[3]=actual.active;
                    irqReceipt[4]=static_cast<uint32_t>(actual.status);irqReceipt[5]=wells;
                    for(unsigned p=0;p<3;++p)if(Display13FBIRQ::powered(wells,p)) {
                        irqReceipt[6+3*p]=Display13Transcoder::control(irqIO,p,irq.state.transcoders);
                        irqReceipt[7+3*p]=irqIO.read(Display13FBIRQ::PipeIIR(p)+4);
                        irqReceipt[8+3*p]=irqIO.read(Display13FBIRQ::PipeIIR(p)-4);
                    }
                    const auto status=Display13FBIRQ::extendActivePipes(irqIO,irq.state,irq.admittedPipes);
                    irqReceipt[15]=static_cast<uint32_t>(status);irqReceipt[16]=irq.state.active;
                    irqReceipt[17]=static_cast<uint32_t>(irq.state.firstFailure);
                    irqReceipt[18]=irq.state.failureRegister;irqReceipt[19]=irq.state.failureBits;
                    irqReceipt[20]=irq.state.enabled;irqReceipt[21]=irq.state.faulted;
                    rearmed=status==Display13FBIRQ::Status::Complete && (irq.state.active&1);
                }
            }
            irq.quiescing=false;irq.blockedPipes=0;
            adlnIRQCache(controller,irqIO);IOSimpleLockUnlockEnableInterrupt(irq.lock,saved);
            complete=restored && rearmed && cursor && power.panel.complete() && owned();
            if(complete){power.sleeping=false;++power.cycles;dsiStopAttempted=false;dsiIRQParked=false;}
            else{power.failed=true;adlnFault(c.memoryFaulted,__LINE__);}
        }
    }
    const auto &h=power.host;const auto &p=power.panel;
    receipt[2]=complete;receipt[3]=power.cycles;receipt[4]=power.sleeping;
    receipt[6]=power.plane.control;receipt[7]=power.plane.surface;receipt[8]=power.cursorControl;receipt[9]=power.cursorBase;
    receipt[10]=static_cast<uint32_t>(h.stage);receipt[11]=static_cast<uint32_t>(h.error);
    receipt[12]=h.reg;receipt[13]=h.value;receipt[14]=h.writes;receipt[15]=h.waitUs;
    receipt[16]=masked;receipt[17]=restored;receipt[18]=rearmed;receipt[19]=cursor;
    receipt[20]=static_cast<uint32_t>(p.stage);receipt[21]=static_cast<uint32_t>(p.error);receipt[22]=p.reg;receipt[23]=p.value;
    const auto &commands=p.host;
    uint64_t commandReceipt[] {1,static_cast<uint32_t>(commands.error),commands.pending,commands.faulted,
        commands.sequenceId,commands.operationOffset,commands.headers,commands.payloadWords,
        commands.completedOperations,commands.waitedUs};
    IOLockUnlock(c.lock);IORecursiveLockUnlock(clock.lock);
    if(acquired)reinterpret_cast<void (*)(IOWorkLoop *)>(adlnWorkLoopOpen)(loop);
    loop->release();IORecursiveLockUnlock(mutex);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIRuntimePower",receipt,sizeof(receipt));
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIResumeIRQ",irqReceipt,sizeof(irqReceipt));
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIResumeCommands",commandReceipt,sizeof(commandReceipt));
    publishDSIBacklight();
    return complete;
}
