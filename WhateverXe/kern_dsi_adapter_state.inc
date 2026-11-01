// DSI attachment owners and identity predicates. Included inside the
// existing anonymous namespace; no new owner, lock or initialization phase.

static void dsiPublishIRQLive();

// The FB mutex serializes prepare/discard/publication. Native allocation also
// publishes GPU-pipe references before this record becomes the FB scanout.
// Keep both owners retained, including on partial failure, until proven free.
struct DSIPendingBackingOwner {
    IOService *framebuffer {},*accelerator {};
    OSObject *pipe {},*pinnedMap {},*pinnedMemory {};
    XELP::DSI::NativeBacking::Record *record {};
    uintptr_t thread {};
    bool allocating {},freeing {},allocated {},freed {},armed {},published {},modeReady {};
    uint32_t nativeResult {};
    Display13FBMemory::SurfaceReadout proof;
    bool initialized {};
};
static DSIPendingBackingOwner dsiPendingBacking;
static bool dsiPendingTarget(IOService *fb) {
    return fb && __atomic_load_n(&dsiPendingBacking.framebuffer,__ATOMIC_ACQUIRE)==fb;
}

static bool dsiPublishedTarget(IOService *fb) {
    return dsiPendingTarget(fb) && dsiPendingBacking.published && dsiPendingBacking.record &&
        Display13FBMemory::field<void *>(fb,0x4348)==dsiPendingBacking.record;
}


// One DSI0 panel. Publish only after native path/pipe association; the FB and
// immutable timing array remain retained until a quiesced DSI stop can detach
// them. A donor property failure must never leave its raw array pointer dangling.
struct DSIFixedModeOwner {
    IOService *framebuffer {};
    OSArray *array {};
    OSData *data {};
    XELP::DSI::Mode mode;
    XELP::DSI::AppleAdapter::Timing timing;
    XELP::DSI::AppleAdapter::Timing presentation,requested;
    OSData *offeredData {};
    const XELP::DSI::AppleAdapter::Timing *offered {};
    unsigned offerCount {};
    uint32_t activeMode {XELP::DSI::AppleAdapter::FixedModeID};
    uintptr_t changingThread {},logical {};
    bool presentationReady {},presentationFault {};
    uint32_t clientMode {XELP::DSI::AppleAdapter::FixedModeID};
    bool ready {};
};
static DSIFixedModeOwner dsiFixedMode;
static bool dsiModeTarget(IOService *fb) {
    return fb && __atomic_load_n(&dsiFixedMode.framebuffer,__ATOMIC_ACQUIRE)==fb;
}
static uintptr_t dsiLogicalSurface(const void *fb) {
    const auto &o=dsiFixedMode;
    return fb && __atomic_load_n(&o.framebuffer,__ATOMIC_ACQUIRE)==fb ?
        __atomic_load_n(&o.logical,__ATOMIC_ACQUIRE) : 0;
}
static const XELP::DSI::AppleAdapter::Timing *dsiPresentation(uint32_t mode) {
    auto &o=dsiFixedMode;
    if(o.changingThread && o.presentationReady && mode==o.activeMode)return &o.presentation;
    const XELP::DSI::AppleAdapter::Timing *selected=nullptr;
    for(unsigned i=0;i<o.offerCount;++i)if(o.offered[i].reservedA[0]==mode){selected=&o.offered[i];break;}
    if(!selected && XELP::DSI::AppleAdapter::acceptsMode(mode,o.clientMode))selected=&o.timing;
    if(!selected && o.presentationReady && mode==o.activeMode)selected=&o.presentation;
    if(!selected)return nullptr;
    // UI orientation is not a request for driver-side scaling/rotation.
    // Preserve the timing actually supplied by CoreDisplay. Fabricating a
    // landscape logical buffer here changes the compositor's target geometry.
    return selected;
}
// The DSI adapter now accepts actual OS quarter-turn timings and allocates
// their scaled GPU source. Expose that support when CoreDisplay builds modes.
static void dsiAdvertisePresentation(IOService *fb) {
    auto data=OSDynamicCast(OSData,fb->getProperty("IOFBScalerInfo"));
    if(!data || data->getLength()!=48)return;
    uint32_t scaler[12];memcpy(scaler,data->getBytesNoCopy(),sizeof(scaler));
    if(!(scaler[4]&0x20)){scaler[4]|=0x20;fb->setProperty("IOFBScalerInfo",scaler,sizeof(scaler));}
}
struct DSIModeScope {
    IORecursiveLock *lock;
    explicit DSIModeScope(IOService *fb):lock(Display13FBMemory::field<IORecursiveLock *>(fb,0x4220)) {
        if(lock)IORecursiveLockLock(lock);
    }
    ~DSIModeScope(){if(lock)IORecursiveLockUnlock(lock);}
};
static bool dsiModeCoherent(IOService *fb) {
    const auto &o=dsiFixedMode;
    return o.ready && o.array && o.data && o.array->getCount()==1 && o.array->getObject(0)==o.data &&
        o.data->getLength()==sizeof(o.timing) && !memcmp(o.data->getBytesNoCopy(),&o.timing,sizeof(o.timing)) &&
        Display13FBMemory::field<OSArray *>(fb,0x4260)==o.array && fb->getProperty("IOFBDetailedTimings")==o.array;
}
