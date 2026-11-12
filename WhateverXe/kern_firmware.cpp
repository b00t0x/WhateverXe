#include "kern_xe.hpp"
#include "kern_dsi_math.hpp"
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>

static void publishBytes(IOPCIDevice *pci,const char *key,const void *bytes,unsigned length) {
    auto data=OSData::withBytes(bytes,length);
    if(data){pci->setProperty(key,data);data->release();}
}

// Firmware is read once, before topology/hook selection. No PCI BAR/ROM scan,
// power acquisition, or MMIO probing occurs here. Explicit properties win later.
void XE::xelpReadFirmware(IOPCIDevice *pci) {
    captureFirmware(pci,xeProfile);
}

void XE::captureFirmware(IOPCIDevice *pci,const XELP::Profile &profile) {
    // Wiring is needed by framebuffer-only profiles before Render is admitted.
    if(!pci || !profile.displayAdmitted())return;
    // Registry IDs identify the physical owner for this boot without retaining
    // a borrowed DeviceInfo pointer. Never replace a valid early snapshot with
    // a later missing/reclaimed OpRegion, or reuse it for a different device.
    const auto owner=pci->getRegistryEntryID();
    if(owner && firmwareOwnerID==owner && firmwareFamily==profile.family && firmwarePorts.valid())return;
    firmwarePorts={};firmwareOwnerID=0;
    const uint32_t asls=pci->configRead32(0xFC);
    uint32_t source=0;
    if(asls && asls<=0xFFFFE000U) {
        auto memory=IOMemoryDescriptor::withPhysicalAddress(asls,8192,kIODirectionIn);
        auto mapping=memory ? memory->map(kIOMapReadOnly) : nullptr;
        if(mapping && mapping->getVirtualAddress() && mapping->getLength()>=8192) {
            const auto bytes=reinterpret_cast<const uint8_t *>(mapping->getVirtualAddress());
            const auto region=XELP::VBT::region(bytes,8192,asls);
            if(region.valid) {
                // Preserve existing diagnostics for collectors.
                pci->setProperty("TGLASLS",asls,32);
                publishBytes(pci,"TGLOpRegion",bytes,8192);
                if(region.address) {
                    auto vbtMemory=IOMemoryDescriptor::withPhysicalAddress(region.address,region.bytes,kIODirectionIn);
                    auto vbtMap=vbtMemory ? vbtMemory->map(kIOMapReadOnly) : nullptr;
                    if(vbtMap && vbtMap->getVirtualAddress() && vbtMap->getLength()>=region.bytes) {
                        const auto data=reinterpret_cast<const void *>(vbtMap->getVirtualAddress());
                        firmwarePorts=XELP::VBT::parse(data,region.bytes,profile.family);
                        if(firmwarePorts.valid()){source=2;publishBytes(pci,"TGLVBT",data,firmwarePorts.bytes);}
                    }
                    if(vbtMap)vbtMap->release();
                    if(vbtMemory)vbtMemory->release();
                }
                if(!firmwarePorts.valid()) {
                    const auto embedded=XELP::VBT::parse(bytes+0x400,region.inlineBytes,profile.family);
                    if(embedded.valid()) {
                        firmwarePorts=embedded;source=1;
                        publishBytes(pci,"TGLVBT",bytes+0x400,firmwarePorts.bytes);
                    } else if(firmwarePorts.status==XELP::VBT::Status::Missing)firmwarePorts=embedded;
                }
            }
        }
        if(mapping)mapping->release();
        if(memory)memory->release();
    }
    const auto hdmi=firmwarePorts.hdmi(3); // physical Combo A/B on both admitted families
    uint32_t summary[]={1,static_cast<uint32_t>(firmwarePorts.status),source,firmwarePorts.version,
        firmwarePorts.present,firmwarePorts.unmapped,firmwarePorts.ambiguous,hdmi.valid,hdmi.hdmi,
        hdmi.ddc[0],hdmi.ddc[1],hdmi.ddc[2]};
    pci->setProperty("WhateverXeFirmwarePorts",summary,sizeof(summary));
    pci->setProperty("WhateverXeFirmwareChecksum",firmwarePorts.checksum,32);
    pci->setProperty("WhateverXeFirmwarePortRecords",firmwarePorts.port,sizeof(firmwarePorts.port));
    const auto &panel=firmwarePorts.panel;
    uint32_t panelFacts[]={panel.valid,panel.type,panel.ddi,panel.lowSwing,panel.hobl};
    pci->setProperty("WhateverXeFirmwarePanel",panelFacts,sizeof(panelFacts));
    if(firmwarePorts.valid()){firmwareOwnerID=owner;firmwareFamily=profile.family;}
    if(checkKernelArgument("-wex-dsi-audit")) {
        // Synchronous audit of the existing copied firmware property. Never
        // retain borrowed sequence pointers, acquire power, or authorize IO.
        const auto vbt=OSDynamicCast(OSData,pci->getProperty("TGLVBT"));
        const auto profile=XELP::DSI::parseProfile({vbt?static_cast<const uint8_t *>(vbt->getBytesNoCopy()):nullptr,
            vbt?vbt->getLength():0U},true);
        const auto link=XELP::DSI::linkPlan(profile);
        uint32_t audit[]={1,static_cast<uint32_t>(profile.status),profile.bdbTailQuirk,
            profile.version,profile.checksum,profile.panel,profile.lanes,
            profile.mode.width,profile.mode.height,profile.mode.clockKHz,profile.mode.htotal(),profile.mode.vtotal(),
            profile.pwmHz,profile.minimum,profile.brightness,profile.precision,
            profile.sequences.packets,profile.sequences.delays,profile.sequences.gpios,
            uint32_t(profile.sequences.delayUs),uint32_t(profile.sequences.delayUs>>32),
            profile.ppsUs[0],profile.ppsUs[1],profile.ppsUs[2],profile.ppsUs[3],profile.ppsUs[4],
            link.valid,link.afeKHz,link.portKHz,link.escapeDivider,link.phyEscapeDivider,
            link.clockTiming,link.dataTiming};
        publishBytes(pci,"WEXDSIFirmwareAudit",audit,sizeof(audit));
    }
}
