// SPDX-License-Identifier: BSD-2-Clause
// Private ABI for 20A2314a TGL framebuffer c004fc1a... only.
// See docs/xe/2026-09-17_dp1-bringup.md. Experimental, not a general shim.
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>
#include <mach/kmod.h>
#include "AppleIntelTGLCompatPortProbe.hpp"
#include "kern_xelp_device.hpp"
#include "kern_xelp_os.hpp"
#include "kern_graphics_activation.hpp"

extern "C" const char version[];

template <typename T> static T &field(void *object, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(object) + offset);
}
template <typename F> static F method(void *object, size_t offset) {
    return reinterpret_cast<F>((*static_cast<uintptr_t **>(object))[offset / 8]);
}
static void *manager(void *controller) { return field<void *>(controller, 0xc40); }
static void *legacyTGLAccess;
static XELP::ProbeBinding probeBinding;
static uint32_t probeBindingState; // 0 unbound, 1 publishing, 2 immutable/pinned.

// Generic register/DSB operations belong to the qualified Display12 families.
// The separately bound Display13 port probe never opens this gate.
static bool hardwareAllowed(void *controller) {
    if (!controller) return false;
    auto service = static_cast<IOService *>(controller);
    auto pci = OSDynamicCast(IOPCIDevice, service->getProvider());
    const auto profile=XELP::deviceProfile(pci);
    const auto adapter=XELP::displayAdapter(profile.family);
    if(!profile.admitted() || (adapter!=XELP::DisplayAdapter::TGL && adapter!=XELP::DisplayAdapter::ADLS))return false;
    if(adapter==XELP::DisplayAdapter::TGL)
        __atomic_store_n(&legacyTGLAccess,manager(controller),__ATOMIC_RELEASE);
    return true;
}

static uint32_t read32(void *access, unsigned long offset) {
    const auto size = field<uint32_t>(access, 0x60);
    if (size < 4 || offset > size - 4) {
        IOLog("AppleIntelTGLCompat: invalid MMIO read offset=%lx size=%x\n", offset, size);
        return 0;
    }
    auto base = field<volatile uint8_t *>(access, 0x50);
    return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

// WhateverXe registers the exact native manager/platform after its donor ABI
// and board-policy checks. The provider independently verifies physical identity;
// it does not read plugin boot arguments or depend on framebuffer globals.
// The provider pins the immutable binding until reboot, like the donor owner.
extern "C" uint32_t xelpProbeCapabilities(uint32_t schema,uint32_t configABI) {
    return schema==XELP::ProbeCapabilityABI && configABI==XELP::ProbeABI ? XELP::ProbeDisplay13ComboDP|XELP::ProbeDisplay13FixedTC|XELP::ProbeDisplay13FixedDP|XELP::ProbeDisplay13Panel|XELP::ProbeDisplay13DPAlt : 0;
}
extern "C" bool xelpConfigureDisplay(void *controller,const XELP::ProbeConfig *config) {
    if(!controller || !config || !config->valid())return false;
    auto service=static_cast<IOService *>(controller);
    if(service->getMetaClass()->getClassSize()<0x1C20)return false;
    auto pci=OSDynamicCast(IOPCIDevice,service->getProvider());
    if(!pci || !XELP::same(config->profile,XELP::deviceProfile(pci)))return false;
    auto access=static_cast<OSObject *>(manager(controller));
    auto platform=field<uint8_t *>(controller,0xD40);
    auto map=OSDynamicCast(IOMemoryMap,field<OSObject *>(controller,0xC70));
    if(!access || access->getMetaClass()->getClassSize()<0x68 || !platform || platform[0x12]!=9 ||
        !map || !map->getLength() || !map->getVirtualAddress() ||
        field<uintptr_t>(access,0x50)!=map->getVirtualAddress() || field<uint32_t>(access,0x60)!=map->getLength())return false;
    // Binding needs an honest retained mapping, not an observed BAR size.
    // ProbeIO checks the range of each generation-specific register read.
    const XELP::ProbeBinding candidate {reinterpret_cast<uintptr_t>(controller),reinterpret_cast<uintptr_t>(access),
        reinterpret_cast<uintptr_t>(platform),*config};
    if(__atomic_load_n(&probeBindingState,__ATOMIC_ACQUIRE)==2)return probeBinding.sameBinding(candidate);
    if(!__sync_bool_compare_and_swap(&probeBindingState,0,1))return false;
    service->retain();access->retain();map->retain();
    probeBinding=candidate;
    __atomic_store_n(&probeBindingState,2,__ATOMIC_RELEASE);
    return true;
}
// Retain the ABI1 export for preceding WEG builds. New callers use the shared
// typed contract; both entries publish the same owner and implementation.
extern "C" bool xelpConfigurePortProbe(void *controller,uint32_t abi,uint32_t selected,uint32_t backend) {
    if(abi!=1 || !controller)return false;
    auto service=static_cast<IOService *>(controller);
    if(service->getMetaClass()->getClassSize()<0x1C20)return false;
    auto pci=OSDynamicCast(IOPCIDevice,service->getProvider());
    if(!pci)return false;
    XELP::ProbeConfig config;config.profile=XELP::deviceProfile(pci);
    if(config.profile.family!=XELP::Family::AlderLakeN)return false;
    config.combo=selected;config.comboBackend=backend;
    return xelpConfigureDisplay(controller,&config);
}

struct ProbeIO {
    void *access;
    bool readable(uint32_t offset) const {
        const auto bytes=field<uint32_t>(access,0x60);
        return field<uintptr_t>(access,0x50) && bytes>=4 && offset<=bytes-4;
    }
    uint32_t read(uint32_t offset){return read32(access,offset);}
};

extern "C" uint32_t tglRead32(void *, unsigned long)
    asm("__ZN31AppleIntelFramebufferController14ReadRegister32Em");
extern "C" uint32_t tglRead32(void *controller, unsigned long offset) {
    if (!hardwareAllowed(controller)) return 0;
    return read32(manager(controller), offset);
}

extern "C" void tglWrite32(void *, unsigned long, uint32_t)
    asm("__ZN31AppleIntelFramebufferController15WriteRegister32Emj");
extern "C" void tglWrite32(void *controller, unsigned long offset, uint32_t value) {
    if (!hardwareAllowed(controller)) return;
    // Preserve virtual dispatch to AppleIntelDSBAccessManager when selected.
    auto access = manager(controller);
    method<void (*)(void *, unsigned long, uint32_t)>(access, 0x130)(access, offset, value);
}

extern "C" uint64_t tglRead64(void *, volatile void *, unsigned long)
    asm("__ZN31AppleIntelFramebufferController14ReadRegister64EPVvm");
extern "C" uint64_t tglRead64(void *controller, volatile void *base, unsigned long offset) {
    if (!hardwareAllowed(controller)) return 0;
    auto access = manager(controller);
    const auto size = field<uint32_t>(access, 0x60);
    if (base == field<volatile void *>(access, 0x50) && (size < 8 || offset > size - 8)) {
        IOLog("AppleIntelTGLCompat: invalid MMIO read64 offset=%lx\n", offset);
        return 0;
    }
    return *reinterpret_cast<volatile uint64_t *>(static_cast<volatile uint8_t *>(base) + offset);
}

extern "C" void tglWrite64(void *, volatile void *, unsigned long, unsigned long long)
    asm("__ZN31AppleIntelFramebufferController15WriteRegister64EPVvmy");
extern "C" void tglWrite64(void *controller, volatile void *base, unsigned long offset,
                           unsigned long long value) {
    if (!hardwareAllowed(controller)) return;
    auto access = manager(controller);
    if (base == field<volatile void *>(access, 0x50)) {
        method<void (*)(void *, unsigned long, uint64_t)>(access, 0x138)(access, offset, value);
        return;
    }
    const auto size = field<uint32_t>(access, 0x60);
    if (size < 8 || offset > size - 8) {
        IOLog("AppleIntelTGLCompat: invalid MMIO write64 offset=%lx\n", offset);
        return;
    }
    *reinterpret_cast<volatile uint64_t *>(static_cast<volatile uint8_t *>(base) + offset) = value;
}

extern "C" uint64_t tglPMT(void *)
    asm("__ZN31AppleIntelFramebufferController9getPMTNowEv");
extern "C" uint64_t tglPMT(void *) {
    uint64_t ticks, ns;
    clock_get_uptime(&ticks);
    absolutetime_to_nanoseconds(ticks, &ns);
    return ns;
}

extern "C" {
uint32_t tglDSBBusy[3] asm("__ZN21AppleIntelDisplayPath19DSBEngineBusyStatusE") = {};
}
static_assert(sizeof(tglDSBBusy) == 12, "Three 32-bit DSB engine slots");

extern "C" IOReturn tglSetupDSB(void *)
    asm("__ZN31AppleIntelFramebufferController16hwSetupDSBMemoryEv");
extern "C" IOReturn tglSetupDSB(void *controller) {
    if (!hardwareAllowed(controller)) return kIOReturnUnsupported;
    auto &stored = field<IOBufferMemoryDescriptor *>(controller, 0xcd8);
    if (stored) return kIOReturnSuccess;
    const auto size = field<uint32_t>(controller, 0xdcc);
    const auto ggtt = field<uint64_t>(controller, 0xc50);
    auto ptes = field<volatile uint8_t *>(controller, 0xca0);
    auto access = manager(controller);
    const auto mmioSize = field<uint32_t>(access, 0x60);
    const auto mmioBase = field<uintptr_t>(access, 0x50);
    const auto pteBytes = field<uint32_t>(controller, 0xcec);
    // Exact donor FBMemMgr_Init produces 18 pages. Native start maps GGTT
    // at BAR0+8 MiB and records its independently sized PTE window at +CEC.
    // A full MMIO span is not the size of that GGTT window.
    if (page_size != 4096 || size != 0x12000 || (ggtt & 4095) || !ptes ||
        !mmioBase || mmioSize < 0x800000 || mmioBase > UINTPTR_MAX - mmioSize ||
        reinterpret_cast<uintptr_t>(ptes) != mmioBase + 0x800000 ||
        pteBytes < 8 || pteBytes > mmioSize - 0x800000 ||
        ggtt > 0x100000000ULL - size ||
        ((ggtt + size - 4096) >> 9) > pteBytes - 8)
        return kIOReturnBadArgument;
    auto buffer = IOBufferMemoryDescriptor::withOptions(0x90, size, page_size);
    if (!buffer) return kIOReturnNoMemory;
    IOByteCount length = 0;
    const auto options = field<uint32_t>(controller, 0xcf0);
    const auto physical = buffer->getPhysicalSegment(0, &length, options);
    auto bytes = buffer->getBytesNoCopy();
    constexpr uint64_t mask = 0x7ffffff000ULL;
    if (!bytes || length < size || !physical || (physical & 4095) ||
        physical > mask || size - 4096 > mask - physical) {
        buffer->release();
        return kIOReturnNoResources;
    }
    bzero(bytes, size);
    for (uint64_t i = 0; i < size; i += 4096) {
        auto pte = reinterpret_cast<volatile uint64_t *>(ptes + ((ggtt + i) >> 9));
        *pte = ((physical + i) & mask) | 7;
    }
    __asm__ volatile("mfence" ::: "memory");
    // Ownership is pinned until reboot in this bring-up revision. Do not unload
    // while the driver can DMA; a final stop hook must quiesce and release it.
    stored = buffer;
    IOLog("AppleIntelTGLCompat: DSB allocated size=%x physical=%llx ggtt=%llx length=%llu\n",
          size, static_cast<unsigned long long>(physical),
          static_cast<unsigned long long>(ggtt), static_cast<unsigned long long>(length));
    return kIOReturnSuccess;
}

extern "C" uint32_t tglProbePort(void *)
    asm("__ZN17AppleIntelPortHAL13probePortModeEv");
extern "C" uint32_t tglProbePort(void *port) {
    if (!port) return 0;
    if(__atomic_load_n(&probeBindingState,__ATOMIC_ACQUIRE)==2) {
        if(static_cast<OSObject *>(port)->getMetaClass()->getClassSize()<0x6B0)return 0;
        auto access=field<void *>(port,0x578);
        auto config=field<const uint32_t *>(port,0x548);
        const auto pointer=reinterpret_cast<uintptr_t>(config);
        if(pointer<probeBinding.platform+0x20 || pointer>=probeBinding.platform+0x20+9*24 ||
            (pointer-probeBinding.platform-0x20)%24 || access!=reinterpret_cast<void *>(probeBinding.access))return 0;
        ProbeIO io {access};
        return XELP::probeMode(io,probeBinding,reinterpret_cast<uintptr_t>(access),pointer,config);
    }
    // ABI0 TGL callers have no binding entry. Only a manager established by
    // hardwareAllowed with a validated TGL profile may use the legacy path.
    // ADL-S establishes generic access but never this unbound TGL fallback.
    auto access = field<void *>(port, 0x578);
    if (!access || access != __atomic_load_n(&legacyTGLAccess, __ATOMIC_ACQUIRE)) return 0;
    auto config = field<void *>(port, 0x548);
    if(!config)return 0;
    ProbeIO io {access};
    return XELPPortProbe::tglMode(io,field<uint32_t>(config,0));
}

extern "C" kern_return_t tglStart(kmod_info_t *, void *) {
    if (!WEXGraphicsActivation::ready()) {
        IOLog("AppleIntelTGLCompat: WEX inactive; leaving firmware graphics in control\n");
        return KERN_FAILURE;
    }
    if (XELP::kernelOS(version)==XELP::OS::Unknown)return KERN_FAILURE;

    // Backend selection is per physical owner/validated binding, not a global
    // boot policy. Register/DSB and port operations enforce their own guards.
    IOLog("AppleIntelTGLCompat: revision 0.5.0 started, physical-owner/binding selection\n");
    return KERN_SUCCESS;
}
extern "C" kern_return_t tglStop(kmod_info_t *, void *) { return KERN_FAILURE; }
KMOD_EXPLICIT_DECL(org.b00t0x.AppleIntelTGLCompat, "0.5.0", tglStart, tglStop)
extern "C" {
__attribute__((visibility("hidden"))) kmod_start_func_t *_realmain = tglStart;
__attribute__((visibility("hidden"))) kmod_stop_func_t *_antimain = tglStop;
__attribute__((visibility("hidden"))) int _kext_apple_cc = __APPLE_CC__;
}
