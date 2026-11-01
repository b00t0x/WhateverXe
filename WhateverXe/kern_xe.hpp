//
//  kern_xe.hpp
//  WhateverGreen
//
//  Copyright © 2018 vit9696. All rights reserved.
//

#ifndef kern_xe_hpp
#define kern_xe_hpp

#include "kern_tgl_tc_boot.hpp"
#include "kern_dsi_boot.hpp"
#include "kern_dsi_apple_timing.hpp"
#include "kern_dsi_ggtt.hpp"
#include "kern_dsi_surface.hpp"
#include "kern_dsi_quiesce.hpp"
#include "kern_dsi_clock.hpp"
#include "kern_dsi_commands.hpp"
#include "kern_dsi_panel.hpp"
#include "kern_dsi_backlight.hpp"
#include "kern_dsi_restart.hpp"
#include "kern_tgl_tc_native.hpp"
#include "kern_tgl_native_fia.hpp"
#include "kern_tgl_hdmi_wiring.hpp"
#include "kern_tgl_combo_dp.hpp"
#include "kern_display13_hdmi_wiring.hpp"
#include "kern_xelp_platform.hpp"
#include "kern_xelp_sst_program.hpp"
#include "kern_vbt.hpp"
#include "kern_alderlake_s.hpp"
#include "kern_xelp_panel_pwm.hpp"
#include "kern_xelp_donor.hpp"
#include "kern_xelp_display_contract.hpp"
#include "kern_xelp_link_requirements.hpp"

#include "kern_dvmt.hpp"
#include <Headers/kern_patcher.hpp>
#include <Headers/kern_devinfo.hpp>
#include <Headers/kern_cpu.hpp>
#include <Headers/kern_disasm.hpp>
#include <IOKit/IOService.h>
#include <IOKit/IOLocks.h>

class IOPCIDevice;
class IOInterruptEventSource;
namespace Display13Clock { struct Readout; }
namespace Display13WM { struct Latch; }
namespace XELP { namespace DSI { struct Profile; } }

class XE {
public:
	void init();
    void selectProfile(const XELP::Profile &profile);
	void deinit();

	/**
	 *  Property patching routine
	 *
	 *  @param patcher  KernelPatcher instance
	 *  @param info     device info
	 */
	void processKernel(KernelPatcher &patcher, DeviceInfo *info);

	/**
	 *  Patch kext if needed and prepare other patches
	 *
	 *  @param patcher KernelPatcher instance
	 *  @param index   kinfo handle
	 *  @param address kinfo load address
	 *  @param size    kinfo memory size
	 *
	 *  @return true if patched anything
	 */
	bool processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

private:
    bool adlpLoadOnly {};
    bool processADLPLoadOnly(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    static IOService *rejectADLPFramebufferProbe(IOService *,IOService *,SInt32 *);
    static IOService *rejectADLPAcceleratorProbe(IOService *,IOService *,SInt32 *);
    static bool rejectADLPStart(IOService *,IOService *);
 XELP::DisplayContract::Mask display12Available() const;
 XELP::DisplayContract::Mask display13Available() const;
	// Do not inline either helper into the small kext-load dispatcher.
	__attribute__((noinline)) bool processAlderLakeNLoadOnly(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processAlderLakeNRejectedAccelerator(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	static IOService *wrapAlderLakeNRejectedAcceleratorProbe(IOService *that,IOService *provider,SInt32 *score);
	__attribute__((noinline)) bool processAlderLakeNParentBoundary(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processXELPMemoryStage(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processAlderLakeNConfigStage(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processAlderLakeNEventStage(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processDisplay13Stage(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processAlderLakeNTaskBoundary(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processDisplay13Framebuffer(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processTGLAccelerator(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	__attribute__((noinline)) bool processKextFull(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	// Separate from generation-selected SKL/ICL patch modules.
	bool tglRequested {false};
	bool tglActive {false};
	bool adlnRequested {false};
	bool adlnDetected {false};
    // Render and Display selections are independent; shared PHY/port code
    // must not depend on which GT implementation the product uses.
    bool display12SmallGTDetected {},display12SmallGTReady {};
    bool tglGT2Detected {},tglGT2Ready {};
    XELP::GTResources tglGT2Resources;
    bool adlsDisplaySelected {},adlsDisplayHooksReady {};
    bool adlsPortInitReady {},tglHPDHookReady {};
    bool adlsPLLTableReady {},adlsClockReady {},adlsPowerReady {},adlsPanelPWMReady {};
 XELP::PanelPWM adlsPanelPWM;
 mach_vm_address_t orgADLSLightUpEDP {},orgADLSPanelBacklight {};
 static bool adlsPanelPWMTarget(void *controller,void *&access,void *&power);
 static bool adlsCapturePanelPWM(void *controller);
 static uint32_t wrapADLSLightUpEDP(void *controller,void *fb,void *path,const void *timing);
 static uint32_t wrapADLSPanelBacklight(void *controller,uint32_t brightness);
 static void wrapADLSPanelPWMEnable(void *access);
    mach_vm_address_t orgADLSPowerPG {};
    static uint8_t *adlsPowerTarget(void *power);
    static void wrapADLSPowerPG(void *power,bool enable,uint32_t requested);
    mach_vm_address_t orgComboEnableClocks {};
    static bool adlsClockTarget(void *controller,void *port,void *otherPort);
    mach_vm_address_t framebufferClockImage {};
    static bool comboClockTarget(void *,void *,void *);
    static bool comboClockPower(void *,bool);
    static uint32_t comboClockRead(void *,unsigned long);
    static void comboClockWrite(void *,unsigned long,uint32_t);
    static void comboClockTranslateWrite(void *,unsigned long &,uint32_t &,uintptr_t);
    static void wrapTGLComboEnableClocks(void *,void *,void *,uint32_t *);
    static bool comboBeginClock(void *controller,void *port,bool enable,bool &keepPLL,void *other=nullptr);
    static void comboEndClock();
    static void wrapADLSEnableClocks(void *controller,void *port,void *otherPort,uint32_t *params);
    mach_vm_address_t orgADLSPortInit {};
    static bool wrapADLSPortInit(void *port,uint32_t *config);
    XELP::SmallGTResources display12SmallGT;
    XELP::Profile xeProfile;
    XELP::VBT::Ports firmwarePorts;
    void xelpReadFirmware(IOPCIDevice *pci);
    void xelpLoadDisplayPool(IOService *provider);
    bool xelpInstallHostRPS(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    uint32_t adlnNativeSubslices {}, adlnL3Banks {}, adlnNativeEUs {};
	bool adlnKernelReady {false};
	bool adlnNativeRequested {false};
    // Display handoff needs GOP/GGTT, mapper and aperture qualification,
    // independently of GT fuses or an accelerator being allowed to start.
    bool display13BootReady {false};
    OSData *dsiFirmware {}; // Private immutable copy backing profile/sequence spans.
    XELP::DSI::Profile dsiProfile;
    XELP::DSI::Boot dsiBoot;
    XELP::DSI::GGTTMode dsiGGTT;
    bool prepareDSIFirmware(IOService *);
    bool dsiResourcesReady {};
    bool bindDSIResources(IOService *);
    XELP::DSI::Quiesce::Result dsiStop;
    XELP::DSI::Clock::Result dsiClockStop;
    bool dsiStopAttempted {},dsiIRQParked {},dsiDDIReleased {},dsiMemoryReleased {};
    bool quiesceDSIPath(IOService *,IOService *,OSObject *,bool retain=false);
    bool resumeDSIPath(IOService *,IOService *,OSObject *);
    XELP::DSI::Backlight dsiBacklight;
    uint32_t setDSIBrightness(IOService *,uintptr_t);
    uint32_t saveDSIBrightness(IOService *,uintptr_t);
    void publishDSIBacklight();
    static void recordDSIAttribute(IOService *,unsigned,uint32_t,uintptr_t,uint32_t);
    bool processDSIPathDisposal(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    bool disposeDSIPath(IOService *,OSObject *);
    mach_vm_address_t dsiFreePlane {},dsiFreeScaler {},dsiDetachPath {},dsiRemovePath {},dsiPipeMap {};
    bool dsiPGReleased {};
    static uint32_t wrapDisplay13ProbeMode(OSObject *);
    bool dsiCacheHooksReady {};
    mach_vm_address_t orgDSICache[2] {};
    bool processDisplay13DSICache(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    bool updateDSIBootCache(IOService *,IOService *,OSObject *);
    bool adoptDSIBootPath(IOService *,OSObject *);
    static void wrapDSIUpdateCache(IOService *,IOService *,OSObject *);
    static void wrapDSIGetCRTC(IOService *,IOService *,OSObject *);
    bool dsiBootProbeReady {};
    mach_vm_address_t orgDSIBootProbe {};
    bool processDisplay13DSIBoot(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    static uint32_t wrapDSIBootProbe(IOService *,bool *,uint32_t *);
    // Render additionally requires the native GT resource certificate.
    bool adlnNativeReady {false};
	bool display13FramebufferRequested {false};
	bool display13FBPowerReferencesReady {false};
	bool display13FBClockReadoutHooksReady {false};
	bool display13FBClockSetHookReady {false};
	bool display13FBEngineEnableHookReady {false};
	bool display13FBLifecycleHooksReady {false};
	bool display13FBDBUFHooksReady {false};
	bool display13FBWatermarkLayoutReady {false};
	bool display13FBWatermarkHooksReady {false};
	bool display13FBOutputHooksReady {false};
 bool display13FBSSTHooksReady {};
 bool display13FBAUXHooksReady {};
 mach_vm_address_t display13AUXControllerSlot {};
 mach_vm_address_t orgDisplay13ReadAUX {},orgDisplay13WriteAUX {},orgDisplay13I2CAUX {},orgDisplay13RunAUX {};
 bool processDisplay13FramebufferAUX(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static bool display13AUXReady(void *port,bool command,bool retained=false);
 static uint32_t wrapDisplay13ReadAUX(void *port,uint32_t address,void *data,uint32_t length);
 static uint32_t wrapDisplay13WriteAUX(void *port,uint32_t address,void *data,uint32_t length);
 static uint32_t wrapDisplay13I2CAUX(void *port,uint32_t command,uint32_t *packet,uint32_t length,uint8_t flag);
 static uint32_t wrapDisplay13RunAUX(void *port,uint32_t command,uint32_t *packet,uint32_t length,bool flag);
 mach_vm_address_t orgDisplay13TrainingPattern {},orgDisplay13SignalLevels {};
 mach_vm_address_t orgDisplay13EnablePort {},orgDisplay13DisablePort {};
 static bool display13MainLinkAUX(void *port,bool acquire,bool stopping=false);
 static uint32_t wrapDisplay13EnablePort(void *port);
 static uint32_t wrapDisplay13DisablePort(void *port);
 bool processDisplay13FramebufferSST(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 bool display13TrainingReady {},display13FramingReady {},display13ClockOrderReady {};
 mach_vm_address_t orgDisplay13PortTraining {};
 bool processDisplay13Training(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static bool display13LeaseValidate(void *controller,void *port,uint32_t pipe);
 static bool display13LeasePower(void *power,uint32_t pg,bool acquire);
 static bool display13LeaseStop(void *controller,void *port);
 static bool display13TrainingPath(void *path,uint32_t &ddi,uint32_t &pipe);
 static uint32_t wrapDisplay13PortTraining(void *port,void *request);
 static bool display13StopTranscoder(void *port);
 static bool display13DeferClock(void *access,uint64_t reg,uint32_t value,uintptr_t caller);

 static uint8_t *display13SSTPort(void *port);
 static bool display13HDMIPort(void *port);
 static uint32_t wrapDisplay13TrainingPattern(void *path,uint32_t pattern);
 static uint32_t wrapDisplay13SignalLevels(void *port,uint32_t swing,uint32_t emphasis,void *overrideConfig);
	mach_vm_address_t xelpProviderProbe {},xelpProviderConfigureProbe {};
 bool display13DPAltProbeReady {},display13DPProbeReady {},display13TCProbeReady {},display13TCDPProbeReady {},display13FBDPHooksReady {};
 bool adlpDisplaySelected {};
 bool adlpRenderRequested {};
 Display13OutputWiring display13BoardWiring(IOService *);
 bool prepareDisplay13BootScanout(IOPCIDevice *);
    bool xelpBindProbe(void *controller);
	bool display13FBTopologyHooksReady {false};
    bool display13DSIModeHooksReady {};
    mach_vm_address_t orgDSIMode[9] {},orgDSIScalerSupport {},orgDSIDetailedValidation {},orgDSISourceValidation {},orgDSIPixelInfo {};
    bool processDisplay13DSIModes(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    static void recordDSIClient(IOService *,unsigned,uint64_t,uint32_t);
    bool releaseDSIFixedMode(IOService *);
    bool bindDSIFixedMode(IOService *,const XELP::DSI::Profile &);
    static uint32_t setDSIPortMode(OSObject *,uint32_t);
    static uint32_t wrapDSIDDC(IOService *,uint32_t,uint8_t *,uint64_t *,uint32_t,OSObject *);
    static uint32_t wrapDSIGetCurrentMode(IOService *,int32_t *,int32_t *);
    static uint32_t wrapDSIGetOnlineInfo(IOService *,OSObject *,uint8_t *,uint8_t *);
    static uint32_t wrapDSISetDetailedTimings(IOService *,OSArray *);
    static uint32_t wrapDSISetDisplayMode(IOService *,int32_t,int32_t);
    static bool dsiNativeUpdateAllowed(IOService *);
    static bool dsiSharedWriterStopped(IOService *,unsigned);
    bool processDSINativeWriters(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    bool processDSINativeTransactions(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    bool processDSINativeAttributes(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    mach_vm_address_t orgDSIAttributes[4] {};
    static uint32_t wrapDSISetStream(IOService *,IOService *,OSObject *,const void *);
    static uint32_t wrapDSISetAttribute(IOService *,uint32_t,uintptr_t);
    static uint32_t wrapDSISetConnectionAttribute(IOService *,int32_t,uint32_t,uintptr_t);
    static uint32_t wrapDSISetPowerState(IOService *,uint32_t);
    mach_vm_address_t orgDSITransaction[5] {};
    static int beginDSIFramebufferWriter(IOService *);
    static uint32_t wrapDSIFlipTransaction(IOService *,void *,uint64_t,void *);
    static uint32_t wrapDSIGammaTransaction(IOService *,void *,void *);
    static uint32_t wrapDSICscTransaction(IOService *,void *,void *,uint64_t,bool);
    static uint32_t wrapDSIPlaneGammaTransaction(IOService *,void *,bool);
    static uint32_t wrapDSISetGammaTable(IOService *,uint32_t,uint32_t,uint32_t,void *,bool);
    bool dsiNativeWriterHooksReady {};
    bool dsiBackingHooksReady {};
    bool dsiPanelOnly {};
    bool dsiCommandTest {};
    bool dsiPanelTest {};
    bool dsiHostTest {};
    bool dsiStopTest {};
    bool dsiPowerTest {};
    struct {
        bool sleeping {},failed {};uint32_t cycles {};
        XELP::DSI::Surface::Plane plane;
        uint32_t cursorControl {},cursorPosition {},cursorBase {};
        XELP::DSI::Restart::State host;
        XELP::DSI::Panel::Result panel;
    } dsiRuntimePower;
    XELP::DSI::Panel::Shutdown dsiPanelStop;
    XELP::DSI::Restart::State dsiHostRestart;
    XELP::DSI::Panel::Result dsiPanelTrial;
    bool probeDSIPanel(IOService *,bool prepareExternalClock=false);
    XELP::DSI::Host dsiCommandHost;
    bool probeDSICommands(IOService *);
    mach_vm_address_t orgDSIBacking[2] {};
    bool processDSIBackingHooks(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    mach_vm_address_t dsiComputeGeometry {},dsiModeCPU[4] {};
    bool publishDSINativeBacking();
    bool publishDSINativeMode(IOService *);
    uint32_t setDSIPresentation(IOService *,uint32_t,const XELP::DSI::AppleAdapter::Timing &);
    bool releaseDSILogical(IOService *);
    bool probeDSIModeGeometry(IOService *);
    bool prepareDSINativeBacking(IOService *);
    bool validateDSINativeBacking();
    bool initializeDSINativeBacking();
    bool switchDSINativeSurface();
    bool dsiArmedBackingOwned(IOService *,OSObject *);
    bool retireDSINativeBacking(IOService *,OSObject *);
    XELP::DSI::Session dsiSession;
    XELP::DSI::Session::Cleanup dsiCleanup;
    bool discardDSINativeBacking(IOService *);
    void probeDSINativeBacking(IOService *);
    bool dsiBackingAttempted {};
    uint32_t dsiBackingStage {};
    static uint32_t wrapDSIBackingAllocate(OSObject *,void *,uint32_t);
    static uint32_t wrapDSIBackingFree(OSObject *,void *,uint32_t);

    mach_vm_address_t orgDSICursor[2] {};
    static int beginDSINativeWriter(IOService *,IOService *);
    static void endDSINativeWriter(int);
    static uint32_t wrapDSICursorImage(IOService *,IOService *,void *);
    static uint32_t wrapDSICursorState(IOService *,IOService *,int32_t,int32_t,bool,bool);
    static uint32_t wrapDSIValidateMode(IOService *,int32_t,const void **,const void **);
    static uint32_t wrapDSIGetModes(IOService *,int32_t *);
    static uint32_t wrapDSIGetModeCount(IOService *);
    static uint32_t wrapDSIGetModeInformation(IOService *,int32_t,void *);
    static uint32_t wrapDSIPixelInfo(IOService *,int32_t,int32_t,int32_t,void *);
    static uint32_t wrapDSIValidateDetailed(IOService *,void *,uint64_t);
    static bool wrapDSIValidateSource(IOService *,void *);
    static bool wrapDSIScalerSupport(void *,uint32_t,uint32_t,uint32_t,uint32_t,uint64_t,uint32_t,bool);
	Display13OutputWiring adlnOutputWiring;
 mach_vm_address_t adlnComboPLLFractionCaller {};
	uint64_t adlnPlatform[0x1C0/8] {};
	IOService *adlnTopologyController {};
	mach_vm_address_t orgAlderLakeNGetOSInformation {},orgAlderLakeNPortState {};
	bool display13FBMemoryHooksReady {false};
	bool display13FBGTTHooksReady {false};
	mach_vm_address_t orgDisplay13FBMemoryInit {};
	__attribute__((noinline)) bool processDisplay13FramebufferGTT(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	static void wrapDisplay13FBMemoryInit(IOService *controller);
	static uint64_t wrapAlderLakeNGTTRead(IOService *controller,volatile void *base,unsigned long offset);
	static IOReturn wrapDisplay13CursorSetup(IOService *controller);
	static IOReturn wrapAlderLakeNDSBSetup(IOService *controller);
	static IOReturn adlnSetupAuxiliaryMemory(IOService *controller,bool dsb);
	mach_vm_address_t orgDisplay13FramebufferPlatformCall {};
	__attribute__((noinline)) bool processAlderLakeNSharedReservation(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	static IOReturn wrapDisplay13FramebufferPlatformCall(IOService *controller,const OSSymbol *name,bool wait,
		void *arg1,void *arg2,void *arg3,void *arg4);
	static uint32_t adlnRequestedSharedReservation(IOService *controller);
	mach_vm_address_t orgAlderLakeNSysMemAllocate {},orgAlderLakeNSysMemFree {};
	__attribute__((noinline)) bool processAlderLakeNSysMem(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	static void wrapAlderLakeNSysMemAllocate(IOService *controller,uint32_t slot);
	static void wrapAlderLakeNSysMemFree(IOService *controller,uint32_t slot);
	static void adlnSysMemOperation(IOService *controller,uint32_t slot,bool release);
	bool display13FBBootHandoffHookReady {false};
	// Native start requires all generation-specific backend routes. These
	// describe installed routes, not successful runtime ownership or scanout.
	bool display13FBPowerBackendReady {false};
	bool display13FBClockReady {false}, display13FBIRQReady {false}, display13FBMemoryReady {false}, display13FBABIReady {false};
	mach_vm_address_t orgAlderLakeNPowerReferences[7] {};
	mach_vm_address_t orgAlderLakeNClockInit {}, orgAlderLakeNClockProbe {};
	mach_vm_address_t orgAlderLakeNClockSet {};
	mach_vm_address_t orgAlderLakeNScanoutAllocate {}, orgAlderLakeNScanoutSetup {}, orgAlderLakeNAperture {};
	mach_vm_address_t adlnScanoutDisableFB {};
	mach_vm_address_t orgAlderLakeNSetupWorkLoop {};
	__attribute__((noinline)) bool processDisplay13FramebufferOutput(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
    bool display13FBTCHooksReady {};
    mach_vm_address_t orgDisplay13TCMode {},display13TCBaseMode {},orgDisplay13TCEnableClocks {},orgDisplay13TCDisableClocks {};
    mach_vm_address_t orgDisplay13TCLightUp {},orgDisplay13TCSetMode {},display13TCNotify {};
    mach_vm_address_t display13TCFullClocks {},display13TCPlane {},display13TCScaler {},display13TCPipe {},display13TCPackets {};
    static uint32_t wrapDisplay13DPLightUp(void *,void *,void *,const void *);
 static void wrapDisplay13DPPhyGating(void *,bool);
 static uint32_t wrapDisplay13DPLaneMask(void *);
 static void wrapDisplay13DPPortWrite(void *,uint32_t,uint32_t);
 mach_vm_address_t orgDisplay13DPLightUp {},display13DPTraining {},display13DPSDPs {};
 mach_vm_address_t orgDisplay13DPPhyGating {},orgDisplay13DPLaneMask {},orgDisplay13DPPortWrite {},display13DPImage {};
 static uint32_t wrapDisplay13TCLightUp(void *,void *,void *,const void *);
    static uint32_t wrapDisplay13TCSetMode(void *,void *,const void *);
    bool processDisplay13FramebufferTC(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    static unsigned display13TCTarget(OSObject *,bool cleanup=false);
    static uint32_t wrapDisplay13TCMode(OSObject *,uint32_t);
    static void drainDisplay13TCMode(unsigned);
    static void wrapDisplay13TCEnableClocks(void *,void *,void *,uint32_t *);
    static void wrapDisplay13TCDisableClocks(void *,void *,void *);
    static uint32_t wrapDisplay13TCSignal(void *,uint32_t,uint32_t,void *);
	__attribute__((noinline)) bool processDisplay13FramebufferTopology(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	static bool wrapAlderLakeNGetOSInformation(IOService *controller);
	static uint32_t wrapAlderLakeNPortState(OSObject *port);
    mach_vm_address_t orgDisplay13GMBus {},orgDisplay13GMBusRead {},orgDisplay13GMBusWrite {},adlnGMBusFunction {};
    mach_vm_address_t orgDisplay13FBEnable {},orgAlderLakeNConnectionAttr {},orgAlderLakeNPortDDC {},orgDisplay13FBDDC {};
    static uint32_t wrapDisplay13FBEnable(IOService *fb);
    bool processFramebufferAGDC(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    bool processDisplay13TrialAGDC(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    static uint32_t wrapDisplay13TrialCommit(void *,void *,void *);
    mach_vm_address_t orgDisplay13TrialCommit {};
    mach_vm_address_t orgFramebufferUserClient {},orgFramebufferAGDCCallback {};
    static uint32_t wrapFramebufferUserClient(IOService *,void *,void *,uint32_t,void **);
    static uint32_t wrapFramebufferAGDCCallback(IOService *,uint32_t,unsigned long,uint32_t);
    static uint32_t wrapAlderLakeNConnectionAttr(IOService *fb,int connection,uint32_t attribute,unsigned long *value);
    static uint32_t wrapDisplay13FBDDC(IOService *fb,int connection,uint32_t block,uint32_t segment,uint32_t type,uint8_t *data,uint64_t *length);
    static uint32_t wrapAlderLakeNPortDDC(OSObject *port,uint32_t block,uint8_t *data,bool force);
    static uint32_t wrapDisplay13GMBusRead(void *access,uint64_t reg);
    static void wrapDisplay13GMBusWrite(void *access,uint64_t reg,uint32_t value);
    mach_vm_address_t orgAlderLakeNGamma10 {},adlnGammaTableUpdate {},orgAlderLakeNPreGamma {};
    bool processDisplay13FramebufferColor(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
    static void wrapAlderLakeNGamma10(void *path,void *transaction,uint32_t control);
    static void wrapAlderLakeNPreGamma(void *path,void *transaction,uint32_t control);
    static void wrapAlderLakeNGammaTable(void *path,uint16_t *table);
    static uint32_t wrapDisplay13GMBus(IOService *controller,uint32_t bus,uint32_t speed,void *request);
	static bool wrapDisplay13FramebufferStart(IOService *that, IOService *provider);
	static void wrapAlderLakeNPowerInit(void *power, void *controller);
	static void wrapAlderLakeNEngineEnable(void *power);
	static void wrapAlderLakeNEngineDisable(void *power);
	static void wrapAlderLakeNPowerOverride(void *power,bool enable);
	static void wrapAlderLakeNPowerRestore(void *power,bool waking);
	__attribute__((noinline)) bool processDisplay13FramebufferLifecycle(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	mach_vm_address_t orgAlderLakeNEngineEnable {};
	mach_vm_address_t orgAlderLakeNDisableDisplay {};
	static void wrapAlderLakeNDisableDisplay(IOService *controller,IOService *fb,OSObject *path,uint8_t mode);
	__attribute__((noinline)) bool processDisplay13FramebufferDBUF(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	__attribute__((noinline)) bool processDisplay13FramebufferWatermarks(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	__attribute__((noinline)) bool processDisplay13FramebufferWMBackend(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	mach_vm_address_t orgAlderLakeNLatencyInit {},orgAlderLakeNPlaneWMSetup {},orgDisplay13CursorWMSetup {},adlnAdjustPixelClock {};
	static void wrapAlderLakeNLatencyInit(IOService *controller);
	static void wrapAlderLakeNPipeWMSetup(IOService *controller,IOService *framebuffer,OSObject *path,void *params);
	mach_vm_address_t orgAlderLakeNMailboxRead {},orgAlderLakeNMailboxReadData {},orgAlderLakeNMailboxWrite {};
	static uint32_t wrapAlderLakeNMailboxRead(IOService *controller,uint32_t command);
	static uint32_t wrapAlderLakeNMailboxReadData(IOService *controller,uint32_t command,uint32_t data);
	static void wrapAlderLakeNMailboxWrite(IOService *controller,uint32_t command,uint32_t data,bool sleep);
	static uint32_t adlnMailbox(IOService *controller,uint32_t command,uint32_t data,unsigned kind,bool sleep);
	static void wrapAlderLakeNPlaneWMSetup(OSObject *plane,OSObject *path);
	static void wrapDisplay13CursorWMSetup(OSObject *plane,OSObject *path);
	static void adlnWMSetup(OSObject *plane,OSObject *path,bool cursor);
	mach_vm_address_t orgAlderLakeNPlaneWMEnable {},orgDisplay13CursorWMEnable {},adlnWMPrepareTrigger {},adlnWMArmUpdate {},adlnWMWrite32 {};
	static void adlnWMEnable(OSObject *plane,bool arm,bool cursor);
	static void wrapAlderLakeNPlaneWMEnable(OSObject *plane,bool arm);
	static void wrapDisplay13CursorWMEnable(OSObject *plane,bool arm);
	static void wrapDisplay13WMArmUpdate(IOService *framebuffer);
    static void display13WMArmUpdate(IOService *framebuffer);
	static Display13WM::Latch adlnVerifyWMLatch(IOService *framebuffer,uint64_t nativeHz,uint64_t targetHz);
	mach_vm_address_t orgDisplay13WMArmUpdate {};
	static void wrapAlderLakeNPrimaryDBUF(OSObject *plane,uint32_t start,uint32_t end,bool wait);
	mach_vm_address_t orgAlderLakeNPrimaryDBUF {};
	static void wrapAlderLakeNPlanarDBUFWrite(void *access,uint64_t reg,uint32_t value);
	static void wrapDisplay13CursorDBUF(OSObject *plane);
	static void wrapAlderLakeNDefaultDBUF(IOService *controller);
	static void wrapAlderLakeNOptimizedDBUF(IOService *controller);
	static void adlnSetupDBUF(IOService *controller,bool optimized);
	static void adlnSetupDBUFPipe(IOService *controller,IOService *framebuffer,unsigned pipe,bool optimized);
	static void wrapAlderLakeNPowerGetPG(void *power, uint32_t pg);
	static void wrapAlderLakeNPowerPutPG(void *power, uint32_t pg);
	static void wrapAlderLakeNPowerGetDDI(void *power, uint32_t ddi);
	static void wrapAlderLakeNPowerPutDDI(void *power, uint32_t ddi);
	static void wrapAlderLakeNPowerGetAUX(void *power, uint32_t aux);
	static void wrapAlderLakeNPowerPutAUX(void *power, uint32_t aux);
	static bool adlnPowerReference(void *power, uint32_t operation, uint32_t domain);
	static Display13Clock::Readout adlnClockReadout(IOService *controller);
	static void wrapAlderLakeNClockInit(IOService *controller);
	static uint64_t wrapAlderLakeNClockProbe(IOService *controller);
	static void wrapAlderLakeNClockSet(IOService *controller,uint64_t nativeHz);
	static void adlnSetDualClock(IOService *controller,uint64_t nativeHz);
	static void wrapAlderLakeNClockDisable(IOService *controller);
	static uint64_t wrapAlderLakeNClockCalculate(IOService *controller,uint64_t minimumHz);
	mach_vm_address_t adlnHasExternalDisplay {};
	static bool wrapAlderLakeNScanoutAllocate(IOService *framebuffer);
	static void wrapAlderLakeNScanoutSetup(IOService *framebuffer);
	static void wrapAlderLakeNScanoutFree(IOService *framebuffer);
	static bool adlnFreeStoppedPipe(IOService *framebuffer);
	static void wrapAlderLakeNFreePath(IOService *framebuffer,OSObject *path);
	mach_vm_address_t orgAlderLakeNFreePath {};
    bool processDisplay13Retirement(KernelPatcher &,size_t,mach_vm_address_t,size_t);
    mach_vm_address_t orgDisplay13FreeResources {},orgDisplay13AllocateResources {};
    static uint32_t releaseDisplay13Path(IOService *,OSObject *);
    static uint32_t retireDisplay13Paths(void *);
    static uint32_t wrapDisplay13FreeResources(void *);
    static bool wrapDisplay13AllocateResources(void *);
    static bool display13RetirementHealthy();
    static bool retireDisplay13Controller(void *);
	mach_vm_address_t orgAlderLakeNScanoutFree {};
	static IOMemoryDescriptor *wrapAlderLakeNAperture(IOService *framebuffer,int aperture);
	static void wrapAlderLakeNSetupWorkLoop(IOService *controller,IOWorkLoop *workLoop);
	mach_vm_address_t orgDisplay13FBIRQ[5] {};
	mach_vm_address_t orgDisplay13FBSWIRQ {};
	mach_vm_address_t adlnWorkLoopInGate {},adlnWorkLoopOnThread {};
	mach_vm_address_t adlnWorkLoopTryClose {},adlnWorkLoopOpen {};
	__attribute__((noinline)) bool processDisplay13FramebufferIRQ(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
	static IOReturn wrapDisplay13FBEnableIRQ(IOService *controller);
	static IOReturn wrapDisplay13FBDisableIRQ(IOService *controller);
	static void wrapDisplay13FBConfigureVBL(IOService *controller,IOService *framebuffer,OSObject *path,bool enable,bool getLock);
	static void wrapDisplay13FBProcessDE(IOService *controller,uint32_t *pipe,uint32_t *aux,uint32_t *hotplug);
	static bool wrapDisplay13FBProcessIRQ(IOService *controller);
	static void wrapDisplay13FBSWIRQ(IOService *controller,IOInterruptEventSource *source,int count);
    void auditDSIBoot(IOPCIDevice *,volatile const uint8_t *,uint64_t,const AlderLakeN::DisplayReadout &,bool);
	void adlnBootstrap(IOPCIDevice *pci);
	uint16_t tglDeviceId {0}; // Physical PCI ID, not a DeviceProperties override.
	mach_vm_address_t orgTGLEnableCombo {}, orgTGLEngineEnable {}, orgTGLEngineDisable {};
	static void wrapTGLEnableCombo(void *port);
	static void wrapTGLEngineEnable(void *power);
	static void wrapTGLEngineDisable(void *power);
	static void tglObserveEngine(void *power, uint32_t event, bool done, void *caller);
	mach_vm_address_t orgTGLScanoutAllocate {}, orgTGLScanoutSetup {}, orgTGLPlaneUpdate {}, orgTGLAperture {};
	mach_vm_address_t orgTGLPlaneSetup {}, orgTGLColorMode {}, orgTGLPixelInfo {};
	mach_vm_address_t orgTGLModeTiming {};
	bool tglPanelDepthReady {false};
 bool display13PanelPWMReady {},display13PanelPowerReady {},display13PanelProbeReady {};
 mach_vm_address_t orgDisplay13PanelPower {};
 void processDisplay13Panel(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static bool display13PanelTarget(void *controller);
 static bool display13PanelHealthy();
 static bool display13PanelPowerReference(bool acquire);
 static bool display13PanelCapture(void *controller);
 static void display13PanelTiming(void *controller,bool apply);
 static uint32_t display13PanelBrightness(void *controller,uint32_t brightness);
 static void display13PanelPWMEnable(void *access);
 static uint32_t wrapDisplay13PanelPower(void *controller,uint32_t state);
 static void resetPanelDepth();
 static void cachePanelDepth(void *port,uint32_t block,const uint8_t *data,uint32_t result);
 void processNativePanel(KernelPatcher &patcher,size_t index,mach_vm_address_t address,size_t size);
 bool tglPanelTimingReady {};
 mach_vm_address_t orgTGLPanelTiming {},orgTGLPanelConfig {};
 uint32_t tglPanelTimingBIOS[3] {};
 bool tglPanelTimingBIOSValid {};
 static void tglApplyPanelTiming(void *controller,bool apply);
 static void wrapTGLPanelConfig(void *controller,uint32_t config);
 static uint32_t wrapTGLPanelTiming(void *controller);
	static uint32_t tglPanelDepthPort(void *port);
	static void wrapTGLModeTiming(void *controller,void *fb,void *timing);
	mach_vm_address_t orgTGLGamma10 {}, orgTGLRegisterWrite {}, orgTGLFastRegisterWrite {};
	static void wrapTGLGamma10(void *path, void *transaction, uint32_t control);
	static void wrapTGLRegisterWrite(void *access, unsigned long reg, uint32_t value);
	static void wrapTGLFastRegisterWrite(void *access, unsigned long reg, uint32_t value);
	bool tglInactivePorts {false};
	bool tglPG1Poll {false};
	bool tglClockOrderRequested {false};
	mach_vm_address_t tglClockOffCaller {}, orgTGLDisableClocks {};
	static volatile uint8_t *tglClockOrderTarget(void *controller, void *port, void *path, void *fb);
	static void wrapTGLDisableClocks(void *controller, void *port, void *otherPort);
	mach_vm_address_t tglPG1FuseReadCaller {};
	mach_vm_address_t tglPGFuseWaitFunction {};
	mach_vm_address_t tglComboPLLFractionCaller {};
	bool tglSinkFramingReady {};
	bool tglComboAUXGateReady {};
	mach_vm_address_t tglPortByIndex {};
	mach_vm_address_t orgTGLCheckLinkStatus {};
	static uint32_t wrapTGLCheckLinkStatus(void *port);
	mach_vm_address_t orgTGLOptimalLaneCount {};
	static uint32_t wrapTGLOptimalLaneCount(void *port, const void *timing, uint32_t bpp);
	static void tglObserveCursorMemory(void *fb);
	mach_vm_address_t orgTGLCursorSetup {}, orgTGLRestoreGTT {}, orgTGLCursorImage {};
	static uint32_t wrapTGLCursorSetup(void *controller);
	static void wrapTGLRestoreGTT(void *controller);
	static uint32_t wrapTGLCursorImage(void *controller, void *fb, void *info);
	static void tglObserveCursorLifetime(void *controller, void *fb, void *info, uint32_t event, uint32_t result);

	static void wrapTGLPlaneSetup(void *plane, void *path);
	static void wrapTGLColorMode(void *controller, void *fb, void *path, uint32_t mode, uint32_t depth, bool flag, const void *timing);
	static uint32_t wrapTGLPixelInfo(void *fb, int mode, int depth, int aperture, void *info);
	static bool wrapTGLScanoutAllocate(void *fb);
	static void wrapTGLScanoutSetup(void *fb);
	static void wrapTGLPlaneUpdate(void *plane, bool flag);
	static IOMemoryDescriptor *wrapTGLAperture(void *fb, int aperture);
	static void tglObserveScanout(void *fb, void *plane, uint32_t event, bool done, uint32_t result);
	bool tglPanelPowerPropertyReady {false};
	bool tglEDPTranscoderReady {false};
	static bool wrapTGLPanelPowerProperty(IORegistryEntry *framebuffer, const char *, OSObject *value);
	bool tglHDMIB {false};
 TGLHDMIWiring tglHDMIWiring;
 AlderLakeS::Wiring adlsWiring;
 bool tglHDMIWiringLoaded {},tglHDMIWiringExplicit {},tglHDMIWiringApplied {};
 uint32_t tglHDMIBackendMask {3}; // TGL-U combo PHY A/B; C is absent
 static bool tglLoadHDMIWiring(IOService *provider);
 static bool adlsPrepareBoot(IOService *provider);
 bool adlsBootConsumersFailed {false};
 bool tglHDMIEligible(const uint32_t *config) const {return tglHDMIWiringApplied && (adlsDisplaySelected?adlsWiring.matches(config,false):tglHDMIWiring.matches(config));}
 bool tglHDMIDDCEligible(uint32_t pin) const {return tglHDMIWiringApplied && (adlsDisplaySelected?adlsWiring.usesDDC(pin):tglHDMIWiring.usesDDC(pin));}

	bool tglGMBusReady {false};
	mach_vm_address_t orgTGLGMBus {};
	mach_vm_address_t orgTGLRegisterRead {}, tglGMBusFunction {};
	static uint32_t wrapTGLRegisterRead(void *access, unsigned long reg);
	static uint32_t wrapTGLGMBus(void *controller, uint32_t bus, uint32_t speed, void *request);
	bool tglTopologyReady {false};
	bool tglTrainingLeaseRequested {false}, tglTrainingLeaseReady {false};
	IORecursiveLock *tglPathPoolLock {};
	mach_vm_address_t orgTGLAllocatePathAny {}, orgTGLAllocatePathFixed {}, orgTGLControllerStop {};
	mach_vm_address_t tglDisablePort {}, tglDisableClocks {};
	// Shared donor protocol despite historical TGL names. Registration belongs
	// only to processFramebufferTraining; backends supply hardware adapters.
	// One reservation per physical port; the native pool limits simultaneous pipes.
	struct TGLTrainingLease {
		void *path {}, *port {}, *fb {}, *power {}, *adoptThread {};
		bool busy {}, powerOwned {}, adopted {}, clocksActive {}, prepared {};
        void *trainingThread {};
		// Native abort-End already ran; retry only our retained cleanup.
		bool endCleanupPending {};
		uint32_t generation {}, savedClock {}, savedFunc {}, savedTP {};
		uint32_t pipe {}, pg {1}, ddi {};
	};
	// Optional Display13 adapter. The native pool/adoption protocol is shared;
    // register domains, identity and power results remain backend-specific.
    uint32_t trainingLeaseDP13Mask {};
    bool (*trainingLeaseValidate)(void *,void *,uint32_t) {};
    bool (*trainingLeasePower)(void *,uint32_t,bool) {};
    bool (*trainingLeaseStop)(void *,void *) {};
    static constexpr uint32_t TGLPortCount = 9;
	TGLTrainingLease tglTrainingLeases[TGLPortCount] {};
	void *tglAllocatingFB[TGLPortCount] {};
	bool tglLeasesStopping {},tglStopRetained {},tglStopInProgress {},tglStopComplete {};
	static TGLTrainingLease *tglTrainingLeaseForDDI(uint32_t ddi);
	static TGLTrainingLease *tglAdoptingLease(); // caller holds the pool lock
	static bool tglLeaseFBReserved(void *fb, const TGLTrainingLease *except = nullptr);
	static void *wrapTGLAllocatePathAny();
	static void *wrapTGLAllocatePathFixed(uint32_t pipe);
	static void wrapTGLControllerStop(void *controller, IOService *provider);
	static uint32_t tglPrepareTrainingLease(void *controller, TGLTrainingLease &lease);
	static bool tglReleaseTrainingLease(TGLTrainingLease &lease, bool disableLink);
	static void tglObserveTrainingLease(const TGLTrainingLease &lease, uint32_t event, uint32_t result, uint32_t argument = 0);
	uint64_t tglPlatformCopy[0x1C0 / sizeof(uint64_t)] {};
	mach_vm_address_t orgTGLGetOSInformation {};
	bool tglStolenMemoryReady {};
	static bool wrapTGLGetOSInformation(void *controller);
	mach_vm_address_t orgTGLLightUpTMDS {};
	static uint32_t wrapTGLLightUpTMDS(void *controller, void *fb, void *path, const void *timing);
	bool tglProbeReady {false};
	bool tglDekelReady {false};
	bool tglPhyClockPreserveReady {false};
	mach_vm_address_t orgTGLEnablePort {};
	mach_vm_address_t orgTGLDisablePort {};
	bool tglComboAUXLifetimeReady {};
	void *tglComboAUXOwners[9] {};
	static void tglComboAUXReference(void *port,bool acquire);
	static uint32_t wrapTGLDisablePort(void *port);
	mach_vm_address_t orgTGLSignalLevels {};
	bool tglComboSignalReady {};
	static bool tglProgramComboSignal(void *port,uint32_t swing,uint32_t emphasis,void *overrideConfig,uint32_t &result);
	static uint32_t wrapTGLSignalLevels(void *port, uint32_t swing, uint32_t emphasis, void *overrideConfig);
	mach_vm_address_t orgTGLPhyClockGating {};
	static void wrapTGLPhyClockGating(void *port, bool gate);
	mach_vm_address_t orgTGLSetDklDPMode {};
	static void wrapTGLSetDklDPMode(void *port);
	mach_vm_address_t orgTGLPortWriteReg {};
	static void wrapTGLPortWriteReg(void *port, uint32_t reg, uint32_t value);
	static uint32_t wrapTGLEnablePort(void *port);
	static void tglSnapshotTransmitter(void *port, bool done, uint32_t result);
	void *tglObservationController {};
	mach_vm_address_t orgTGLMEConnect {};
	static bool wrapTGLMEConnect(void *client, void *data);
	mach_vm_address_t orgTGLMEIInit {}, orgTGLMEIInterrupt {}, orgTGLMEIFilter {};
	mach_vm_address_t orgTGLMEIRead {}, orgTGLMEIWrite {}, orgTGLMEIHeader {};
	static void wrapTGLMEIInit(void *driver);
	static void wrapTGLMEIInterrupt(void *driver, void *source, int count);
	static bool wrapTGLMEIFilter(void *driver, void *source);
	static uint32_t wrapTGLMEIHeader(void *driver);
	static uint32_t wrapTGLMEIRead(void *driver, void *buffer, uint32_t *length, uint8_t *complete);
	static uint32_t wrapTGLMEIWrite(void *driver, void *buffer, uint32_t *length, uint8_t me, uint8_t host, bool secure);
	// Admission of new PCODE users requires every known native writer to be
	// routed, including accelerator eDRAM. Installed wrappers always arbitrate;
	// changing readiness must never create an unprotected in-flight call.
	bool tglPcodeFBReady {false}, tglPcodeAcceleratorReady {false};
 static void tglRecordStartOrder(unsigned stage,void *object,unsigned ddi=9,
  uint64_t argument=0,uint64_t actual=0,uint64_t result=0);
	bool tglPcodeNativeReady() const {
		return __atomic_load_n(&tglPcodeFBReady,__ATOMIC_ACQUIRE) &&
		       __atomic_load_n(&tglPcodeAcceleratorReady,__ATOMIC_ACQUIRE);
	}
	mach_vm_address_t orgTGLMailboxRead {}, orgTGLMailboxReadData {}, orgTGLMailboxWrite {};
	static void tglEDRAMPCODE(void *manager);
 static void tglBeginTCColdProtection(void *controller);
 bool tglBootReadoutReady {};mach_vm_address_t orgTGLPowerInit {};
 TGLTCNativeFunctions tglTCNativeFunctions;
 static void tglRecordNativeModeBinding(void *port,uint32_t ddi,uint32_t requested);
 static void wrapTGLPowerInit(void *power,void *controller);
	static uint32_t wrapTGLMailboxRead(void *controller, uint32_t command);
	static uint32_t wrapTGLMailboxReadData(void *controller, uint32_t command, uint32_t data);
	static void wrapTGLMailboxWrite(void *controller, uint32_t command, uint32_t data, bool microWait);
	mach_vm_address_t orgTGLRunAUX {};
	mach_vm_address_t tglEnablePG {}, tglDisablePG {}, tglPortByDDI {};
	mach_vm_address_t orgTGLEnableAUXPower {}, orgTGLDisableAUXPower {};
	bool tglAUXParentReady {false};
	void *tglAUXParentOwner {};
	// Qualified PHY backends only; immutable while AUX references are live.
	// Explicit fixed-DP wiring selects the common TC1..6 backend.
	uint32_t tglAUXParentMask {TGLTCWiring::Mask};
 TGLTCWiring tglTCWiring {TGLTCWiring::fromMasks(0,0,0)};
 bool tglTCWiringLoaded {}, tglTCWiringExplicit {};
 // Native mode setter owns FIA1 bits 0/1 correctly on modular TGL. Later
 // instances need the new mode backend, regardless of available PHY math.
 uint32_t tglTCAutoCandidates {},tglTCAutoRegistered {};
 uint32_t tglDPAltCandidates {},tglComboDPMask {};
 XELP::Display12SSTRoutes tglSSTRoutes() const {
  auto routes=tglTCWiring.sstRoutes(tglAUXParentMask,
   tglTCAutoCandidates&__atomic_load_n(&tglTCAutoRegistered,__ATOMIC_ACQUIRE),tglDPAltCandidates);
  routes.combo=adlsDisplaySelected ? (adlsWiring.valid?adlsWiring.dp:0) : tglComboDPMask;
  routes.panel=adlsDisplaySelected && adlsWiring.valid ? adlsWiring.panel : 0;
  return routes;
 }
 bool tglDPSSTEligible(uint32_t ddi) const {return tglSSTRoutes().eligible(ddi);}
 bool tglDPSSTMode(uint32_t ddi,uint32_t mode) const {
  const auto route=tglSSTRoutes().external(ddi,mode);
  return route.valid() && route.mode==mode;
 }
 uintptr_t tglNativeLegacyHPDCaller {};
 bool tglRegisterLegacyHPD(uint32_t ddi,uint32_t mode,uintptr_t caller) {
  // The qualified native HPD-high branch supplies its decoded transport.
  // This admits the same firmware/backend pair as the boot probe, without
  // reading FIA before the native mode setter acquires its power references.
  if(!tglNativeLegacyHPDCaller || caller!=tglNativeLegacyHPDCaller || mode!=4 ||
     ddi<3 || ddi>=9 || !(tglTCAutoCandidates&tglAUXParentMask&(1U<<ddi)))return false;
  __atomic_or_fetch(&tglTCAutoRegistered,1U<<ddi,__ATOMIC_RELEASE);
  return true;
 }
 uint32_t tglTCProbeMask() const {return tglTCWiring.valid ?
  (tglTCWiring.legacy|tglTCAutoCandidates)&tglAUXParentMask : 0;}
 bool tglTCModeAllowed(uint32_t ddi,uint32_t mode) const {
  if(ddi>=9 || !(tglTCAutoCandidates&(1U<<ddi)))return true;
  return mode==0 || (mode==3 && (tglDPAltCandidates&(1U<<ddi))) ||
   (mode==4 && (__atomic_load_n(&tglTCAutoRegistered,__ATOMIC_ACQUIRE)&(1U<<ddi)));
 }
 static bool tglLoadTCWiring(IOService *provider);
 bool tglLegacyTCEligible(uint32_t ddi) const {
  return tglSSTRoutes().external(ddi,4).valid();
 }
	uint32_t tglAUXParentRefs[9] {};
	bool tglAUXParentEligible(uint32_t ddi) const {
		return tglAUXParentReady && tglSSTRoutes().fixed(ddi).auxParentPG!=0;
	}
	uint32_t tglAUXParentPG(uint32_t ddi) const {return tglSSTRoutes().fixed(ddi).auxParentPG;}
	static void wrapTGLEnableAUXPower(void *power, uint32_t ddi);
	static void wrapTGLDisableAUXPower(void *power, uint32_t ddi);
	static uint32_t wrapTGLRunAUX(void *port, uint32_t command, uint32_t *packet, uint32_t length, bool flag);
	static void tglTimeEvent(uint32_t stage, uint32_t id, bool done, uint32_t arg, uint32_t result);

	mach_vm_address_t orgTGLTrainingPattern {}, orgTGLPowerDDIState {};
	static uint32_t wrapTGLTrainingPattern(void *path, uint32_t pattern);
	mach_vm_address_t orgTGLPortTraining {}, orgTGLPathLookup {}, orgTGLFreePath {};
	mach_vm_address_t orgTGLLightUpDP {}, orgTGLDisableDisplay {}, orgTGLEnablePipe {};
	mach_vm_address_t orgTGLReadAUX {}, orgTGLWriteAUX {}, orgTGLI2CAUX {};
	bool tglBootstrapAUXReady {};
	static uint32_t wrapTGLPortTraining(void *port, void *request);
	static void *wrapTGLPathLookup(void *port);
	static void wrapTGLFreePath(void *path);
	static uint32_t wrapTGLLightUpDP(void *controller, void *fb, void *path, const void *timing);
	static uint32_t wrapTGLDisableDisplay(void *controller, void *fb, void *path, uint8_t flag);
	static void wrapTGLEnablePipe(void *controller, void *fb, void *path, const void *timing);
	static uint32_t wrapTGLReadAUX(void *port, uint32_t address, void *data, uint32_t length);
	static uint32_t wrapTGLWriteAUX(void *port, uint32_t address, void *data, uint32_t length);
	static uint32_t wrapTGLI2CAUX(void *port, uint32_t command, uint32_t *packet, uint32_t length, uint8_t flag);
	mach_vm_address_t orgTGLFreeResources {}, orgTGLFreePathResources {}, orgTGLAllocateResources {};
	mach_vm_address_t orgTGLFBDisable {}, orgTGLReleaseResources {}, orgTGLCommitResources {};
	mach_vm_address_t orgTGLAttachPath {}, orgTGLTestResources {};
	static void wrapTGLFreeResources(void *fb);
	static void wrapTGLFreePathResources(void *fb, void *path);
	static bool wrapTGLAllocateResources(void *fb);
	static void wrapTGLFBDisable(void *fb, bool flag);
	static uint32_t wrapTGLReleaseResources(void *controller, void *request);
	static uint32_t wrapTGLCommitResources(void *controller, void *request, void *output);
	static uint32_t wrapTGLTestResources(void *controller, void *request, void *output);
	static void wrapTGLAttachPath(void *fb, void *path);
	mach_vm_address_t orgTGLAllocatePathResources {}, orgTGLAddPath {};
	static bool wrapTGLAllocatePathResources(void *fb, uint32_t portIndex, uint32_t stream);
	static void *wrapTGLAddPath(void *fb, uint32_t pipe);
	static void tglObserveConfig(void *controller, void *request, void *output, bool test, bool done, uint32_t result);
	static void *tglResourceFB(void *controller, uint32_t index);
	static void tglObserveResource(void *fb, void *path, uint32_t event, bool done, uint32_t result, uint64_t arg0, uint64_t arg1, void *caller);
	static void tglObservePath(void *port, void *path, uint32_t event, bool done, uint32_t result, uint64_t arg, void *caller);
	static void wrapTGLPowerDDIState(void *power, bool enable, uint32_t requested);
	static void tglObserveActivation(void *controller, void *port, void *path, uint32_t phase, bool done, uint32_t result);

	IOSimpleLock *tglDekelLock {};
	mach_vm_address_t orgTGLReadDekel {}, orgTGLWriteDekel {};
	static uint32_t wrapTGLReadDekel(void *port, uint32_t index);
	static void wrapTGLWriteDekel(void *port, uint32_t index, uint32_t value);
	static bool tglDekelAccess(void *port, uint32_t index, bool write, uint32_t &value);
	bool tglPlatformNVRAMReady {false};
	bool tglFramebufferInitReady {false};
 // The donor layer owns its hooks once, before the hardware backend runs.
 bool framebufferDonorAttempted {},framebufferDonorCommonReady {},framebufferBackendInstalled {};
 mach_vm_address_t orgFramebufferStart {},orgFramebufferInit {},orgFramebufferComputeLanes {},framebufferLaneCaller {};
 __attribute__((noinline)) bool processFramebufferKext(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 bool processDisplay13Shutdown(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static uint32_t wrapDisplay13Shutdown(void *,void *);
 static void finishDisplay13TCShutdown(void *);
 mach_vm_address_t orgDisplay13Shutdown {},display13ShutdownPowerCaller {};
 __attribute__((noinline)) bool processFramebufferCommon(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 bool framebufferTrainingHooksReady {};
 bool framebufferStartInProgress {};
 uint32_t framebufferProtocolCalls {};
 void *framebufferStartedController {};
 __attribute__((noinline)) bool processFramebufferTraining(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 static bool wrapFramebufferStart(IOService *,IOService *);
 static bool wrapFramebufferInit(IOService *,IOService *,uint32_t);
 static bool wrapFramebufferComputeLanes(void *,const void *,uint32_t,uint32_t,uint32_t *);
 bool framebufferHDMIEligible(const uint32_t *) const;
 static void observeFramebufferUserClient(void *,uint32_t,void **,uint32_t);
 bool framebufferDonorRuntimeReady {};
 static XELP::LinkRequirements framebufferLinkRequirements(void *,uint32_t);
 static uint32_t framebufferPortWaitPolls(void *);
 bool processFramebufferPortWait(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 bool processFramebufferDonorRuntime(KernelPatcher &,size_t,mach_vm_address_t,size_t);
	mach_vm_address_t orgTGLHDCP2Init {};
	static bool wrapTGLHDCP2Init(void *hdcp, void *path);
	mach_vm_address_t orgTGLHDCP1Dispatch {}, orgTGLHDCP1Timer {};
	static uint32_t wrapTGLHDCP1Dispatch(void *hdcp, uint32_t event);
	static void wrapTGLHDCP1Timer(void *hdcp, void *timer);
	static uint32_t tglObserveHDCP1(void *hdcp, uint32_t stage, uint32_t event,
		bool done, uint32_t result, uint32_t pipe = 0xFFFFFFFFU);
	bool tglFBIRQReady {true};
	mach_vm_address_t orgTGLFBEnableIRQ {}, orgTGLFBSetupIRQ {};
	static uint32_t wrapTGLFBEnableIRQ(void *controller);
	static void wrapTGLFBSetupIRQ(void *controller,void *workloop);
	mach_vm_address_t orgTGLAcceleratorStart {};
	mach_vm_address_t orgAlderLakeNConfigureDevice {};
	bool adlnParentBoundaryReady {false};
	bool xelpMemoryStageReady {false};
	bool adlnConfigStageReady {false};
	bool adlnEventStageReady {false};
	bool display13StageReady {false};
	bool adlnTaskBoundaryReady {false};
	mach_vm_address_t orgAlderLakeNNewEventMachine {};
	mach_vm_address_t orgAlderLakeNEventInit {},orgAlderLakeNNewDisplayMachine {};
	mach_vm_address_t orgDisplay13Init {},orgAlderLakeNNewStatistics {};
	mach_vm_address_t orgAlderLakeNKernelTask {};
	uint32_t xelpMemoryStageStarted {};
	IOService *xelpMemoryStageOwner {}; // Keep dummy-PTE backing alive until reboot.
	static bool wrapAlderLakeNConfigureDevice(IOService *that, IOPCIDevice *provider);
	static void *wrapAlderLakeNNewEventMachine(IOService *that);
	static bool wrapAlderLakeNEventInit(void *that, IOService *owner, uint32_t timeout, int stamps);
	static void *wrapAlderLakeNNewDisplayMachine(IOService *that);
	static void wrapDisplay13Init(void *that, IOService *owner);
	static void *wrapAlderLakeNNewStatistics(IOService *that);
	static void *wrapAlderLakeNKernelTask(IOService *that);
	__attribute__((noinline)) static bool startAlderLakeNParentBoundary(IOService *that, IOService *provider);
	__attribute__((noinline)) static bool runXELPMemoryStage(IOService *that, IOPCIDevice *provider);
	__attribute__((noinline)) static bool configureAlderLakeNWithGT(IOService *that, IOPCIDevice *provider, IOMemoryMap *map, bool &released);
	bool tglAcceleratorRequested {false}, tglAcceleratorReady {false};
	mach_vm_address_t orgTGLAcceleratorProbe {}, orgTGLAcceleratorFailure {};
	mach_vm_address_t orgTGLAcceleratorRegister {}, orgTGLAcceleratorStatusMemory {}, orgTGLAcceleratorEngine {};
	static IOService *wrapTGLAcceleratorProbe(IOService *that, IOService *provider, SInt32 *score);
	static void wrapTGLAcceleratorFailure(IOService *that, uint32_t reason);
	static bool wrapTGLAcceleratorRegister(IOService *that);
	static bool wrapTGLAcceleratorStatusMemory(IOService *that);
	static bool wrapTGLAcceleratorEngine(IOService *that);
	mach_vm_address_t orgAlderLakeNNativeMemory {}, orgAlderLakeNNativeGPUInfo {}, orgAlderLakeNNativeIRQ {};
	static bool wrapAlderLakeNNativeConfigure(IOService *that, IOPCIDevice *provider);
	static bool wrapAlderLakeNNativeMemory(IOService *that);
	static void wrapAlderLakeNNativeGPUInfo(IOService *that);
	static bool wrapAlderLakeNNativeIRQ(IOService *that);
	mach_vm_address_t orgTGLFastCopy {}, tglSourceCopy {};
	mach_vm_address_t orgTGLBlitDeferredInit {};
	static void wrapTGLBlitDeferredInit(void *context,void *params,const void *rects);
	mach_vm_address_t orgTGLSurfaceAux {};
	static void wrapTGLSurfaceAux(void *resource, void *entry, uint32_t *surface, uint32_t intent);
	mach_vm_address_t orgTGLHangRingDump {};
	static void wrapTGLHangRingDump(void *ring, void *registers);
	mach_vm_address_t orgTGLRecovery {};
	static void wrapTGLRecovery(void *channel);
	static void wrapTGLFastCopy(void *context, void *params, const void *rects, void *workarounds);
	mach_vm_address_t orgTGLHostSubmit {}, orgTGLHostWait {}, orgTGLHostCSB {}, orgTGLHostCSBPanic {}, orgTGLHostCSBIDPanic {};
	mach_vm_address_t orgTGLHostIRQEnable {}, orgTGLHostIRQClassEnable {}, orgTGLHostIRQClassDisable {};
	static void wrapTGLHostIRQClassEnable(void *bridge);
	static void wrapTGLHostIRQClassDisable(void *bridge);
	static void wrapTGLHostIRQEnable(void *bridge);
	mach_vm_address_t orgTGLHostIRQProcess {}, orgTGLHostIRQService {}, orgTGLHostSWInterrupt {};
	static void wrapTGLHostIRQProcess(void *bridge);
	static void wrapTGLHostIRQService(void *bridge, const uint64_t *bits);
	static void wrapTGLHostSWInterrupt(void *streamer, void *source, int count);
	static void wrapTGLHostCSBIDPanic(const uint32_t *entry);
	static void wrapTGLHostCSB(void *streamer, uint32_t *entry, uint32_t *index);
	bool installTGLCSB12(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
	mach_vm_address_t orgTGLCSBInit {}, orgTGLCSBReset {}, orgTGLCSBWake {}, orgTGLCSBProcess {};
	static bool wrapTGLCSBInit(void *streamer, void *owner, void *loop, void *scheduler, unsigned engine);
	static void wrapTGLCSBReset(void *streamer);
	static void wrapTGLCSBWake(void *streamer);
	static void wrapTGLCSBProcess(void *streamer, uint32_t flags);
	static void wrapTGLHostCSBPanic(const uint32_t *entry);
	mach_vm_address_t orgAlderLakeNExecutionWake {};
	static void wrapAlderLakeNExecutionWake(void *owner, bool enable, unsigned domains, unsigned client);
	static void wrapTGLHostSubmit(void *streamer, uint32_t flags);
	static bool wrapTGLHostWait(void *ring, uint32_t *stamp, uint32_t target, uint32_t timeout);
	mach_vm_address_t orgTGLGuCLoadBinary {}, orgTGLGuCDMA {}, orgTGLGuCHeaderPanic {}, orgTGLGlobalMapRange {};
	mach_vm_address_t orgTGLGuCBuffer {}, orgTGLGuCGPUAddress {};
	mach_vm_address_t orgTGLAcceleratorStop {};
	static void wrapTGLAcceleratorStop(IOService *that, IOService *provider);
	void *tglAcceleratorOwner {};
	uint64_t tglAcceleratorBARSize {};
	static void *wrapTGLGuCBuffer(void *task, size_t size, uint32_t options, uint32_t flags);
	static uint64_t wrapTGLGuCGPUAddress(void *buffer);
	static bool wrapTGLGuCLoadBinary(void *guc, bool reload);
	static bool wrapTGLGuCDMA(void *guc, uint64_t source, uint32_t destination, uint32_t size, uint32_t type, bool wait);
	static void wrapTGLGuCHeaderPanic(void *guc);
	static bool wrapTGLGlobalMapRange(void *table, const uint64_t *range, uint64_t physical, uint64_t flags);
	static void tglSampleGuCDMA(void *guc);
	static void tglInstallGuCFirmwarePTEs(uint32_t bufferIndex);
	static void tglObserveAccelerator(IOService *that, uint32_t stage, bool done, uint64_t result, uint64_t detail);
	mach_vm_address_t orgTGLLaneMask {};
	mach_vm_address_t orgTGLHPDState {};
	mach_vm_address_t orgTGLSetPortMode {};
	mach_vm_address_t orgTGLDPPower {};
	mach_vm_address_t orgTGLSinkCount {};
	mach_vm_address_t orgTGLEDID {};
	mach_vm_address_t orgTGLFBDDC {};
	static void tglTrace(uint32_t stage, uint32_t id, bool done, uint32_t a, uint32_t b, uint32_t result, uint64_t value);
	static void tglSnapshotFB(void *fb);
	mach_vm_address_t orgTGLTrainingEnd {};
	static uint32_t wrapTGLTrainingEnd(void *controller, void *request);
	mach_vm_address_t orgTGLTraining {};
	static uint32_t wrapTGLTraining(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLTrainingBegin {};
	static uint32_t wrapTGLTrainingBegin(void *controller, void *request);
	mach_vm_address_t orgTGLAGDCPortStatus {};
	static uint32_t wrapTGLAGDCPortStatus(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLAGDCAUX {};
	static uint32_t wrapTGLAGDCAUX(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLFBSetAttr {};
	static uint32_t wrapTGLFBSetAttr(void *fb, uint32_t attribute, unsigned long value);
	mach_vm_address_t orgTGLFBGetAttr {};
	static uint32_t wrapTGLFBGetAttr(void *fb, uint32_t attribute, unsigned long *value);
	mach_vm_address_t orgTGLFBEnable {};
	static uint32_t wrapTGLFBEnable(void *fb);
	mach_vm_address_t orgTGLStreamRequest {};
	static uint32_t wrapTGLStreamRequest(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLClientAttribute {};
	static uint32_t wrapTGLClientAttribute(void *client, uint32_t attribute, unsigned long *input, unsigned long inputSize, unsigned long *output, unsigned long *outputSize, void *arguments);
	mach_vm_address_t orgTGLLinkConfig {};
	static uint32_t wrapTGLLinkConfig(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLGPUCapability {};
	static uint32_t wrapTGLGPUCapability(void *controller, void *output);
	mach_vm_address_t orgTGLTopDDC {};
	static uint32_t wrapTGLTopDDC(void *fb, int connection, uint32_t block, uint32_t type, uint32_t options, uint8_t *data, uint64_t *length);
	mach_vm_address_t orgTGLPortDDC {};
	static uint32_t wrapTGLPortDDC(void *port, uint32_t block, uint8_t *data, bool force);
	mach_vm_address_t orgTGLStreamEDID {};
	static uint32_t wrapTGLStreamEDID(void *controller, void *request, void *output);
	mach_vm_address_t orgTGLSetEDID {};
	static uint32_t wrapTGLSetEDID(void *controller, void *request);
	mach_vm_address_t orgTGLFBOnline {};
	static uint32_t wrapTGLFBOnline(void *controller, void *request);
	mach_vm_address_t orgTGLAGDCRegister {};
	static uint32_t wrapTGLAGDCRegister(void *controller, void *request);
	mach_vm_address_t orgTGLConnectionAttr {};
	static uint32_t wrapTGLConnectionAttr(void *fb, int connection, uint32_t attribute, unsigned long *value);

	static uint32_t wrapTGLFBDDC(void *fb, uint32_t block, uint8_t *data, uint64_t *length, uint32_t flags, void *path);
	mach_vm_address_t orgTGLPortState {};
	static uint32_t wrapTGLEDID(void *port, uint32_t address, uint32_t offset, uint8_t *data, uint32_t length);
	static uint32_t wrapTGLPortState(void *port);
	static uint32_t wrapTGLDPPower(void *port, uint8_t state);
	static uint32_t wrapTGLSinkCount(void *port, uint8_t *count);
	// Shared across TC ports: native HAL performs an unprotected FIA RMW.
	// Kept for the routed kext lifetime; distinct from path-pool/power locks.
	IORecursiveLock *tglModeTransitionLock {};
	uint32_t tglModeSerializedCalls {};
 TGLNativeFiaContext tglNativeFiaContext;
 bool tglNativeFiaReady {}; uint32_t tglModularProbed {},tglNativeFiaReads {},tglNativeFiaWrites {};
 mach_vm_address_t tglNativeFiaBase {},orgTGLPortReadReg {};
 static uint32_t wrapTGLPortReadReg(void *port,uint32_t reg);
 bool tglDPAltFiaReady {},tglDPModeReplaceReady {};
 mach_vm_address_t orgTGLConfiguredLanes {},orgTGLFIALaneCount {};
 static bool tglDPAltFia(void *port,TGLFiaPort &fia);
 static bool tglLegacyFia(void *port,TGLFiaPort &fia);
 static uint32_t wrapTGLConfiguredLanes(void *port);
 static void wrapTGLFIALaneCount(void *port,uint32_t lanes);
 static bool tglNativeFiaAccess(void *port,uint32_t reg,uintptr_t caller,bool write,TGLNativeFia &mapping);
	bool tglModeResultReady {false};
	uint32_t tglModeResultCorrections {};
	static uint32_t wrapTGLSetPortMode(void *port, uint32_t mode);
	static uint32_t wrapTGLHPDState(void *port);
	static uint32_t wrapTGLLaneMask(void *port);
	static uint32_t wrapTGLProbePort(void *port);
	static bool wrapTGLFramebufferStart(IOService *that, IOService *provider);
	static bool wrapTGLAcceleratorStart(IOService *that, IOService *provider);


    static XE *callbackXE;
    KernelPatcher::KextInfo *currentGraphics {};
    KernelPatcher::KextInfo *currentFramebuffer {};
    struct { uint32_t fStolenMemorySize {}; } framebufferPatch;
    struct { struct { bool FPFStolenMemorySize {}; } bits; } framebufferPatchFlags;
    DVMTCalcFix modDVMTCalcFix;
};
#endif
