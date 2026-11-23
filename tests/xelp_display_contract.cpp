#include "kern_xelp_display_contract.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using C=XELP::DisplayContract;
static void faults(C c) {
 assert(c.required && c.ready());
 // Every dependency must independently close admission. An unrelated failed
 // operation must not disable this route. Reinstalling restores admission.
 for(unsigned i=0;i<64;++i) {
  auto broken=c;auto bit=1ULL<<i;broken.available&=~bit;
  assert(broken.missing()==(c.required&bit));
  assert(broken.ready()==!(c.required&bit));
  if(broken.missing())assert(strcmp(C::name(bit),"Unknown"));
  broken.available|=bit;assert(broken.ready());
 }
}
int main() {
 unsigned cases=0;
 for(unsigned bits=0;bits<64;++bits) {
  C::Routes r{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32)};
  auto c=C::display12(~0ULL,r);faults(c);++cases;
  assert(bool(c.required&C::GMBus)==r.hdmi);
  assert(bool(c.required&C::ComboSignal)==(r.panel||r.combo));
  assert(bool(c.required&C::AltFIA)==(r.alt||r.legacy));
  assert(bool(c.required&C::AUXParent)==(r.combo||r.legacy));
  assert(bool(c.required&C::Lease)==(r.combo||r.legacy||r.alt||r.leaseRequested));
  assert(!(c.required&(C::Backend13&~C::DonorRuntime)));
  // Requirements compose by set union. Adding another output does not
  // silently remove the dependencies of the first output.
  C::Mask joined=C::display12(~0ULL,{}).required;
  for(unsigned i=0;i<6;++i)if(bits&(1U<<i)) {
   C::Routes single{};
   switch(i){case 0:single.hdmi=true;break;case 1:single.panel=true;break;
    case 2:single.combo=true;break;case 3:single.legacy=true;break;
    case 4:single.alt=true;break;case 5:single.leaseRequested=true;break;}
   joined|=C::display12(~0ULL,single).required;
  }
  assert(joined==c.required);
 }
 auto adlsHDMI=C::adls(~0ULL,false),adlsDP=C::adls(~0ULL,true);
 faults(adlsHDMI);faults(adlsDP);
 auto adlsPanel=C::adls(~0ULL,false,true),adlsMixed=C::adls(~0ULL,true,true);
 faults(adlsPanel);faults(adlsMixed);
 assert(adlsMixed.required==(adlsPanel.required|adlsDP.required));
 assert((adlsPanel.required&(C::AUXParent|C::PanelDepth|C::PanelTiming))==(C::AUXParent|C::PanelDepth|C::PanelTiming));
 assert(!(adlsPanel.required&C::Lease));
 assert((adlsDP.required&adlsHDMI.required)==adlsHDMI.required);
 assert((adlsDP.required&C::Training12)==C::Training12);
 assert(!(adlsDP.required&(C::NativeFIA|C::AltFIA)));
 auto d13=C::display13(~0ULL);faults(d13);
 using Port=C::Port13;
 auto dp13=C::display13Port(~0ULL,Port::FixedDP);faults(dp13);
 assert(dp13.required==(C::TCNative|C::Mode|C::FixedDPProbe13|C::SST13|C::AUX13|C::Training12|C::Framing));
 assert(C::display13Port(~0ULL,Port::Panel).required==C::Panel13);
 assert(C::display13Port(~0ULL,Port::TCTMDS).required==(C::TCNative|C::Mode));
 assert(!(C::display13Port(~0ULL,Port::DPAlt).required&(C::Mode|C::FixedDPProbe13|C::DPAltBoot13)));
 assert(C::display13Port(~C::DPAltBoot13,Port::DPAlt,true).missing()==C::DPAltBoot13);
 assert(d13.required==(C::DonorRuntime|C::Power13|C::Clock13|C::MemoryGroup13|C::IRQ13|C::WMLayout13|C::Output13|C::Topology13));
 printf("PASS %u route combinations, union composition and all single-operation failures/restores\n",cases);
}
