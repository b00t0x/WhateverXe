#include <cstdio>
#include "compat_kernel_fixture.hpp"
extern "C" const char version[]="Darwin Kernel Version 25.6.9: synthetic; root:xnu-test/RELEASE_X86_64";
#include "AppleIntelTGLCompat/AppleIntelTGLCompat.cpp"
#include "kern_display13_fb_topology.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_tgl_tc_boot.hpp"
// Registry is an observation boundary; the real WEX method constructs and
// delivers ProbeConfig directly to the real Compat export on the same objects.
struct XE {
    XELP::Profile xeProfile;
    XELP::ProbePort xelpProviderProbe=tglProbePort;
    XELP::ConfigureProbe xelpProviderConfigureProbe=xelpConfigureDisplay;
    bool adlsDisplaySelected=false,tglHDMIWiringApplied=false,tglTCWiringLoaded=false;
    TGLHDMIWiring tglHDMIWiring;
    AlderLakeS::Wiring adlsWiring;
    TGLTCWiring tglTCWiring;
    uint32_t tglTCAutoCandidates=0,tglAUXParentMask=0;
    bool display13FBTopologyHooksReady=false,display13TCProbeReady=false,display13FBTCHooksReady=false;
    bool display13TCDPProbeReady=false,display13FBSSTHooksReady=false,display13FBAUXHooksReady=false;
    bool display13DPAltProbeReady=false,display13PanelProbeReady=false,display13PanelPowerReady=false;
    bool dsiResourcesReady=false;
    void *adlnTopologyController=nullptr;
    Display13OutputWiring adlnOutputWiring;
    bool xelpBindProbe(void *);
    // TC_MASK_METHOD
};
// BIND_METHOD
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];XE xe;
    IOService controller;IOPCIDevice pci;OSObject access,port;IOMemoryMap map;
    std::vector<uint64_t> mmio(0x1000000/8);
    alignas(8) uint8_t donor[0x1C0]{},platform[0x1C0]{};
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
        if(strstr(mode,"tc-") || strstr(mode,"alt")) {
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
        if(strstr(mode,"panel")){w.hdmi=0;w.ddc[1]=0;w.panel=1;selected=0;}
        if(strstr(mode,"dsi")){w.dsi=1;xe.dsiResourcesReady=true;}
        assert(Display13FBTopology::apply(platform,sizeof(platform),donor,w));
    }
    const bool missing=strstr(mode,"missing")!=nullptr;
    if(!strcmp(mode,"tc-dp-missing-aux"))xe.display13FBAUXHooksReady=false;
    if(!strcmp(mode,"panel-missing-power"))xe.display13PanelPowerReady=false;
    if(!strcmp(mode,"dsi-missing-resource"))xe.dsiResourcesReady=false;
    assert(xe.xelpBindProbe(&controller)==!missing);
    if(missing){
        assert(!probeBindingState && !controller.retains && !access.retains && !map.retains);
        assert(!IORegistryEntry::getRegistryRoot()->bound);
        xe.display13FBAUXHooksReady=xe.display13PanelPowerReady=xe.dsiResourcesReady=true;
        assert(xe.xelpBindProbe(&controller));
    }
    assert(IORegistryEntry::getRegistryRoot()->bound && controller.retains==1 && access.retains==1 && map.retains==1);
    field<void *>(&port,0x548)=platform+0x20+24*selected;
    assert(tglProbePort(&port)==expected);
    if(!strcmp(mode,"adln-two")){field<void *>(&port,0x548)=platform+0x20;assert(tglProbePort(&port)==1);}
    if(strstr(mode,"dsi")){
        // DSI is owned by WEX's DSI hooks, never Compat's native AUX probe.
        field<void *>(&port,0x548)=platform+0x20;assert(!tglProbePort(&port));
    }
    if(!tgl && !adls)assert(!hardwareAllowed(&controller));
    puts("PASS WEX wiring/config producer -> complete Compat -> native port-mode consumer. CPU only.");
}
