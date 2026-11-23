#!/usr/bin/env python3
"""Exercise production hook inventory and route-to-contract adapter, without IOKit."""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
methods=[re.search(r'XELP::DisplayContract::Mask XE::display'+n+r'Available\(\) const \{.*?\n\}',s,re.S).group().replace('XE::','') for n in ('12','13')]
# Fixed fixture declarations, reviewed separately from the production mapping.
# New production inputs must be explicitly modeled instead of auto-enabled.
fields = """framebufferDonorRuntimeReady tglSonomaSurfaceFramebufferReady
tglPlatformNVRAMReady tglFBIRQReady tglProbeReady tglFramebufferInitReady
tglTopologyReady tglPanelPowerPropertyReady tglEDPTranscoderReady tglDekelReady
tglGMBusReady tglTrainingLeaseReady tglEnablePG tglDisablePG tglPortByDDI
orgTGLPathLookup orgTGLAddPath orgTGLFreePath orgTGLTrainingEnd orgTGLTraining
orgTGLTrainingBegin orgTGLAllocatePathResources tglSinkFramingReady
tglComboSignalReady tglComboAUXLifetimeReady tglAUXParentReady tglModeResultReady
tglNativeLegacyHPDCaller tglDPAltFiaReady tglNativeFiaReady tglModeTransitionLock
adlsClockReady adlsPowerReady adlsPortInitReady orgTGLEnableCombo
orgTGLGetOSInformation orgTGLRegisterRead orgTGLRegisterWrite tglHPDHookReady
tglClockOffCaller tglComboAUXGateReady tglPanelDepthReady tglPanelTimingReady
adlsPanelPWMReady display13FBPowerReferencesReady display13FBEngineEnableHookReady
display13FBLifecycleHooksReady display13FBDBUFHooksReady display13FBWatermarkHooksReady
display13FBClockReadoutHooksReady display13FBClockSetHookReady display13FBMemoryHooksReady
display13FBGTTHooksReady display13FBABIReady display13FBBootHandoffHookReady
display13FBIRQReady display13FBWatermarkLayoutReady display13FBOutputHooksReady
display13FBTopologyHooksReady display13FBSSTHooksReady display13FBAUXHooksReady
display13TrainingReady display13FramingReady
 display13PanelPWMReady""".split()
# Real callback invocation inside start, through to the immutable contract.
begin=s.index(' // Evaluate firmware candidates, not only currently registered/live ports:')
end=s.index(' uint64_t contractRecord[]',begin)
adapter=s[begin:end].replace('auto self=callbackXE;','auto self=this;')+'return contract;'
code=r'''
#include "kern_xelp_display_contract.hpp"
#include <cassert>
#include <cstdio>
using C=XELP::DisplayContract;
struct State {
FIELDS
 bool display13PanelPowerReady=false,display13PanelProbeReady=false;
 bool display13SharedModePatchFaulted=false;
 bool display13FBTCHooksReady=false,display13TCProbeReady=true,display13DPAltProbeReady=true;
 bool display13TCDPProbeReady=false,display13ClockOrderReady=true;
 struct Native {bool valid=true;bool complete()const{return valid;}}tglTCNativeFunctions;
 bool tglHDMIB=false,adlsDisplaySelected=false,tglTrainingLeaseRequested=true;
 struct {struct {bool valid=false;}panel;}firmwarePorts;
 struct {unsigned dp=0;}adlsWiring;
 struct {unsigned legacy=0;}tglTCWiring;
 unsigned tglTCAutoCandidates=0,tglDPAltCandidates=0,tglComboDPMask=0;
METHODS
 C start() const {ADAPTER}
};
int main() {
 State s;
 s.framebufferDonorRuntimeReady=false;
 assert(s.start().missing()==C::DonorRuntime);
 assert(C::display13(s.display13Available()).missing()==C::DonorRuntime);
 s.framebufferDonorRuntimeReady=true;
 using Port=C::Port13;
 const auto route=[&](Port port){return C::display13Port(s.display13Available(),port);};
 assert(C::display13(s.display13Available()).ready());
 s.display13SharedModePatchFaulted=true;
 assert(C::display13(s.display13Available()).missing()==C::Output13);
 s.display13SharedModePatchFaulted=false;
 assert(route(Port::TMDS).ready());
 assert(route(Port::TCTMDS).missing()==C::TCNative);
 assert(route(Port::FixedDP).missing()==(C::TCNative|C::FixedDPProbe13));
 s.display13FBTCHooksReady=true;
 assert(route(Port::DPAlt).ready());
 s.display13DPAltProbeReady=false;
 assert(route(Port::DPAlt).missing()==C::HPD && route(Port::TCTMDS).ready());
 s.display13TCDPProbeReady=true;assert(route(Port::FixedDP).ready());
 s.display13TCProbeReady=false;assert(route(Port::TCTMDS).missing()==C::Mode);
 s.display13TCProbeReady=true;
 s.display13FBAUXHooksReady=false;assert(route(Port::FixedDP).missing()==C::AUX13);
 s.display13FBAUXHooksReady=true;s.display13FBSSTHooksReady=false;
 assert(route(Port::FixedDP).missing()==C::SST13);
 s.display13FBSSTHooksReady=true;
 s.display13TrainingReady=false;assert(route(Port::FixedDP).missing()==C::Training12);
 s.display13TrainingReady=true;s.display13ClockOrderReady=false;
 assert(route(Port::FixedDP).missing()==C::ClockOrder);
 s.display13ClockOrderReady=true;s.display13FramingReady=false;
 assert(route(Port::FixedDP).missing()==C::Framing);s.display13FramingReady=true;
 s.display13FBTCHooksReady=false;
 assert(route(Port::Panel).missing()==C::PanelPower);
 s.display13PanelPowerReady=true;assert(route(Port::Panel).missing()==C::PanelPower);
 s.display13PanelProbeReady=true;assert(route(Port::Panel).ready());
 s.display13PanelPWMReady=false;assert(route(Port::Panel).missing()==C::PanelPWM);
 s.display13PanelPWMReady=true;s.display13PanelPowerReady=s.display13PanelProbeReady=false;
 // TGL: candidate not yet registered, native donor setup is complete.
 // Missing framing, AUX, mode lock or cleanup must fail before any hotplug.
 s.tglTCAutoCandidates=8;assert(s.start().ready());
 s.tglSinkFramingReady=false;assert(s.start().missing()==C::Framing);s.tglSinkFramingReady=true;
 s.tglAUXParentReady=false;assert(s.start().missing()==C::AUXParent);s.tglAUXParentReady=true;
 s.tglModeTransitionLock=false;assert(s.start().missing()==C::ModeLock);s.tglModeTransitionLock=true;
 s.tglClockOffCaller=false;assert(s.start().missing()==C::ClockOrder);s.tglClockOffCaller=true;
 s.tglTCAutoCandidates=0;s.tglDPAltCandidates=8;
 s.tglDPAltFiaReady=false;assert(s.start().missing()==C::AltFIA);s.tglDPAltFiaReady=true;
 s.tglAUXParentReady=false;assert(s.start().ready()); // TC owns DP-alt AUX power.
 s.tglAUXParentReady=true;s.tglDPAltCandidates=0;
 s.adlsDisplaySelected=true;s.adlsWiring.dp=1;
 s.tglComboSignalReady=false;assert(s.start().missing()==C::ComboSignal);
 s.tglComboSignalReady=true;
 s.adlsDisplaySelected=false;s.adlsWiring.dp=0;s.firmwarePorts.panel.valid=true;
 s.tglTCNativeFunctions.valid=false;assert(s.start().missing()==C::TCNative);s.tglTCNativeFunctions.valid=true;
 s.tglComboAUXGateReady=false;assert(s.start().missing()==C::ComboAUXGate);s.tglComboAUXGateReady=true;
 s.tglPanelDepthReady=false;assert(s.start().missing()==C::PanelDepth);s.tglPanelDepthReady=true;
 s.tglPanelTimingReady=false;assert(s.start().missing()==C::PanelTiming);
 return 0;
}
'''
code=code.replace('FIELDS','\n'.join(' bool '+f+'=true;' for f in dict.fromkeys(fields))).replace('METHODS','\n'.join(methods)).replace('ADAPTER',adapter)
with tempfile.TemporaryDirectory(prefix='display-contract-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS production startup inventory: future hotplug, framing, AUX, mode, cleanup and DP-alt dependency failures')
