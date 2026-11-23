#include <cstdio>
#include "compat_kernel_fixture.hpp"
extern "C" const char version[]="Darwin Kernel Version 25.6.9: synthetic; root:xnu-test/RELEASE_X86_64";
#include "AppleIntelTGLCompat/AppleIntelTGLCompat.cpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_xelp_display_contract.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_tgl_tc_boot.hpp"
// The WEX registry is an observation boundary. Preserve the separate
// activation mock that is compiled into Compat.
using C=XELP::DisplayContract;
struct ProbeRegistry {
    C::Mask waitingMissing=0;
    unsigned waitingDDI=UINT32_MAX;
    static ProbeRegistry *getRegistryRoot(){static ProbeRegistry root;return &root;}
    void setProperty(const char *key,bool value){
        if(!strcmp(key,"XELPSharedProbeBound"))IORegistryEntry::getRegistryRoot()->setProperty(key,value);
    }
    void setProperty(const char *key,const void *data,unsigned bytes){
        if(!strcmp(key,"WEXDisplay13WaitingPortContract")){
            assert(bytes==6*sizeof(uint64_t));
            const auto words=static_cast<const uint64_t *>(data);
            waitingDDI=words[1];waitingMissing=words[5];
        }
    }
};
#define IORegistryEntry ProbeRegistry
#define FunctionCast(wrapper,fn) fn
namespace Display13FBMemory {using ::field;}
static bool display13Offline=false;
static unsigned nativeProbes=0,providerProbes=0,longPulses=0,shortPulses=0;
static uint32_t providerProbe(void *port){++providerProbes;return tglProbePort(port);}
static uint32_t nativeProbe(OSObject *);
static bool configureBeforeAlt(void *controller,const XELP::ProbeConfig *config){
    assert(config->typeC==0 && !(config->legacy&(1U<<5)));
    return xelpConfigureDisplay(controller,config);
}
static void nativeHotplug(void *,unsigned ddi,unsigned mode){assert(ddi==5 && (mode==3 || mode==4));++longPulses;}
static void nativeShortPulse(void *,unsigned ddi,unsigned mode){assert(ddi==5 && (mode==3 || mode==4));++shortPulses;}
// Registry is an observation boundary; the real WEX method constructs and
// delivers ProbeConfig directly to the real Compat export on the same objects.
struct XE {
    XELP::Profile xeProfile;
    XELP::ProbePort xelpProviderProbe=providerProbe;
    XELP::ConfigureProbe xelpProviderConfigureProbe=xelpConfigureDisplay;
    bool adlsDisplaySelected=false,tglHDMIWiringApplied=false,tglTCWiringLoaded=false;
    TGLHDMIWiring tglHDMIWiring;
    AlderLakeS::Wiring adlsWiring;
    TGLTCWiring tglTCWiring;
    uint32_t tglTCAutoCandidates=0,tglAUXParentMask=0;
    bool display13FBTopologyHooksReady=false,display13TCProbeReady=false,display13FBTCHooksReady=false;
    bool display13TCDPProbeReady=false,display13FBSSTHooksReady=false,display13FBAUXHooksReady=false;
    bool display13DPAltProbeReady=false,display13PanelProbeReady=false,display13PanelPowerReady=false;
    bool framebufferDonorRuntimeReady=true,display13FBPowerReferencesReady=true,display13FBEngineEnableHookReady=true;
    bool display13FBLifecycleHooksReady=true,display13FBDBUFHooksReady=true,display13FBWatermarkHooksReady=true;
    bool display13FBClockReadoutHooksReady=true,display13FBClockSetHookReady=true,display13FBMemoryHooksReady=true;
    bool display13FBGTTHooksReady=true,display13FBABIReady=true,display13FBBootHandoffHookReady=true;
    bool display13FBIRQReady=true,display13FBWatermarkLayoutReady=true,display13FBOutputHooksReady=true;
    bool display13SharedModePatchFaulted=false;
    bool display13TrainingReady=true,display13FramingReady=true,display13ClockOrderReady=true;
    bool tglPanelPowerPropertyReady=true,tglEDPTranscoderReady=true,tglPanelDepthReady=true,tglPanelTimingReady=true,display13PanelPWMReady=true;
    bool dsiResourcesReady=false;
    void *adlnTopologyController=nullptr;
    alignas(8) uint8_t adlnPlatform[0x1C0]{};
    Display13OutputWiring adlnOutputWiring;
    decltype(&nativeProbe) orgAlderLakeNPortState=nativeProbe;
    decltype(&nativeHotplug) orgDisplay13Hotplug=nativeHotplug;
    decltype(&nativeShortPulse) orgDisplay13ShortPulse=nativeShortPulse;
    bool xelpBindProbe(void *);
    C::Mask display13Available() const;
    bool display13PortReady(unsigned,bool boot=false) const;
    bool display13HPDAllowed(void *,unsigned) const;
    static void wrapDisplay13Hotplug(void *,unsigned,unsigned);
    static void wrapDisplay13ShortPulse(void *,unsigned,unsigned);
    static uint32_t wrapDisplay13ProbeMode(OSObject *);
    static uint32_t wrapAlderLakeNPortState(OSObject *);
    static uint32_t setDSIPortMode(OSObject *,uint32_t){assert(false);return 1;}
    // TC_MASK_METHOD
};
static XE *callbackXE;
// A selected native state's probe reaches the same HAL slot used by startup.
static uint32_t nativeProbe(OSObject *p){
    ++nativeProbes;const auto result=XE::wrapDisplay13ProbeMode(p);
    return field<uint32_t>(p,0x554)=result?2:0;
}
// PROBE_METHODS
#undef IORegistryEntry
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];XE xe;callbackXE=&xe;
    IOService controller;IOPCIDevice pci;OSObject access,port;IOMemoryMap map;
    std::vector<uint64_t> mmio(0x1000000/8);
    alignas(8) uint8_t donor[0x1C0]{};auto &platform=xe.adlnPlatform;
    donor[0x12]=9;
    for(unsigned d=0;d<9;++d){
        const uint32_t record[]={d,d<3?d+1:d+6,1,d<3?0U:1U,0x400,0x201};
        memcpy(donor+0x20+24*d,record,sizeof(record));
    }
    // Exact donor's initial panel and Combo-B DP record shape, not a
    // generated ProbeConfig expectation. Real topology/wiring produces ports.
    const uint32_t panel[]={0,0,0,0,2,0x18},comboB[]={1,2,1,0,0x400,0x201};
    memcpy(donor+0x20,panel,sizeof(panel));memcpy(donor+0x38,comboB,sizeof(comboB));
    memcpy(platform,donor,sizeof(platform));
    const bool tgl=!strncmp(mode,"tgl",3),adls=!strcmp(mode,"adls");
    pci.identity=tgl?0x9A498086:adls?0x46908086:!strncmp(mode,"adln",4)?0x46D28086:0x46A68086;
    pci.revision=adls?12:0;controller.provider=&pci;xe.xeProfile=XELP::deviceProfile(&pci);
    map.address=reinterpret_cast<uintptr_t>(mmio.data());map.bytes=0x1000000;
    field<void *>(&controller,0xC40)=&access;field<void *>(&controller,0xD40)=platform;
    field<OSObject *>(&controller,0xC70)=&map;
    field<uintptr_t>(&access,0x50)=map.address;field<uint32_t>(&access,0x60)=map.bytes;access.meta.size=0x68;
    port.meta.size=0x6B0;field<void *>(&port,0x578)=&access;
    unsigned selected=1;uint32_t expected=1;
    if(tgl){
        xe.tglHDMIWiringApplied=xe.tglTCWiringLoaded=true;
        xe.tglTCWiring=TGLTCWiring::fromMasks(1U<<4,0,0);
        xe.tglAUXParentMask=1U<<4;
        const uint32_t property[]={1,2,0,3,0};
        xe.tglHDMIWiring=TGLHDMIWiring::fromProperty(property,sizeof(property));
        assert(xe.tglHDMIWiring.apply(platform,sizeof(platform)));
        if(!strcmp(mode,"tgl-tc")){
            selected=4;expected=4;
            field<uint32_t>(mmio.data(),0xC4000)=1U<<25;
            field<uint32_t>(mmio.data(),0x1638A0)=0x10;
            field<uint32_t>(mmio.data(),0x163890)=2;
        }
    } else if(adls){
        xe.adlsDisplaySelected=xe.tglHDMIWiringApplied=true;
        xe.adlsWiring.valid=true;xe.adlsWiring.hdmi=8;xe.adlsWiring.pin[3]=9;selected=3;
        assert(xe.adlsWiring.apply(platform,sizeof(platform)));
    } else {
        xe.adlnTopologyController=&controller;xe.display13FBTopologyHooksReady=true;
        auto &w=xe.adlnOutputWiring;w.valid=true;w.hdmi=2;w.ddc[1]=3;
        // These are explicit installed-operation prerequisites, not generated
        // flags: their real hook installation is covered outside this replay.
        xe.display13TCProbeReady=XELP::display13TCProbeCapable(xelpProbeCapabilities);
        xe.display13TCDPProbeReady=XELP::display13TCDPProbeCapable(xelpProbeCapabilities);
        xe.display13DPAltProbeReady=XELP::display13DPAltProbeCapable(xelpProbeCapabilities);
        xe.display13FBTCHooksReady=xe.display13FBSSTHooksReady=xe.display13FBAUXHooksReady=true;
        xe.display13PanelProbeReady=(xelpProbeCapabilities(XELP::ProbeCapabilityABI,XELP::ProbeABI)&XELP::ProbeDisplay13Panel)!=0;
        xe.display13PanelPowerReady=true;
        if(!strcmp(mode,"adln-two")){w.hdmi=3;w.ddc[0]=1;}
        if(strstr(mode,"tc-") || strstr(mode,"alt") || !strcmp(mode,"mixed-fixed-tc")) {
            selected=5;expected=strstr(mode,"alt")?3:4;
            w.hdmi=0;w.ddc[1]=0;
            if(strstr(mode,"hdmi")){w.hdmi=1U<<selected;w.ddc[selected]=12;}
            else w.dp=1U<<selected;
            if(strstr(mode,"alt"))w.typeC=w.dp;
            field<uint32_t>(mmio.data(),0xC4000)=1U<<26;
            field<uint32_t>(mmio.data(),0x161508)=w.hdmi?4:0;
            // DP-alt HPD is a separate native bit range.
            if(w.typeC){field<uint32_t>(mmio.data(),0xC4000)=0;field<uint32_t>(mmio.data(),0x44470)=1U<<18;}
        }
        if(strstr(mode,"panel") || !strcmp(mode,"mixed-fixed-tc")){
            w.hdmi=0;w.ddc[1]=0;w.panel=1;
            if(!w.dp)selected=0;
        }
        if(strstr(mode,"dsi")){w.dsi=1;xe.dsiResourcesReady=true;}
        assert(Display13FBTopology::apply(platform,sizeof(platform),donor,w));
    }
    using Mask=C::Mask;
    Mask missing=0;
    if(!strcmp(mode,"tc-dp-missing-aux")){xe.display13FBAUXHooksReady=false;missing=C::AUX13;}
    if(!strcmp(mode,"panel-missing-power")){xe.display13PanelPowerReady=false;missing=C::PanelPower;}
    if(!strcmp(mode,"panel-alt-wait-hooks")){xe.display13FBTCHooksReady=false;missing=C::TCNative;}
    const bool olderProvider=!strcmp(mode,"panel-alt-wait-capability");
    if(olderProvider){
        xe.xelpProviderConfigureProbe=configureBeforeAlt;xe.display13DPAltProbeReady=false;missing=C::HPD;
    }
    const bool dsiMissing=!strcmp(mode,"dsi-missing-resource");
    if(dsiMissing)xe.dsiResourcesReady=false;
    assert(xe.xelpBindProbe(&controller)==!dsiMissing);
    if(dsiMissing){
        assert(!probeBindingState && !controller.retains && !access.retains && !map.retains);
        assert(!IORegistryEntry::getRegistryRoot()->bound);
        xe.dsiResourcesReady=true;assert(xe.xelpBindProbe(&controller));
    }
    assert(IORegistryEntry::getRegistryRoot()->bound && controller.retains==1 && access.retains==1 && map.retains==1);
    field<void *>(&port,0x548)=platform+0x20+24*selected;
    assert(tglProbePort(&port)==(olderProvider?0:expected));
    if(!strcmp(mode,"adln-two")){field<void *>(&port,0x548)=platform+0x20;assert(tglProbePort(&port)==1);}
    if(strstr(mode,"dsi")){
        // DSI is owned by WEX's DSI hooks, never Compat's native AUX probe.
        field<void *>(&port,0x548)=platform+0x20;assert(!tglProbePort(&port));
    }
    if(!tgl && !adls && !strstr(mode,"dsi")){
        field<void *>(&port,0x548)=platform+0x20+24*selected;
        if(xe.adlnOutputWiring.panel && selected==5){
            field<void *>(&port,0x548)=platform+0x20;
            assert(xe.display13PortReady(0,true));
            assert(XE::wrapDisplay13ProbeMode(&port)==1);
            assert(XE::wrapAlderLakeNPortState(&port)==2);
            field<void *>(&port,0x548)=platform+0x20+24*selected;
        }
        const auto oldNative=nativeProbes,oldProvider=providerProbes;
        if(missing){
            field<uint32_t>(&port,0x554)=2;
            assert(!XE::wrapAlderLakeNPortState(&port) && field<uint32_t>(&port,0x554)==0);
            assert(!XE::wrapDisplay13ProbeMode(&port));
            if(selected==5){
                XE::wrapDisplay13Hotplug(&controller,5,expected);
                XE::wrapDisplay13ShortPulse(&controller,5,expected);
            }
            assert(nativeProbes==oldNative && providerProbes==oldProvider && !longPulses && !shortPulses);
            const auto registry=ProbeRegistry::getRegistryRoot();
            assert(registry->waitingDDI==selected && registry->waitingMissing==missing);
            if(olderProvider){
                assert(xe.xelpBindProbe(&controller) && controller.retains==1);
                return 0;
            }
            xe.display13FBAUXHooksReady=xe.display13PanelPowerReady=xe.display13FBTCHooksReady=true;
        }
        if(selected==5){
            auto &hpd=field<uint32_t>(mmio.data(),xe.adlnOutputWiring.typeC?0x44470:0xC4000);
            const auto connected=hpd;hpd=0;
            assert(!XE::wrapDisplay13ProbeMode(&port)); // No cable at first probe.
            hpd=connected;
            XE::wrapDisplay13Hotplug(&controller,5,expected);
            XE::wrapDisplay13ShortPulse(&controller,5,expected);
            assert(longPulses==1 && shortPulses==1);
            assert(xe.display13PortReady(5,true)==!bool(xe.adlnOutputWiring.typeC));
        }
        assert(XE::wrapDisplay13ProbeMode(&port)==expected);
        assert(XE::wrapAlderLakeNPortState(&port)==2);
        assert(nativeProbes==oldNative+1 && controller.retains==1 && access.retains==1 && map.retains==1);
    }
    if(!tgl && !adls)assert(!hardwareAllowed(&controller));
    puts("PASS WEX wiring/config producer -> complete Compat -> native port-mode consumer. CPU only.");
}
