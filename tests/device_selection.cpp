#include "kern_xelp_platform.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace XELP;
int main() {
 unsigned implemented=0;
 for(const auto &entry:DeviceTable) {
  const uint32_t pci=uint32_t(entry.device)<<16|0x8086;
  const auto revision=0U;
  auto p=identify(pci,revision);
  assert(p.identity==pci && p.effectiveIdentity==pci && !p.overridden());
  assert(p.family==entry.family && p.display==entry.display && p.gt==entry.backend);
  assert(!strcmp(model(p),entry.model));
  assert(p.admitted()==(entry.backend!=GT::Unknown));
  implemented+=p.admitted();
  const uint32_t value=entry.device;
  assert(same(p,select(pci,revision,true,&value,4)));
  // Display12 small-GT qualification requires physical identity; established
  // TGL/ADL-N explicit override semantics remain unchanged.
  auto f=select(0x12348086,revision,true,&value,4);
  assert(f.overridden() && f.identity==0x12348086 && f.effectiveIdentity==pci);
  assert(f.admitted()==(p.admitted() && !display12SmallGT(entry.backend) && entry.backend!=GT::ADLPMobile));
  assert(!strcmp(model(f),entry.model));
  auto nonIntel=select(0x123410DE,0,true,&value,4);assert(!nonIntel.admitted());
 }
 assert(implemented==60); // Recognition of other table rows adds no native backend.
 uint32_t id=0x46D2;
 assert(select(0x12348086,0,true,&id,4).admitted());
 assert(select(0x12348086,1,true,&id,4).admitted());
 assert(!identify(0x46908086,256).admitted()); // Malformed PCI revision width.
 assert(!select(0x12348086,0,false,nullptr,0).admitted());
 for(size_t n:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(5),size_t(8)})
  assert(!select(0x9A498086,0,true,&id,n).admitted());
 assert(!select(0x9A498086,0,true,nullptr,4).admitted());
 for(uint32_t bad:{0U,0xFFFFU,0x808646D2U,0x1234U})
  assert(!select(0x9A498086,0,true,&bad,4).admitted());
 auto p=select(0x46A68086,0,true,&id,4);assert(!p.admitted() && !p.displayAdmitted() && p.family==Family::AlderLakeP);
 assert(!strcmp(model(p),"Intel Iris Xe Graphics")); // Physical ADL-P never becomes an ADL-N render backend.
 auto q=p;q.effectiveIdentity=0x46D18086;assert(!same(p,q));
 ProbeConfig c;c.profile=p;c.combo=c.comboBackend=3;assert(!c.valid());
 id=0x9A49;p=select(0x46A68086,12,true,&id,4);
 assert(p.displayAdmitted() && p.admitted() && p.mobileDisplay13());
 c={};c.profile=p;c.comboBackend=3;c.legacy=8;c.legacyBackend=0x78;assert(c.valid());
 c.abi=2;assert(!c.valid());c.abi=3;c.profile.gt=GT::TGLGT2;assert(!c.valid());
 puts("PASS device table, physical/effective identity, strict override bytes/vendor/revision, model choice and ABI3");
}
