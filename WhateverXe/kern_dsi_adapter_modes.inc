// Apple fixed-link mode adapter and native GPU presentation transactions.
// Rotation/scaling remain Apple operations; this is their DSI attachment.
// Included only by kern_xe.cpp; retains its private owners and lock ordering.

// Pipe A belongs to DSI for this adoption phase. Malformed/ambiguous objects
// must not fall into a normal-transcoder read or the donor PWM calculation.
static bool dsiCachePath(OSObject *path) {
    if(!path || path->getMetaClass()->getClassSize()<0x3730)return true;
    const auto pipe=Display13FBMemory::field<uint32_t>(path,0x3640);
    if(pipe==0 || pipe>=3)return true;
    const auto port=Display13FBMemory::field<OSObject *>(path,0x3648);
    return !port || port->getMetaClass()->getClassSize()<0x580 || Display13FBMemory::field<uint32_t>(port,0x120)==0;
}

// Called at the existing mode/disable owners, before any donor notification or
// cursor/MMIO write. A retained panel FB remains ours even if its path fields
// are damaged. Ambiguous objects must not escape into the normal DDI writer.
static bool dsiLifecycleTarget(IOService *fb,OSObject *path) {
    if(dsiModeTarget(fb) || !fb || fb->getMetaClass()->getClassSize()<0x4B90)return true;
    const auto attached=Display13FBMemory::singlePath<OSObject *>(fb);
    return attached!=path || dsiCachePath(attached);
}

bool XE::processDisplay13DSIModes(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size) {
    const char *symbols[]={
        "__ZN21AppleIntelFramebuffer18setDetailedTimingsEP7OSArray",
        "__ZN21AppleIntelFramebuffer19validateDisplayModeEiPPKNS_15ModeDescriptionEPPK29IODetailedTimingInformationV2",
        "__ZN21AppleIntelFramebuffer15getDisplayModesEPi",
        "__ZN21AppleIntelFramebuffer19getDisplayModeCountEv",
        "__ZN21AppleIntelFramebuffer28getInformationForDisplayModeEiP24IODisplayModeInformation",
        "__ZN21AppleIntelFramebuffer14setDisplayModeEii",
        "__ZN21AppleIntelFramebuffer21getCurrentDisplayModeEPiS0_",
        "__ZN21AppleIntelFramebuffer13getOnlineInfoEP21AppleIntelDisplayPathPhS2_",
        "__ZN21AppleIntelFramebuffer13fbGetDDCBlockEjPhPyjP21AppleIntelDisplayPath"};
    const uint32_t offsets[]={0x30D0E,0x30DE2,0x31FE6,0x32D04,0x32010,0x30F10,0x32A88,0x2E232,0x2F1B4};
    const uint8_t entries[][16]={
        {0x55,0x48,0x89,0xE5,0x41,0x56,0x53,0x49,0x89,0xF6,0x48,0x89,0xFB,0x48,0xFF,0x05},
        {0x55,0x48,0x89,0xE5,0x41,0x56,0x53,0x48,0xFF,0x05,0x98,0x93,0x0A,0x00,0x83,0xFE},
        {0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0x8F,0x84,0x0A,0x00,0x80,0xBF,0xE0,0x01,0x00},
        {0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0xE1,0x79,0x0A,0x00,0x80,0xBF,0xE0,0x01,0x00},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xEC,0x18,0x49,0x89,0xD6},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xEC},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xEC},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xEC},
        {0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50,0x44,0x89}};
    if(!display13FramebufferRequested || size<=0x32D14)return false;
    for(unsigned i=0;i<arrsize(offsets);++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,symbols[i],address,size);patcher.clearError();
        if(fn!=address+offsets[i] || memcmp(reinterpret_cast<void *>(fn),entries[i],16))return false;
    }
    const char *geometry="__ZN21AppleIntelFramebuffer32computeTransformAndSetDimensionsEPK29IODetailedTimingInformationV2";
    const auto compute=patcher.solveSymbol<mach_vm_address_t>(index,geometry,address,size);patcher.clearError();
    const uint8_t entry[]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xEC};
    if(compute!=address+0x31484 || memcmp(reinterpret_cast<void *>(compute),entry,sizeof(entry)))return false;
    dsiComputeGeometry=compute;
    const char *modeSymbols[]={"__ZN21AppleIntelFramebuffer21allocateLogicalBufferEv",
        "__ZN21AppleIntelFramebuffer28setupInitialTransactionStateEj",
        "__ZN21AppleIntelFramebuffer18setTransactionCapsEv",
        "__ZN21AppleIntelFramebuffer20releaseLogicalBufferEv"};
    const uint32_t modeOffsets[]={0x31726,0x318E8,0x319D2,0x33062};
    for(unsigned i=0;i<arrsize(modeOffsets);++i) {
        const auto fn=patcher.solveSymbol<mach_vm_address_t>(index,modeSymbols[i],address,size);patcher.clearError();
        if(fn!=address+modeOffsets[i] || memcmp(reinterpret_cast<void *>(fn),entry,4))return false;
        dsiModeCPU[i]=fn;
    }
    const char *scaler="__ZN16AppleIntelScaler19isSupportedByScalerEjjjjyjb";
    const auto scalerFn=patcher.solveSymbol<mach_vm_address_t>(index,scaler,address,size);patcher.clearError();
    const uint8_t scalerEntry[]={0x55,0x48,0x89,0xE5,0x48,0xFF,0x05,0xD7,0xD7,0x07,0x00,0x48,0x8B,0x47,0x10};
    if(size<=0x6686D || scalerFn!=address+0x6685E ||
       memcmp(reinterpret_cast<void *>(scalerFn),scalerEntry,sizeof(scalerEntry)))return false;
    const char *detailed="__ZN21AppleIntelFramebuffer22validateDetailedTimingEPvy";
    const char *source="__ZN21AppleIntelFramebuffer18ValidateSourceSizeEP29IODetailedTimingInformationV2";
    const auto detailedFn=patcher.solveSymbol<mach_vm_address_t>(index,detailed,address,size);patcher.clearError();
    const auto sourceFn=patcher.solveSymbol<mach_vm_address_t>(index,source,address,size);patcher.clearError();
    const uint8_t detailedEntry[]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xEC};
    const uint8_t sourceEntry[]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x83,0xEC,0x20};
    if(detailedFn!=address+0x2FFCA || sourceFn!=address+0x30AD8 ||
       memcmp(reinterpret_cast<void *>(detailedFn),detailedEntry,sizeof(detailedEntry)) ||
       memcmp(reinterpret_cast<void *>(sourceFn),sourceEntry,sizeof(sourceEntry)))return false;
    const char *pixel="__ZN21AppleIntelFramebuffer19getPixelInformationEiiiP18IOPixelInformation";
    const auto pixelFn=patcher.solveSymbol<mach_vm_address_t>(index,pixel,address,size);patcher.clearError();
    if(pixelFn!=address+0x324A4 || memcmp(reinterpret_cast<void *>(pixelFn),entry,sizeof(entry)))return false;
    KernelPatcher::RouteRequest routes[]={
        {pixel,wrapDSIPixelInfo,orgDSIPixelInfo},
        {detailed,wrapDSIValidateDetailed,orgDSIDetailedValidation},
        {source,wrapDSIValidateSource,orgDSISourceValidation},
        {scaler,wrapDSIScalerSupport,orgDSIScalerSupport},
        {symbols[0],wrapDSISetDetailedTimings,orgDSIMode[0]},
        {symbols[1],wrapDSIValidateMode,orgDSIMode[1]},
        {symbols[2],wrapDSIGetModes,orgDSIMode[2]},
        {symbols[3],wrapDSIGetModeCount,orgDSIMode[3]},
        {symbols[4],wrapDSIGetModeInformation,orgDSIMode[4]},
        {symbols[5],wrapDSISetDisplayMode,orgDSIMode[5]},
        {symbols[6],wrapDSIGetCurrentMode,orgDSIMode[6]},
        {symbols[7],wrapDSIGetOnlineInfo,orgDSIMode[7]},
        {symbols[8],wrapDSIDDC,orgDSIMode[8]}};
    const bool ready=patcher.routeMultiple(index,routes,arrsize(routes),address,size);
    patcher.clearError();return ready;
}

// Bounded, copied CPU evidence from the real FB0 client callbacks. Also
// observe calls before the fixed-mode owner is bound; do not treat the trace
// as admission or invoke any driver/hardware operation while collecting it.
void XE::recordDSIClient(IOService *fb,unsigned event,uint64_t argument,uint32_t result) {
    // Observe peer presentation at the actual framebuffer API boundary.
    // Bounded flips per mode/power transition; no alternate rendering path.
    if(callbackXE->adlnOutputWiring.dsi && fb && fb->getMetaClass()->getClassSize()>=0x4B90 &&
       Display13FBMemory::field<IOService *>(fb,0x1D0)==callbackXE->adlnTopologyController) {
        const auto index=Display13FBMemory::field<uint32_t>(fb,0x1DC);
        if(index>0 && index<3 && (event==6 || event==9 || event==10 || event==11 || event==13)) {
            DSIModeScope scope(fb);if(!scope.lock)return;
            static uint64_t counts[3][14] {};
            if(event==6 || event==9)counts[index][13]=0;
            const auto count=++counts[index][event];
            if(event==13 && count>8 && (!result || count>32))return;
            auto path=Display13FBMemory::singlePath<OSObject *>(fb);
            const bool pathReady=path && path->getMetaClass()->getClassSize()>=0x3730;
            uint64_t words[]={1,event,count,argument,result,index,
                Display13FBMemory::field<uint8_t>(fb,0x1E0),Display13FBMemory::field<uint8_t>(fb,0x199),
                Display13FBMemory::field<uint32_t>(fb,0x4004),Display13FBMemory::field<uint32_t>(fb,0x4284),
                Display13FBMemory::field<uint8_t>(fb,0x4270),Display13FBMemory::field<uint32_t>(fb,0x4A00),
                pathReady,pathReady ? Display13FBMemory::field<uint32_t>(path,0x3640) : UINT32_MAX,
                pathReady ? Display13FBMemory::field<uint32_t>(path,0x3644) : UINT32_MAX,
                pathReady ? Display13FBMemory::field<uint32_t>(path,0x170) : UINT32_MAX,
                pathReady ? Display13FBMemory::field<uint8_t>(path,0x32A5) : UINT32_MAX,
                pathReady && Display13FBMemory::field<IOService *>(path,0x32B0)==fb,
                Display13FBMemory::field<uint32_t>(fb,0x4250),Display13FBMemory::field<uint32_t>(fb,0x4254)};
            const char *name=event==6 ? "Mode" : event==9 ? "Power" : event==10 ? "Current" : event==11 ? "Online" : "Flip";
            char key[64];snprintf(key,sizeof(key),"WEXPeer%u%s",index,name);
            IORegistryEntry::getRegistryRoot()->setProperty(key,words,sizeof(words));
            return;
        }
    }
    if(!callbackXE->adlnOutputWiring.dsi || event>=13 || !fb ||
       fb->getMetaClass()->getClassSize()<0x4B90 || Display13FBMemory::field<uint32_t>(fb,0x1DC))return;
    DSIModeScope scope(fb);if(!scope.lock)return;
    static uint64_t counts[13] {};
    const char *keys[] {"WEXDSIClientBind","WEXDSIClientTimings","WEXDSIClientValidate",
        "WEXDSIClientModes","WEXDSIClientCount","WEXDSIClientInfo","WEXDSIClientSetMode",
        "WEXDSIClientAttribute","WEXDSIClientConnection","WEXDSIClientPower",
        "WEXDSIClientCurrent","WEXDSIClientOnline","WEXDSIClientDDC"};
    const auto count=__atomic_add_fetch(&counts[event],1ULL,__ATOMIC_RELAXED);
    uint64_t words[] {1,event,count,argument,result,
        Display13FBMemory::field<uint8_t>(fb,0x1E0),Display13FBMemory::field<uint8_t>(fb,0x44DE),
        Display13FBMemory::field<uint8_t>(fb,0x1E7),Display13FBMemory::field<uint32_t>(fb,0x4250),
        Display13FBMemory::field<uint32_t>(fb,0x4254),Display13FBMemory::field<uint32_t>(fb,0x4284),
        Display13FBMemory::field<uint32_t>(fb,0x420C),dsiModeTarget(fb),
        dsiModeTarget(fb) && dsiFixedMode.ready,Display13FBMemory::field<void *>(fb,0x4260)!=nullptr,
        Display13FBMemory::field<void *>(fb,0x4348)!=nullptr};
    IORegistryEntry::getRegistryRoot()->setProperty(keys[event],words,sizeof(words));
}

bool XE::bindDSIFixedMode(IOService *fb,const XELP::DSI::Profile &profile) {
    auto &c=display13FBPower;auto &o=dsiFixedMode;
    if(!display13DSIModeHooksReady || adlnOutputWiring.dsi!=1 || !c.lock || !fb ||
       fb->getMetaClass()->getClassSize()<0x4B90 || !profile.valid())return false;
    DSIModeScope scope(fb);if(!scope.lock)return false;
    XELP::DSI::AppleAdapter::Timing timing;
    if(!XELP::DSI::AppleAdapter::makeTiming(profile,timing))return false;
    if(__atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE))return dsiModeTarget(fb) && dsiModeCoherent(fb) &&
        !memcmp(&timing,&o.timing,sizeof(timing));
    auto data=OSData::withBytes(&timing,sizeof(timing));auto array=OSArray::withCapacity(1);
    if(!data || !array || !array->setObject(data)) {OSSafeReleaseNULL(data);OSSafeReleaseNULL(array);return false;}
    IOLockLock(c.lock);
    const auto path=Display13FBMemory::singlePath<OSObject *>(fb);
    const auto binding=adlnInspectPathBinding(fb,path,adlnPlatform,adlnOutputWiring);
    const bool bound=!__atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE) &&
        c.controller==adlnTopologyController && binding.status==TGLFBBinding::Status::Complete &&
        binding.ddi==0 && c.transcoders.dsi(binding.pipe) && c.bootDSI.matches(profile) &&
        c.bootDSI.identity.pipe==binding.pipe && !c.memoryFaulted && !c.bridge.state.faulted;
    if(bound) {
        o.array=array;o.data=data;o.timing=timing;o.mode=profile.mode;o.ready=false;
        fb->retain();__atomic_store_n(&o.framebuffer,fb,__ATOMIC_RELEASE);
    }
    IOLockUnlock(c.lock);
    if(!bound) {data->release();array->release();return false;}
    // The donor ignores setProperty's bool result. Retain our own array/data
    // before calling it, then verify both the raw pointer and registry owner.
    const auto result=FunctionCast(wrapDSISetDetailedTimings,orgDSIMode[0])(fb,array);
    o.ready=result==kIOReturnSuccess;
    o.ready=dsiModeCoherent(fb);
    fb->setProperty("WEXDSIFixedModeReady",o.ready);
    recordDSIClient(fb,0,XELP::DSI::AppleAdapter::FixedModeID,o.ready);
    return o.ready;
}

// Called with the FB mutex and provider gate held, after stopped path/PG
// disposal. The donor's qualified null setter removes only the timing property
// and its raw +4260 pointer; it performs no display or allocator operation.
bool XE::releaseDSIFixedMode(IOService *fb) {
    auto &o=dsiFixedMode;
    if(!o.framebuffer)return true;
    if(!dsiModeTarget(fb) || o.ready || o.logical || !orgDSIMode[0])return false;
    auto raw=Display13FBMemory::field<OSArray *>(fb,0x4260);
    auto property=fb->getProperty("IOFBDetailedTimings");
    if((raw && raw!=o.array) || (property && property!=o.array))return false;
    const auto result=FunctionCast(wrapDSISetDetailedTimings,orgDSIMode[0])(fb,nullptr);
    if(result!=kIOReturnSuccess || Display13FBMemory::field<OSArray *>(fb,0x4260) ||
       fb->getProperty("IOFBDetailedTimings"))return false;
    auto array=o.array;auto data=o.data;auto offered=o.offeredData;
    __atomic_store_n(&o.framebuffer,static_cast<IOService *>(nullptr),__ATOMIC_RELEASE);
    o.array=nullptr;o.data=nullptr;o.offeredData=nullptr;o.offered=nullptr;o.mode={};o.timing={};o.clientMode=XELP::DSI::AppleAdapter::FixedModeID;
    o.presentation={};o.requested={};o.offerCount=0;o.activeMode=o.clientMode;o.presentationReady=false;o.presentationFault=false;o.changingThread=0;
    OSSafeReleaseNULL(array);OSSafeReleaseNULL(data);OSSafeReleaseNULL(offered);fb->release();return true;
}

// IOGraphics also requests EDID independently of online detection. Stop at
// the shared internal DDC entry, before it acquires AUX power on behalf of
// the built-in panel. Supply one synthetic profile descriptor from CPU memory.
uint32_t XE::wrapDSIDDC(IOService *fb,uint32_t block,uint8_t *data,uint64_t *length,uint32_t flags,OSObject *path) {
    if(!callbackXE->adlnOutputWiring.dsi)return FunctionCast(wrapDSIDDC,callbackXE->orgDSIMode[8])(fb,block,data,length,flags,path);
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
    if(Display13FBMemory::field<uint32_t>(fb,0x1DC))return FunctionCast(wrapDSIDDC,callbackXE->orgDSIMode[8])(fb,block,data,length,flags,path);
    const auto result=[&]() -> uint32_t {
        if(!length || !data)return kIOReturnBadArgument;
        const auto capacity=*length;*length=0;
        if(block<1 || block>2 || flags)return kIOReturnUnsupported;
        if(capacity<128)return kIOReturnBadArgument;
        DSIModeScope scope(fb);
        if(!scope.lock || !dsiModeTarget(fb) || !dsiModeCoherent(fb) ||
           (path && path!=Display13FBMemory::singlePath<OSObject *>(fb)))return kIOReturnNotReady;
        if(block==2 && !XELP::DSI::AppleAdapter::panelNeedsDisplayID(dsiFixedMode.mode))return kIOReturnUnsupported;
        uint8_t edid[128];
        if(!XELP::DSI::AppleAdapter::makePanelEDID(dsiFixedMode.mode,edid,block))return kIOReturnNotReady;
        memcpy(data,edid,sizeof(edid));*length=sizeof(edid);return kIOReturnSuccess;
    }();
    recordDSIClient(fb,12,block,result);return result;
}

// Native online detection assumes eDP AUX/EDID. A DSI panel has no such
// sink. Answer only for the real adopted path and its immutable fixed mode;
// its synthetic profile descriptor is supplied without touching AUX power.
uint32_t XE::wrapDSIGetOnlineInfo(IOService *fb,OSObject *path,uint8_t *online,uint8_t *edid) {
    if(!callbackXE->adlnOutputWiring.dsi)return FunctionCast(wrapDSIGetOnlineInfo,callbackXE->orgDSIMode[7])(fb,path,online,edid);
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
    if(Display13FBMemory::field<uint32_t>(fb,0x1DC)) {
        // Explicit development isolation; leave physical wiring and the
        // ordinary external-output backend intact for coexistence testing.
        if(callbackXE->dsiPanelOnly) {
            if(!online || !edid)return kIOReturnBadArgument;
            *online=0;bzero(edid,128);return kIOReturnSuccess;
        }
        return FunctionCast(wrapDSIGetOnlineInfo,callbackXE->orgDSIMode[7])(fb,path,online,edid);
    }
    if(!online || !edid)return kIOReturnBadArgument;
    *online=0;bzero(edid,128);
    DSIModeScope scope(fb);
    const bool ready=scope.lock && dsiModeTarget(fb) && dsiModeCoherent(fb) &&
        path && path==Display13FBMemory::singlePath<OSObject *>(fb) && dsiNativeUpdateAllowed(fb);
    uint8_t descriptor[128];
    const bool described=ready && XELP::DSI::AppleAdapter::makePanelEDID(dsiFixedMode.mode,descriptor);
    if(described){*online=1;memcpy(edid,descriptor,sizeof(descriptor));dsiAdvertisePresentation(fb);}
    const auto result=described ? kIOReturnSuccess : kIOReturnNotReady;
    recordDSIClient(fb,11,*online,result);return result;
}

// The donor initializes current mode to -5, then tries to recover it through
// normal-transcoder timing conversion. DSI already has a qualified GOP and
// fixed timing. Publish that same current mode under the native FB mutex.
uint32_t XE::wrapDSIGetCurrentMode(IOService *fb,int32_t *mode,int32_t *depth) {
    if(!callbackXE->adlnOutputWiring.dsi)return FunctionCast(wrapDSIGetCurrentMode,callbackXE->orgDSIMode[6])(fb,mode,depth);
    if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
    if(Display13FBMemory::field<uint32_t>(fb,0x1DC))return FunctionCast(wrapDSIGetCurrentMode,callbackXE->orgDSIMode[6])(fb,mode,depth);
    DSIModeScope scope(fb);
    const bool ready=scope.lock && dsiModeTarget(fb) && dsiModeCoherent(fb) &&
        Display13FBMemory::field<uint8_t>(fb,0x1E0) &&
        ((dsiFixedMode.changingThread && dsiFixedMode.changingThread==reinterpret_cast<uintptr_t>(current_thread())) || dsiNativeUpdateAllowed(fb));
    if(ready) {
        const uint32_t fixed=dsiFixedMode.presentationReady ? dsiFixedMode.activeMode : dsiFixedMode.clientMode;
        memcpy(reinterpret_cast<uint8_t *>(fb)+0x4250,&fixed,sizeof(fixed));
        if(mode)*mode=fixed;
        if(depth)*depth=Display13FBMemory::field<uint32_t>(fb,0x4254);
    }
    const auto result=ready ? kIOReturnSuccess : kIOReturnNotReady;
    recordDSIClient(fb,10,ready ? dsiFixedMode.clientMode : 0,result);return result;
}

uint32_t XE::wrapDSISetDetailedTimings(IOService *fb,OSArray *array) {
    recordDSIClient(fb,1,array!=nullptr,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(!dsiModeTarget(fb))return FunctionCast(wrapDSISetDetailedTimings,callbackXE->orgDSIMode[0])(fb,array);
        DSIModeScope scope(fb);if(!scope.lock || !dsiModeCoherent(fb))return kIOReturnNotReady;
        uint32_t client=0;unsigned count=0;
        uint64_t offer[16] {3,array ? array->getCount() : 0,UINT32_MAX};
        if(array)for(unsigned i=0;i<array->getCount();++i) {
            auto bytes=OSDynamicCast(OSData,array->getObject(i));
            if(!bytes || bytes->getLength()!=sizeof(dsiFixedMode.timing))continue;
            const auto t=XELP::DSI::AppleAdapter::panelOffer(
                *static_cast<const XELP::DSI::AppleAdapter::Timing *>(bytes->getBytesNoCopy()),dsiFixedMode.timing);
            const bool native=XELP::DSI::AppleAdapter::clientTiming(dsiFixedMode.timing,t,i);
            if(!native && !XELP::DSI::AppleAdapter::presentationTiming(dsiFixedMode.timing,t,i)) {
                // Keep the last rejected OS request across later successful
                // enumeration calls, so clock/geometry admission is observable.
                IORegistryEntry::getRegistryRoot()->setProperty("WEXDSITimingRejected",bytes);
                continue;
            }
            // A rotated native offer has explicit draw dimensions and a
            // transform. Keep an exact wire alias first when both are offered.
            const bool swap=(t.scalerFlags&0x10)!=0;
            const bool fullSize=!t.horizontalInset && !t.verticalInset &&
                t.horizontalScaled==(swap?t.verticalActive:t.horizontalActive) &&
                t.verticalScaled==(swap?t.horizontalActive:t.verticalActive);
            if(native || (!client && fullSize))client=t.reservedA[0];
            ++count;offer[2]=i;offer[3]=client;offer[4]=t.pixelClock;
            offer[5]=t.horizontalActive;offer[6]=t.horizontalBlanking;offer[7]=t.horizontalSyncOffset;offer[8]=t.horizontalSyncWidth;
            offer[9]=t.verticalActive;offer[10]=t.verticalBlanking;offer[11]=t.verticalSyncOffset;offer[12]=t.verticalSyncWidth;
            offer[13]=t.scalerFlags;offer[14]=t.horizontalScaled;offer[15]=t.verticalScaled;
        }
        IORegistryEntry::getRegistryRoot()->setProperty("WEXDSITimingOffer",offer,sizeof(offer));
        auto data=array && array->getCount()==1 ? OSDynamicCast(OSData,array->getObject(0)) : nullptr;
        const bool canonical=data && data->getLength()==sizeof(dsiFixedMode.timing) &&
            !memcmp(data->getBytesNoCopy(),&dsiFixedMode.timing,sizeof(dsiFixedMode.timing));
        if(!count && !canonical)return kIOReturnUnsupported;
        // Size storage from the accepted OS list, not a fixed number of modes.
        // Prepare the complete replacement before touching native state so an
        // allocation failure preserves the previous list and its timing pointers.
        OSData *replacement=nullptr;
        if(count) {
            if(count>UINT32_MAX/sizeof(dsiFixedMode.timing))return kIOReturnNoResources;
            replacement=OSData::withCapacity(count*sizeof(dsiFixedMode.timing));
            if(!replacement)return kIOReturnNoResources;
            for(unsigned i=0;i<array->getCount();++i) {
                auto bytes=OSDynamicCast(OSData,array->getObject(i));
                if(!bytes || bytes->getLength()!=sizeof(dsiFixedMode.timing))continue;
                const auto t=XELP::DSI::AppleAdapter::panelOffer(
                    *static_cast<const XELP::DSI::AppleAdapter::Timing *>(bytes->getBytesNoCopy()),dsiFixedMode.timing);
                if(XELP::DSI::AppleAdapter::clientTiming(dsiFixedMode.timing,t,i) ||
                   XELP::DSI::AppleAdapter::presentationTiming(dsiFixedMode.timing,t,i)) {
                    if(!replacement->appendBytes(&t,sizeof(t))) {
                        replacement->release();return kIOReturnNoResources;
                    }
                }
            }
        }
        // Native storage retains the immutable wire mode. Qualified presentation
        // timings are returned by validation, without changing the DSI link.
        const auto result=FunctionCast(wrapDSISetDetailedTimings,callbackXE->orgDSIMode[0])(fb,dsiFixedMode.array);
        if(result==kIOReturnSuccess && dsiModeCoherent(fb)) {
            auto previous=dsiFixedMode.offeredData;
            dsiFixedMode.offeredData=replacement;
            dsiFixedMode.offered=replacement ? static_cast<const XELP::DSI::AppleAdapter::Timing *>(replacement->getBytesNoCopy()) : nullptr;
            dsiFixedMode.offerCount=count;
            if(replacement)IORegistryEntry::getRegistryRoot()->setProperty("WEXDSITimingList",replacement);
            else IORegistryEntry::getRegistryRoot()->removeProperty("WEXDSITimingList");
            dsiFixedMode.clientMode=client ? client : XELP::DSI::AppleAdapter::FixedModeID;
            OSSafeReleaseNULL(previous);
        } else {
            OSSafeReleaseNULL(replacement);
            if(result==kIOReturnSuccess)return kIOReturnNotReady;
        }
        return result;
    }();
    recordDSIClient(fb,1,array!=nullptr,result);
    return result;
}

uint32_t XE::wrapDSIValidateMode(IOService *fb,int32_t mode,const void **description,const void **timing) {
    recordDSIClient(fb,2,static_cast<uint32_t>(mode),UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(!dsiModeTarget(fb))return FunctionCast(wrapDSIValidateMode,callbackXE->orgDSIMode[1])(fb,mode,description,timing);
        if(description)*description=nullptr;if(timing)*timing=nullptr;
        DSIModeScope scope(fb);if(!scope.lock || !dsiModeCoherent(fb))return kIOReturnNotReady;
        const auto selected=dsiPresentation(mode);if(!selected)return kIOReturnBadArgument;
        const auto result=FunctionCast(wrapDSIValidateMode,callbackXE->orgDSIMode[1])(
            fb,XELP::DSI::AppleAdapter::FixedModeID,description,timing);
        if(!result && timing) {
            if(*timing!=dsiFixedMode.data->getBytesNoCopy())return kIOReturnNotReady;
            if(selected->scalerFlags || selected->horizontalScaled || selected->verticalScaled)*timing=selected;
        }
        return result;
    }();
    recordDSIClient(fb,2,static_cast<uint32_t>(mode),result);
    return result;
}

uint32_t XE::wrapDSIGetModes(IOService *fb,int32_t *modes) {
    recordDSIClient(fb,3,modes!=nullptr,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(!dsiModeTarget(fb))return FunctionCast(wrapDSIGetModes,callbackXE->orgDSIMode[2])(fb,modes);
        DSIModeScope scope(fb);if(!scope.lock || !dsiModeCoherent(fb))return kIOReturnNotReady;
        if(!modes)return kIOReturnBadArgument;
        if(dsiFixedMode.offerCount)for(unsigned i=0;i<dsiFixedMode.offerCount;++i)modes[i]=dsiFixedMode.offered[i].reservedA[0];
        else *modes=dsiFixedMode.clientMode;return kIOReturnSuccess;
    }();
    recordDSIClient(fb,3,modes!=nullptr,result);
    return result;
}

uint32_t XE::wrapDSIGetModeCount(IOService *fb) {
    recordDSIClient(fb,4,0,UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(!dsiModeTarget(fb))return FunctionCast(wrapDSIGetModeCount,callbackXE->orgDSIMode[3])(fb);
        DSIModeScope scope(fb);return scope.lock && dsiModeCoherent(fb) ? (dsiFixedMode.offerCount ? dsiFixedMode.offerCount : 1) : 0;
    }();
    recordDSIClient(fb,4,0,result);
    return result;
}

uint32_t XE::wrapDSIPixelInfo(IOService *fb,int32_t mode,int32_t depth,int32_t aperture,void *info) {
    const auto result=FunctionCast(wrapDSIPixelInfo,callbackXE->orgDSIPixelInfo)(fb,mode,depth,aperture,info);
    if(dsiModeTarget(fb)) {
        static uint32_t sequence=0;uint64_t record[28] {1,__sync_add_and_fetch(&sequence,1),
            static_cast<uint32_t>(mode),static_cast<uint32_t>(depth),static_cast<uint32_t>(aperture),result};
        if(info && !result)memcpy(record+6,info,172);
        char key[40];snprintf(key,sizeof(key),"WEXDSIPixelCall%u",static_cast<unsigned>(record[1])%32);
        IORegistryEntry::getRegistryRoot()->setProperty(key,record,sizeof(record));
    }
    return result;
}

// Observe native mode admission separately from the later published mode list.
uint32_t XE::wrapDSIValidateDetailed(IOService *fb,void *raw,uint64_t size) {
    uint64_t record[60] {};const bool observe=dsiModeTarget(fb) && raw && size==204;
    if(observe)memcpy(record+8,raw,204);
    const auto result=FunctionCast(wrapDSIValidateDetailed,callbackXE->orgDSIDetailedValidation)(fb,raw,size);
    // Match getInformationForDisplayMode: the retained scanout is RGB8.
    // CoreDisplay consumes this metadata before it asks for a mode's info.
    if(observe && !result)static_cast<XELP::DSI::AppleAdapter::ModeInformation *>(raw)->depth=0;
    if(observe) {
        static uint32_t sequence=0;record[0]=1;record[1]=__sync_add_and_fetch(&sequence,1);
        record[2]=result;record[3]=Display13FBMemory::field<uint32_t>(fb,0x4318);
        record[4]=Display13FBMemory::field<uint32_t>(fb,0x431C);
        memcpy(reinterpret_cast<uint8_t *>(record)+268,raw,204);
        char key[40];snprintf(key,sizeof(key),"WEXDSIDetailedValidation%u",static_cast<unsigned>(record[1])%32);
        IORegistryEntry::getRegistryRoot()->setProperty(key,record,sizeof(record));
    }
    return result;
}

bool XE::wrapDSIValidateSource(IOService *fb,void *raw) {
    const auto result=FunctionCast(wrapDSIValidateSource,callbackXE->orgDSISourceValidation)(fb,raw);
    if(dsiModeTarget(fb) && raw) {
        static uint32_t sequence=0;uint64_t record[25] {1,__sync_add_and_fetch(&sequence,1),result,
            Display13FBMemory::field<uint32_t>(fb,0x4318),Display13FBMemory::field<uint32_t>(fb,0x431C)};
        memcpy(record+5,raw,160);
        char key[40];snprintf(key,sizeof(key),"WEXDSISourceValidation%u",static_cast<unsigned>(record[1])%32);
        IORegistryEntry::getRegistryRoot()->setProperty(key,record,sizeof(record));
    }
    return result;
}

// The adopted DSI scanout is fixed-size. It has no panel-fitter resource
// transaction, so let the donor select its existing GPU scaling path. Preserve
// the real OS timing and transform; other framebuffer owners keep native policy.
bool XE::wrapDSIScalerSupport(void *scaler,uint32_t outputW,uint32_t outputH,
    uint32_t sourceW,uint32_t sourceH,uint64_t clock,uint32_t flags,bool format) {
    auto fb=scaler ? Display13FBMemory::field<IOService *>(scaler,0x10) : nullptr;
    if(dsiModeTarget(fb))return false;
    return FunctionCast(wrapDSIScalerSupport,callbackXE->orgDSIScalerSupport)(
        scaler,outputW,outputH,sourceW,sourceH,clock,flags,format);
}

uint32_t XE::wrapDSIGetModeInformation(IOService *fb,int32_t mode,void *raw) {
    recordDSIClient(fb,5,static_cast<uint32_t>(mode),UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(!dsiModeTarget(fb))return FunctionCast(wrapDSIGetModeInformation,callbackXE->orgDSIMode[4])(fb,mode,raw);
        DSIModeScope scope(fb);if(!scope.lock || !dsiModeCoherent(fb))return kIOReturnNotReady;
        const auto selected=dsiPresentation(mode);if(!raw || !selected)return kIOReturnBadArgument;
        const bool rotated=(selected->scalerFlags&0x10)!=0;
        const auto result=FunctionCast(wrapDSIGetModeInformation,callbackXE->orgDSIMode[4])(fb,mode,raw);
        if(!result && !XELP::DSI::AppleAdapter::annotateMode(dsiFixedMode.mode,
            *static_cast<XELP::DSI::AppleAdapter::ModeInformation *>(raw),rotated,selected->horizontalScaled,selected->verticalScaled))return kIOReturnNotReady;
        return result;
    }();
    if(dsiModeTarget(fb)) {
        DSIModeScope scope(fb);
        for(unsigned i=0;scope.lock && i<dsiFixedMode.offerCount;++i)if(dsiFixedMode.offered[i].reservedA[0]==static_cast<uint32_t>(mode)) {
            uint32_t receipt[12] {1,static_cast<uint32_t>(mode),result};
            if(raw && !result)memcpy(receipt+3,raw,36);
            char key[40];snprintf(key,sizeof(key),"WEXDSIModeInfo%u",i);
            IORegistryEntry::getRegistryRoot()->setProperty(key,receipt,sizeof(receipt));break;
        }
    }
    recordDSIClient(fb,5,static_cast<uint32_t>(mode),result);
    return result;
}

// Native logical storage belongs to the same GPU pipe as the retained scanout.
// Never free the scanout or modify the DSI host while changing presentation.
bool XE::releaseDSILogical(IOService *fb) {
    auto &o=dsiFixedMode;auto pipe=dsiPendingBacking.pipe;
    if(!dsiModeTarget(fb) || !pipe || !dsiModeCPU[3] ||
       Display13FBMemory::field<uintptr_t>(fb,0x4340)!=o.logical)return false;
    if(!o.logical)return true;
    reinterpret_cast<void (*)(IOService *)>(dsiModeCPU[3])(fb);
    const bool released=!Display13FBMemory::field<uintptr_t>(fb,0x4340) &&
        !Display13FBMemory::field<uintptr_t>(pipe,0x12B8) && !Display13FBMemory::field<uintptr_t>(pipe,0x12C0);
    if(released)__atomic_store_n(&o.logical,uintptr_t(0),__ATOMIC_RELEASE);
    else o.presentationFault=true;
    return released;
}

uint32_t XE::setDSIPresentation(IOService *fb,uint32_t mode,const XELP::DSI::AppleAdapter::Timing &requested) {
    using Display13FBMemory::field;
    auto &o=dsiFixedMode;auto &c=display13FBPower;
    if(!dsiModeTarget(fb) || !dsiModeCoherent(fb) || !dsiNativeUpdateAllowed(fb) || o.changingThread)return kIOReturnNotReady;
    auto path=Display13FBMemory::singlePath<OSObject *>(fb);auto pipe=dsiPendingBacking.pipe;
    if(!path || !pipe || !dsiArmedBackingOwned(fb,path))return kIOReturnNotReady;
    const auto old=o.presentationReady ? o.presentation : o.timing;
    const uint32_t oldMode=o.presentationReady ? o.activeMode : o.clientMode;
    if(requested.scalerFlags==old.scalerFlags && requested.horizontalScaled==old.horizontalScaled &&
       requested.verticalScaled==old.verticalScaled && requested.horizontalInset==old.horizontalInset &&
       requested.verticalInset==old.verticalInset) {
        o.presentation=requested;o.activeMode=mode;o.presentationReady=true;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4250)=mode;
        return kIOReturnSuccess;
    }
    IOLockLock(c.lock);
    const bool idle=!c.dsiNativeWriters && !c.pipe[0].wmPending && !c.pipe[0].wmCommitting &&
        !field<uint32_t>(path,0x35F0) && !c.dbuf.updating;
    IOLockUnlock(c.lock);if(!idle)return kIOReturnNotReady;
    o.changingThread=reinterpret_cast<uintptr_t>(current_thread());
    uint64_t receipt[16] {1,mode,requested.scalerFlags,oldMode,old.scalerFlags};
    uint64_t gpuState[13] {1};
    const auto captureGPU=[&](unsigned base) {
        const unsigned offsets[]={0x1264,0x1268,0x128C,0x1290,0x1294,0x12D8};
        if(pipe->getMetaClass()->getClassSize()>=0x12DC)
            for(unsigned i=0;i<arrsize(offsets);++i)gpuState[base+i]=field<uint32_t>(pipe,offsets[i]);
    };
    captureGPU(1);
    const auto apply=[&](uint32_t selected,const XELP::DSI::AppleAdapter::Timing &timing) -> bool {
        if(!releaseDSILogical(fb))return false;
        auto cpu=o.timing;cpu.scalerFlags=timing.scalerFlags;
        cpu.horizontalScaled=timing.horizontalScaled;cpu.verticalScaled=timing.verticalScaled;
        cpu.horizontalInset=timing.horizontalInset;cpu.verticalInset=timing.verticalInset;
        if(!XELP::DSI::AppleAdapter::prepareGeometry(fb,path,dsiProfile,cpu,
            reinterpret_cast<void (*)(void *,const XELP::DSI::AppleAdapter::Timing *)>(dsiComputeGeometry),true))return false;
        // The native allocator asks getCurrentDisplayMode/getPixelInformation
        // through its GPU callback. Publish the selected CPU contract first,
        // while other display writers remain excluded by changingThread.
        o.presentation=timing;o.activeMode=selected;o.presentationReady=true;
        *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(fb)+0x4250)=selected;
        const auto result=reinterpret_cast<uint32_t (*)(IOService *)>(dsiModeCPU[0])(fb);
        __atomic_store_n(&o.logical,field<uintptr_t>(fb,0x4340),__ATOMIC_RELEASE);
        receipt[5]=result;receipt[6]=o.logical;receipt[7]=field<uintptr_t>(pipe,0x12B8);receipt[8]=field<uintptr_t>(pipe,0x12C0);
        if(result)return false; // native failure can still leave a record to release
        if(field<uint32_t>(fb,0x4338)&0x30) {
            auto record=reinterpret_cast<const XELP::DSI::NativeBacking::Record *>(o.logical);
            if(!record || record->kind!=2 || record->columns!=(field<uint32_t>(fb,0x430C)*4+127)/128 || record->rows!=(field<uint32_t>(fb,0x4310)+31)/32 ||
               record->pitch!=field<uint32_t>(fb,0x4334) || record->bytes!=uint64_t(record->pitch)*field<uint32_t>(fb,0x4310) || !record->memory || !record->map ||
               field<uintptr_t>(pipe,0x12B8)!=record->memory || field<uintptr_t>(pipe,0x12C0)!=record->map)return false;
        } else if(o.logical || field<uintptr_t>(pipe,0x12B8) || field<uintptr_t>(pipe,0x12C0))return false;
        reinterpret_cast<void (*)(IOService *,uint32_t)>(dsiModeCPU[1])(fb,0);
        reinterpret_cast<void (*)(IOService *)>(dsiModeCPU[2])(fb);
        return dsiArmedBackingOwned(fb,path) && dsiPublishedTarget(fb);
    };
    const bool changed=apply(mode,requested);
    receipt[9]=changed;
    if(!changed) {
        const bool restored=!o.presentationFault && apply(oldMode,old);
        receipt[10]=restored;if(!restored)o.presentationFault=true;
    }
    receipt[11]=field<uint32_t>(fb,0x4338);receipt[12]=field<uintptr_t>(fb,0x4348);
    receipt[13]=o.presentationFault;receipt[14]=field<uint32_t>(path,0xF8);receipt[15]=field<uint32_t>(path,0xFC);
    captureGPU(7);
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPresentationGPU",gpuState,sizeof(gpuState));
    o.changingThread=0;
    IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIPresentation",receipt,sizeof(receipt));
    return changed ? kIOReturnSuccess : kIOReturnNotReady;
}

// Native timing/resource setup can disable or reallocate before hwSetMode.
// DSI uses its own fixed-mode transaction to retain the adopted host link.
uint32_t XE::wrapDSISetDisplayMode(IOService *fb,int32_t mode,int32_t depth) {
    recordDSIClient(fb,6,(uint64_t(static_cast<uint32_t>(mode))<<32)|static_cast<uint32_t>(depth),UINT32_MAX);
    const auto result=[&]() -> uint32_t {
        if(callbackXE->adlnOutputWiring.dsi) {
            if(!fb || fb->getMetaClass()->getClassSize()<0x4B90)return kIOReturnNotReady;
            if(callbackXE->dsiPanelOnly && Display13FBMemory::field<uint32_t>(fb,0x1DC))return kIOReturnNotReady;
            DSIModeScope scope(fb);if(!scope.lock)return kIOReturnNotReady;
            if(dsiLifecycleTarget(fb,Display13FBMemory::singlePath<OSObject *>(fb))) {
                // A bounded receipt of the real request. Keep the donor's mode
                // reallocation path closed until the DSI surface transaction owns it.
                uint64_t record[] {1,Display13FBMemory::field<uint32_t>(fb,0x1DC),
                    Display13FBMemory::field<uint8_t>(fb,0x1E0),static_cast<uint32_t>(mode),
                    static_cast<uint32_t>(depth),dsiModeTarget(fb),callbackXE->dsiResourcesReady,
                    Display13FBMemory::field<uint32_t>(fb,0x420C)};
                IORegistryEntry::getRegistryRoot()->setProperty("WEXDSIModeRequest",record,sizeof(record));
                const auto selected=dsiPresentation(mode);
                if(!selected || depth || !dsiModeTarget(fb))return kIOReturnNotReady;
                const auto presentation=*selected;
                if(dsiNativeUpdateAllowed(fb) && !dsiPendingTarget(fb))callbackXE->probeDSINativeBacking(fb);
                if(!callbackXE->publishDSINativeMode(fb))return kIOReturnNotReady;
                return callbackXE->setDSIPresentation(fb,mode,presentation);
            }
        }
        return FunctionCast(wrapDSISetDisplayMode,callbackXE->orgDSIMode[5])(fb,mode,depth);
    }();
    recordDSIClient(fb,6,(uint64_t(static_cast<uint32_t>(mode))<<32)|static_cast<uint32_t>(depth),result);
    return result;
}
