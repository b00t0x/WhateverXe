// Included after the production adapters by display13_boot_handoff.py.
// Model hardware completion at the MMIO boundary. Run the qualified donor's
// modeset dispatch, error unwind, port/transport and Combo clock bodies.
using Display13FBMemory::field;

IORecursiveLock reconstructionClockLock;
uint64_t reconstructionCanary=0x384756;
unsigned stoppedPorts=0;
unsigned peerPLL=UINT32_MAX;
uint32_t retainedPowerRefs[Display13Power::Count]{};
constexpr uint32_t comboMux=0x164280;
constexpr uint32_t pipeClock=0x46140;
constexpr uint32_t pipeConfig=0x70008;
constexpr uint32_t transport=0x60540;
constexpr uint32_t transcoder=0x60400;

bool XE::comboClockPower(void *power,bool acquire) {
    auto &c=display13FBPower;
    assert(power==c.power);
    Display13FBPowerIO io{c.map};
    return (acquire ? c.bridge.getPG(io,1) : c.bridge.putPG(io,1)).status==Display13Power::Status::Complete;
}

uint32_t XE::comboClockRead(void *access,unsigned long reg) { return nativeRead(access,reg); }
void XE::comboClockWrite(void *access,unsigned long reg,uint32_t value) { nativeWrite(access,reg,value); }

struct AUXCommand {
    bool active;
    unsigned ddi {};
    explicit AUXCommand(bool enabled):active(enabled) {
        if(!active)return;
        auto &c=display13FBPower;
        ddi=field<uint32_t>(reconstructionPort,0x120);
        Display13FBPowerIO io{c.map};
        assert(c.bridge.getAUX(io,ddi).status==Display13Power::Status::Complete);
        assert(c.bridge.beginAUXCommand(io,ddi));
    }
    ~AUXCommand() {
        if(!active)return;
        auto &c=display13FBPower;
        Display13FBPowerIO io{c.map};
        assert(c.bridge.endAUXCommand(io,ddi));
        assert(c.bridge.putAUX(io,ddi).status==Display13Power::Status::Complete);
    }
};

uint32_t nativeDSCWrite(void *port,uint32_t address,void *value,uint32_t bytes) {
    assert(replayCleanup && port==reconstructionPort && address==0x160 && bytes==1);
    assert(!*static_cast<uint8_t *>(value));
    AUXCommand command(true);
    ++nativeDSCWrites;
    if(nativeDSCWriteFails)return kIOReturnNotReady;
    dscEnable=0;
    return 0;
}

void nativeGetDDI(void *port) {
    auto &c=display13FBPower;
    assert(port==reconstructionPort && !locked);
    Display13FBPowerIO io{c.map};
    assert(c.bridge.getDDI(io,field<uint32_t>(port,0x120)).status==Display13Power::Status::Complete);
}

void nativePutDDI(void *port) {
    auto &c=display13FBPower;
    assert(port==reconstructionPort && !locked && !c.map->regs.at(pipeClock));
    assert(!(c.map->regs.at(transport)&0x80000000U));
    const auto ddi=field<uint32_t>(port,0x120);
    assert(!c.mainLink[ddi].owner);
    Display13FBPowerIO io{c.map};
    assert(c.bridge.putDDI(io,ddi).status==Display13Power::Status::Complete);
}

void replayEnableClocks(void *controller,void *port,void *other,uint32_t *params) {
    assert(!locked && port==reconstructionPort);
    lightStages.push_back(Clocks);
    XE::wrapDisplay13TCEnableClocks(controller,port,other,params);
    auto &c=display13FBPower;
    const auto ddi=field<uint32_t>(port,0x120);
    const XELPComboClock::Port route(ddi);
    const auto mux=c.map->regs.at(comboMux);
    const auto selected=(mux>>route.clockShift)&3;
    assert(!c.clockFaulted && !(mux&route.clockOff) && selected<2);
    assert(c.map->regs.at(0x46010+4*selected)==0xcc000000);
    assert(c.bridge.ddiRefs[ddi]==1 && c.bridge.pgRefs[0]==1);
}

uint32_t replayDisablePort(void *port) {
    ++stoppedPorts;
    return XE::wrapDisplay13DisablePort(port);
}

uint32_t replayRead(void *access,uint64_t reg) {
    if(comboClockAccess(access))reg=comboClockTransaction.translation.reg(reg,
        XELPComboClock::donorPC(state.framebufferClockImage,reinterpret_cast<uintptr_t>(__builtin_return_address(0))));
    return nativeRead(access,reg);
}

void replayWrite(void *access,uint64_t reg,uint32_t value) {
    const auto caller=reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    if(XE::display13DeferClock(access,reg,value,caller))return;
    if(reg==pipeClock && !value) {
        const auto &regs=display13FBPower.map->regs;
        const auto ddi=field<uint32_t>(reconstructionPort,0x120);
        assert(!((regs.at(transport)|regs.at(0x64000+0x100*ddi))&0x80000000U));
    }
    unsigned long address=reg;
    XE::comboClockTranslateWrite(access,address,value,caller);
    if(address==pipeConfig && !(value&0x80000000U))value&=~0x40000000U;
    if(address==0x46010 || address==0x46014) {
        value&=~0x44000000U;
        if(value&0x08000000U)value|=0x04000000U;
        if(value&0x80000000U)value|=0x40000000U;
        if(address==0x46010+4*peerPLL)assert(value==0xcc000000);
    }
    nativeWrite(access,address,value);
}

void replayMailbox(void *controller,uint32_t command,uint32_t,bool wait) {
    assert(controller==display13FBPower.controller && command==7 && !wait);
}

uint32_t replayVoltage(void *controller) { assert(controller==display13FBPower.controller); return 1; }
void replayDelay(unsigned us) { assert(us==1 || us==8 || us==10000); }
void replayIgnored() {}

void installReconstructionReplay(uint8_t *image) {
    // Preserve the native prologue and timing copy. Resume at the connector
    // dispatch after the already-tested comparator has selected a full mode.
    auto next=0x300040U;
    const auto call=[&](unsigned offset,uintptr_t target) {
        redirect(image,offset,next,target);next+=0x20;
    };
    call(0x4fa0e,reinterpret_cast<uintptr_t>(memcpy));
    call(0x50128,reinterpret_cast<uintptr_t>(memcpy));
    const uint8_t pathPort[]={0x49,0x8b,0x9f,0x48,0x36,0x00,0x00};
    memcpy(image+next,pathPort,sizeof(pathPort));
    const auto dispatch=next+sizeof(pathPort);
    const auto branch=[&](unsigned from,unsigned to) {
        image[from]=0xe9;
        put(image,from+1,int32_t(to-from-5));
    };
    branch(dispatch,0x4ff5f);
    branch(0x4fa13,next);
    next+=0x20;

    // The same eDP pipe correction checked by the panel/transcoder fixtures.
    const uint8_t fixedTranscoder[]={0xb9,0x0f,0x00,0x00,0x00};
    assert(!memcmp(image+0x4d705,fixedTranscoder,sizeof(fixedTranscoder)));
    memset(image+0x4d705,0x90,sizeof(fixedTranscoder));
    put(image,0xc80a8,&reconstructionCanary);
    for(auto at:{0x50028U,0x4fc78U,0x50047U,0x500eeU,0x4d506U,0x4d5a3U,0x4d5bbU,
                 0x4d5d4U,0x4d5edU,0x4d95aU,0x4d9aaU,0x4db58U,0x4db64U,
                 0x4db6cU,0x4dba2U,0x4dbd8U})
        call(at,reinterpret_cast<uintptr_t>(replayIgnored));
    for(auto at:{0x4d56aU,0x4dbacU,0x5fc99U,0x6d081U,0x6d9ecU,0x6dd6dU,0x6de16U})
        call(at,reinterpret_cast<uintptr_t>(replayDelay));
    for(auto at:{0x1d232U,0x6d54cU,0x6dda6U})call(at,reinterpret_cast<uintptr_t>(nativeLog));
    for(auto table:{std::pair<unsigned,unsigned>{0xcd330,3},{0xcd348,6},{0xcd378,3},{0xcd3c0,3}})
        for(unsigned i=0;i<table.second;++i) {
            const auto offset=table.first+8*i;
            const auto address=field<uint64_t>(image,offset);
            assert(address>=0xd0000 && address<0x160000);
            put(image,offset,address+reinterpret_cast<uintptr_t>(image));
        }
    const uint64_t cfg[] {0x164284,0x16428c,0x164200,0x164288,0x164290,0x164204};
    memcpy(image+0x9a0e8,cfg,sizeof(cfg));
    const auto tail=[&](unsigned at,uintptr_t target) {
        assert(image[at]==0xe9);
        jump(image+next,target);branch(at,next);next+=0x20;
    };
    tail(0x6ce0b,reinterpret_cast<uintptr_t>(replayEnableClocks));
    tail(0x6dae0,reinterpret_cast<uintptr_t>(XE::wrapDisplay13TCDisableClocks));
    jump(image+0x5e344,reinterpret_cast<uintptr_t>(replayRead));
    jump(image+0x1fe8e,reinterpret_cast<uintptr_t>(portRead));
    jump(image+0x1fea8,reinterpret_cast<uintptr_t>(portWrite));
    jump(image+0x91bca,reinterpret_cast<uintptr_t>(firstPath));
    jump(image+0x4691e,reinterpret_cast<uintptr_t>(replayMailbox));
    jump(image+0x6bf6e,reinterpret_cast<uintptr_t>(replayVoltage));
    jump(image+0x1fefe,reinterpret_cast<uintptr_t>(nativeGetDDI));
    jump(image+0x1ff54,reinterpret_cast<uintptr_t>(nativePutDDI));
    jump(image+0x1ceb0,reinterpret_cast<uintptr_t>(nativeDSCWrite));
    assert(field<uint64_t>(image,0xc95a8)==0x1b456);
    put(image,0xc95a8,reinterpret_cast<uintptr_t>(image+0x1b456));
}

void setupReconstruction(Fixture &f,Map &map,unsigned ddi,uint8_t *image) {
    reconstructionPort=f.port;
    comboClockTransaction={};
    reconstructionClockLock={};
    comboClockTransaction.lock=&reconstructionClockLock;
    state.display13ClockOrderReady=true;
    state.display13DPImage=state.framebufferClockImage=reinterpret_cast<uintptr_t>(image);
    state.tglPortByDDI=reinterpret_cast<uintptr_t>(+[](uint32_t d)->void* {
        return d==field<uint32_t>(reconstructionPort,0x120) ? reconstructionPort : nullptr;
    });
    state.orgDisplay13TCEnableClocks=reinterpret_cast<uintptr_t>(image+0x6ce10);
    state.orgDisplay13TCDisableClocks=reinterpret_cast<uintptr_t>(image+0x6dae6);
    realDisable=reinterpret_cast<decltype(realDisable)>(image+0x5fc56);
    static uintptr_t accessVT[64]{},portVT[64]{};
    accessVT[0x130/8]=reinterpret_cast<uintptr_t>(replayWrite);
    portVT[0x130/8]=reinterpret_cast<uintptr_t>(replayDisablePort);
    put(f.access,0,accessVT);
    put(f.access,0x50,static_cast<void *>(f.ctl));
    objectSizes[f.port]=sizeof(f.port);
    put(f.port,0,portVT);
    put(f.port,0x5b8,ddi?0x6c10cU:0x16210cU);
    put(f.port,0x114,uint8_t(1));
    put(f.port,0x105,uint8_t(stoppedSink!=Unsupported && stoppedSink!=Legacy));
    put(f.ctl,0x1b54,uint8_t(unreadDSC()));
    put(image,0x138ba8,static_cast<void *>(f.ctl));
    put(f.ctl,0xea8,0U);
    put(f.path,0x154,0x80000000U);
    put(f.path,0x128,(ddi+1)<<28); // SetupParams supplies the Combo transcoder route.
    assert(!field<uint32_t *>(f.port,0x548)[2]); // Native eDP connector class.
    for(auto reg:{0xc7204U,0xc8250U,0xc8254U,0xc8258U,0x16428cU,0x164290U,0x46014U})
        map.regs[reg]=0;
    map.regs[comboMux]=1U<<(11-ddi); // GOP uses PLL0.
    if(field<uint32_t>(f.port,0x550)==1) {
        peerPLL=ddi;
        const XELPComboClock::Port peer(1-ddi);
        Display13FBPowerIO io{&map};
        assert(display13FBPower.bridge.getDDI(io,peer.phy).status==Display13Power::Status::Complete);
        map.regs[comboMux]=(map.regs[comboMux]&~peer.clockOff)|(peerPLL<<peer.clockShift);
        map.regs[0x46010+4*peerPLL]=0xcc000000;
        if(peerPLL) {
            map.regs[0x16428c]=0x12345;
            map.regs[0x164290]=0x67890;
        }
    } else peerPLL=UINT32_MAX;
}

void nativeStop(Fixture &f,uint8_t *donor) {
    const auto before=stoppedPorts;
    using Disable=uint32_t (*)(void *,void *,void *,uint8_t);
    assert(!reinterpret_cast<Disable>(donor+0x4d34c)(f.ctl,f.fb,f.path,0));
    assert(stoppedPorts==before+1);
}

void assertStopped(Fixture &f,Map &map,unsigned ddi) {
    auto &c=display13FBPower;
    assert(field<uint32_t>(f.path,0x3644)==1 && !c.bridge.state.faulted && !c.clockFaulted);
    assert(!c.mainLink[ddi].owner && !c.bridge.nativeAUXRefs(ddi) && !c.bridge.auxBusy());
    assert((!c.bridge.nativeDDIRefs(ddi) && c.bridge.pgRefs[0]==1)); // Native pipe owns PG1.
    assert(c.bridge.nativeDDIRefs(1-ddi)==(peerPLL<2?1U:0U));
    const uint32_t targetWell=3U<<(2*ddi);
    assert(!(map.regs.at(0x45454)&targetWell) && !(map.regs.at(0x45444)&targetWell));
    if(peerPLL<2)assert((map.regs.at(0x45454)&(3U<<(2*(1-ddi))))==(3U<<(2*(1-ddi))));
    assert(!display13DeferredClocks[0].pending);
    assert(!map.regs.at(pipeClock) && !(map.regs.at(pipeConfig)&0xc0000000U));
    assert(!((map.regs.at(transcoder)|map.regs.at(transport))&0x80000000U));
    assert((map.regs.at(0x64000+0x100*ddi)&0x80000080U)==0x80U);
    assert(map.regs.at(comboMux)&(1U<<(10+ddi)));
    assert(!comboClockTransaction.allocation.binding[ddi] && !reconstructionClockLock.depth);
    for(unsigned pll=0;pll<2;++pll)
        assert(map.regs.at(0x46010+4*pll)==(pll==peerPLL?0xcc000000U:0));
    if(peerPLL<2) {
        const XELPComboClock::Port peer(1-ddi);
        assert(!(map.regs.at(comboMux)&peer.clockOff));
        assert(((map.regs.at(comboMux)>>peer.clockShift)&3)==peerPLL);
        if(peerPLL)assert(map.regs.at(0x16428c)==0x12345 && map.regs.at(0x164290)==0x67890);
    }
}
