// Startup contracts describe installed operations, separately from physical
// GPU admission, firmware wiring and runtime resource ownership.
#ifndef KERN_XELP_DISPLAY_CONTRACT_HPP
#define KERN_XELP_DISPLAY_CONTRACT_HPP
#include <stdint.h>
namespace XELP {
struct DisplayContract {
 using Mask=uint64_t;
 enum Operation:Mask {
  Surface=1ULL<<0,
  NVRAM=1ULL<<1,
  IRQ=1ULL<<2,
  Probe=1ULL<<3,
  Framebuffer=1ULL<<4,
  Topology=1ULL<<5,
  PanelPower=1ULL<<6,
  PanelTranscoder=1ULL<<7,
  Dekel=1ULL<<8,
  GMBus=1ULL<<9,
  Lease=1ULL<<10,
  PowerGet=1ULL<<11,
  PowerPut=1ULL<<12,
  PortLookup=1ULL<<13,
  PathLookup=1ULL<<14,
  PathAdd=1ULL<<15,
  PathFree=1ULL<<16,
  TrainingEnd=1ULL<<17,
  Training=1ULL<<18,
  TrainingBegin=1ULL<<19,
  PathAllocate=1ULL<<20,
  Framing=1ULL<<21,
  ComboSignal=1ULL<<22,
  ComboAUX=1ULL<<23,
  AUXParent=1ULL<<24,
  TCNative=1ULL<<25,
  Mode=1ULL<<26,
  HPDCaller=1ULL<<27,
  AltFIA=1ULL<<28,
  NativeFIA=1ULL<<29,
  ModeLock=1ULL<<30,
  ClockAdapter=1ULL<<31,
  PowerAdapter=1ULL<<32,
  PortInit=1ULL<<33,
  EnableCombo=1ULL<<34,
  OSInformation=1ULL<<35,
  MMIORead=1ULL<<36,
  MMIOWrite=1ULL<<37,
  HPD=1ULL<<38,
  PowerRefs13=1ULL<<39,
  Engine13=1ULL<<40,
  Lifecycle13=1ULL<<41,
  DBUF13=1ULL<<42,
  WM13=1ULL<<43,
  ClockRead13=1ULL<<44,
  ClockSet13=1ULL<<45,
  Memory13=1ULL<<46,
  GTT13=1ULL<<47,
  ABI13=1ULL<<48,
  Handoff13=1ULL<<49,
  IRQ13=1ULL<<50,
  WMLayout13=1ULL<<51,
  Output13=1ULL<<52,
  Topology13=1ULL<<53,
  ClockOrder=1ULL<<54,
  ComboAUXGate=1ULL<<55,
  PanelDepth=1ULL<<56,
  PanelTiming=1ULL<<57,
  PanelPWM=1ULL<<58,
  SST13=1ULL<<59,
  AUX13=1ULL<<60,
  FixedDPProbe13=1ULL<<61,
  DPAltBoot13=1ULL<<62,
  DonorRuntime=1ULL<<63,
 };
 Mask required {},available {};
 Mask missing() const {return required&~available;}
 bool ready() const {return !missing();}
 static constexpr Mask Base12=DonorRuntime|Surface|NVRAM|IRQ|Probe|Framebuffer|Topology|PanelPower|PanelTranscoder|Dekel;
 static constexpr Mask Training12=Lease|PowerGet|PowerPut|PortLookup|PathLookup|PathAdd|PathFree|TrainingEnd|Training|TrainingBegin|PathAllocate|ClockOrder;
 static constexpr Mask Combo12=Framing|ComboSignal|ComboAUX|ComboAUXGate|TCNative|MMIORead|MMIOWrite;
 static constexpr Mask Fixed12=AUXParent|TCNative|Mode|HPDCaller;
 static constexpr Mask Alt12=TCNative|Mode|ModeLock|AltFIA|NativeFIA;
 static constexpr Mask PlatformADLS=DonorRuntime|ClockAdapter|PowerAdapter|PortInit|EnableCombo|PortLookup|OSInformation|MMIORead|MMIOWrite|HPD;
 static constexpr Mask Power13=PowerRefs13|Engine13|Lifecycle13|DBUF13|WM13;
 static constexpr Mask Clock13=ClockRead13|ClockSet13|Lifecycle13;
 static constexpr Mask MemoryGroup13=Memory13|GTT13|ABI13|Handoff13;
 static constexpr Mask Backend13=DonorRuntime|Power13|Clock13|MemoryGroup13|IRQ13|WMLayout13|Output13|Topology13;
 struct Routes {
  bool hdmi {},panel {},combo {},legacy {},alt {},leaseRequested {};
 };
 static DisplayContract display12(Mask installed,const Routes &r) {
  // These donor startup hooks are controller-wide even without a panel or
  // connected Dekel sink. Route-specific operations are added below.
  Mask required=Base12;
  if(r.hdmi)required|=GMBus;
  if(r.leaseRequested||r.combo||r.legacy||r.alt)required|=Training12;
  if(r.panel||r.combo)required|=Combo12;
  if(r.panel)required|=PanelDepth|PanelTiming|PowerGet|PowerPut;
  if(r.combo)required|=AUXParent;
  if(r.legacy)required|=Fixed12|AltFIA|NativeFIA; // Fixed TC also needs topology-aware FIA lanes/ownership.
  if(r.legacy||r.alt)required|=Framing|ModeLock;
  if(r.alt)required|=Alt12;
  return {required,installed};
 }
 static DisplayContract adls(Mask installed,bool dp,bool panel=false) {
  return {PlatformADLS|(dp?Training12|Combo12|Fixed12:0)|
   (panel?Combo12|PanelDepth|PanelTiming|PanelPWM|AUXParent|PowerGet|PowerPut:0),installed};
 }
 static constexpr Mask DP13=SST13|AUX13|Training12|Framing;
 static constexpr Mask Panel13=DP13|PanelPower|PanelTranscoder|PanelDepth|PanelTiming|PanelPWM;
 static DisplayContract display13(Mask installed) {return {Backend13,installed};}
 enum class Port13 {TMDS,Panel,DSI,TCTMDS,FixedDP,DPAlt};
 static DisplayContract display13Port(Mask installed,Port13 port,bool boot=false) {
  Mask required=0;
  switch(port) {
   case Port13::TMDS:
   case Port13::DSI: break; // DSI has its own explicit boot and runtime gates.
   case Port13::Panel: required=Panel13;break;
   case Port13::TCTMDS: required=TCNative|Mode;break;
   case Port13::FixedDP: required=TCNative|Mode|FixedDPProbe13|DP13;break;
   case Port13::DPAlt: required=TCNative|HPD|DP13|(boot?DPAltBoot13:0);break;
  }
  return {required,installed};
 }
 static const char *name(Mask bit) {
  switch(bit) {
   case DonorRuntime:return "DonorRuntime";
   case Surface:return "Surface";
   case NVRAM:return "NVRAM";
   case IRQ:return "IRQ";
   case Probe:return "Probe";
   case Framebuffer:return "Framebuffer";
   case Topology:return "Topology";
   case PanelPower:return "PanelPower";
   case PanelTranscoder:return "PanelTranscoder";
   case Dekel:return "Dekel";
   case GMBus:return "GMBus";
   case Lease:return "Lease";
   case PowerGet:return "PowerGet";
   case PowerPut:return "PowerPut";
   case PortLookup:return "PortLookup";
   case PathLookup:return "PathLookup";
   case PathAdd:return "PathAdd";
   case PathFree:return "PathFree";
   case TrainingEnd:return "TrainingEnd";
   case Training:return "Training";
   case TrainingBegin:return "TrainingBegin";
   case PathAllocate:return "PathAllocate";
   case Framing:return "Framing";
   case ComboSignal:return "ComboSignal";
   case ComboAUX:return "ComboAUX";
   case AUXParent:return "AUXParent";
   case TCNative:return "TCNative";
   case Mode:return "Mode";
   case HPDCaller:return "HPDCaller";
   case AltFIA:return "AltFIA";
   case NativeFIA:return "NativeFIA";
   case ModeLock:return "ModeLock";
   case ClockAdapter:return "ClockAdapter";
   case PowerAdapter:return "PowerAdapter";
   case PortInit:return "PortInit";
   case EnableCombo:return "EnableCombo";
   case OSInformation:return "OSInformation";
   case MMIORead:return "MMIORead";
   case MMIOWrite:return "MMIOWrite";
   case HPD:return "HPD";
   case PowerRefs13:return "PowerRefs13";
   case Engine13:return "Engine13";
   case Lifecycle13:return "Lifecycle13";
   case DBUF13:return "DBUF13";
   case WM13:return "WM13";
   case ClockRead13:return "ClockRead13";
   case ClockSet13:return "ClockSet13";
   case Memory13:return "Memory13";
   case GTT13:return "GTT13";
   case ABI13:return "ABI13";
   case Handoff13:return "Handoff13";
   case IRQ13:return "IRQ13";
   case WMLayout13:return "WMLayout13";
   case Output13:return "Output13";
   case Topology13:return "Topology13";
   case ClockOrder:return "ClockOrder";
   case ComboAUXGate:return "ComboAUXGate";
   case PanelDepth:return "PanelDepth";
   case PanelTiming:return "PanelTiming";
   case PanelPWM:return "PanelPWM";
   case SST13:return "SST13";
   case AUX13:return "AUX13";
   case FixedDPProbe13:return "FixedDPProbe13";
   case DPAltBoot13:return "DPAltBoot13";
   default:return "Unknown";
  }
 }
};
}
#endif
