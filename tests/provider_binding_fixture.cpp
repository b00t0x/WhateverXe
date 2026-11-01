#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "AppleIntelTGLCompatPortProbe.hpp"
struct Meta {unsigned size=0x1C20;unsigned getClassSize(){return size;}};
struct OSObject {
 virtual ~OSObject()=default;
 alignas(8) uint8_t storage[0x1C20] {};
 Meta meta;unsigned retains=0;
 Meta *getMetaClass(){return &meta;}
 void retain(){++retains;}
};
struct OSData:OSObject {uint32_t value{};size_t size=4;void *getBytesNoCopy(){return &value;}size_t getLength(){return size;}};
struct IOService:OSObject {OSObject *provider {};OSObject *getProvider(){return provider;}};
struct IOPCIDevice:OSObject {uint32_t identity {};uint8_t revision {};OSObject *property {};
 OSObject *getProperty(const char *key){assert(!strcmp(key,"device-id"));return property;}
 uint32_t configRead32(unsigned r){assert(!r);return identity;}
 uint8_t configRead8(unsigned r){assert(r==8);return revision;}
};
struct IOMemoryMap:OSObject {uintptr_t address {};uint64_t bytes {};
 uintptr_t getVirtualAddress(){return address;}uint64_t getLength(){return bytes;}
};
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
#define IOLog(...) ((void)0)
// PROVIDER_SOURCE
int main(int argc,char **argv) {
 assert(XELP::display13ProbeCapable(xelpProbeCapabilities));
 assert(!XELP::display13ProbeCapable(nullptr));
 assert(XELP::display13TCProbeCapable(xelpProbeCapabilities));
 assert(!XELP::display13TCProbeCapable(nullptr));
 assert(!XELP::display13TCProbeCapable(+[](uint32_t,uint32_t)->uint32_t{return XELP::ProbeDisplay13ComboDP;}));
 assert(XELP::display13TCProbeCapable(+[](uint32_t,uint32_t)->uint32_t{return XELP::ProbeDisplay13FixedTC;}));
 assert(XELP::display13TCDPProbeCapable(xelpProbeCapabilities));
 assert(!XELP::display13TCDPProbeCapable(+[](uint32_t,uint32_t)->uint32_t{return 3;}));
 for(unsigned schema=0;schema<5;++schema)for(unsigned abi=0;abi<6;++abi)
  assert(xelpProbeCapabilities(schema,abi)==(schema==1&&abi==3?(XELP::ProbeDisplay13ComboDP|XELP::ProbeDisplay13FixedTC|XELP::ProbeDisplay13FixedDP|XELP::ProbeDisplay13Panel|XELP::ProbeDisplay13DPAlt):0));
 assert(!XELP::display13ProbeCapable(+[](uint32_t,uint32_t)->uint32_t{return 0;}));
 assert(!XELP::display13ProbeCapable(+[](uint32_t,uint32_t)->uint32_t{return 2;}));
 assert(argc==2 || argc==3);const bool adln=!strncmp(argv[1],"adln",4);
 const bool rpls=!strncmp(argv[1],"rpls",4);
 const bool adls=!strncmp(argv[1],"adls",4) || rpls;
 const bool adlp=!strncmp(argv[1],"adlp",4) || !strncmp(argv[1],"rplp",4);
 const bool legacy=!strncmp(argv[1],"adln-abi1",9);
 const bool fake=strstr(argv[1],"fake")!=nullptr;
 const bool rejected=!strcmp(argv[1],"unknown");
 IOService controller;IOPCIDevice pci;OSObject access,port,foreign;IOMemoryMap map;
 std::vector<uint8_t> mmio(0x1000000);alignas(8) uint8_t platform[0x1C0] {};
 pci.identity=adln?0x46D18086:0x9A498086;
 if(adls){pci.identity=0x46908086;pci.revision=12;}
 if(rpls){pci.identity=0xA7808086;pci.revision=!strcmp(argv[1],"rpls-revision")?8:4;}
 if(!strcmp(argv[1],"adls-revision"))pci.revision=0;
 if(!strcmp(argv[1],"tgl-g4"))pci.identity=0x9A788086;
 if(!strcmp(argv[1],"tgl-9a40"))pci.identity=0x9A408086;
 if(!strcmp(argv[1],"adln-16eu"))pci.identity=0x46D28086;
 if(!strcmp(argv[1],"unknown"))pci.identity=0x4C8A8086;
 if(!strcmp(argv[1],"adln-revision"))pci.revision=1;
 OSData fakeID;
 if(fake){pci.identity=0x12348086;fakeID.value=adln?0x46D2:0x9A49;pci.property=&fakeID;}
 if(adlp){pci.identity=!strncmp(argv[1],"rplp",4)?0xA7A88086:0x46A68086;pci.revision=12;
  if(!strcmp(argv[1],"adlp-fb")){fakeID.value=0x9A49;pci.property=&fakeID;}}
 if(argc==3)pci.identity=(uint32_t(strtoul(argv[2],nullptr,16))<<16)|0x8086;
 controller.provider=&pci;platform[0x12]=9;
 map.address=reinterpret_cast<uintptr_t>(mmio.data());map.bytes=mmio.size();
 field<void *>(&controller,0xC40)=&access;field<void *>(&controller,0xD40)=platform;
 field<OSObject *>(&controller,0xC70)=&map;field<uintptr_t>(&access,0x50)=map.address;
 field<uint32_t>(&access,0x60)=map.bytes;access.meta.size=0x68;
 port.meta.size=0x6B0;field<void *>(&port,0x578)=&access;
 // Before registration, the port cannot read MMIO or infer its generation.
 auto initial=reinterpret_cast<uint32_t *>(platform+0x20);
 const uint32_t initialValues[]={0,1,2,0,0x800,0x202};memcpy(initial,initialValues,sizeof(initialValues));
 field<void *>(&port,0x548)=initial;
 assert(!tglProbePort(&port));
 assert(!hardwareAllowed(nullptr));IOService orphan;assert(!hardwareAllowed(&orphan));
 const bool physicalTGL=!adln && !adls && !adlp && !rejected;
 assert(hardwareAllowed(&controller)==(!adln && !adlp && !rejected));
 assert(tglProbePort(&port)==(physicalTGL?1U:0U));
 field<void *>(&port,0x578)=&foreign;assert(!tglProbePort(&port));field<void *>(&port,0x578)=&access;
 XELP::ProbeConfig config;config.profile=XELP::deviceProfile(&pci);
 xelpSelectedGPU=&pci;xelpSelectedProfile=config.profile;
 assert(xelpSelectedDevice(&pci)==config.profile.displayAdmitted());
 assert(xelpDisplay13RenderDevice(&pci)==((adln || adlp) && !rejected));
 assert(xelpDisplay13Device(&pci)==((adln || adlp) && !rejected));
 IOPCIDevice foreignPCI;foreignPCI.identity=pci.identity;foreignPCI.revision=pci.revision;foreignPCI.property=pci.property;
 assert(!xelpSelectedDevice(&foreignPCI));
 assert(!xelpDisplay13Device(&foreignPCI));
 config.combo=3;config.comboBackend=adln?3:7;config.legacy=adln?0:0x1F8;config.legacyBackend=adln?0:0x1F8;
 if(adls){config.combo=8;config.comboBackend=0x19;config.legacy=config.legacyBackend=0;}
 if(adls && !rejected){assert(hardwareAllowed(&controller));assert(!tglProbePort(&port));}
 if(adlp){config.combo=0;config.comboBackend=3;config.legacy=8;config.legacyBackend=0x78;}
 // The contract derives generation from physical identity, not boot args.
 assert(!xelpConfigurePortProbe(&controller,2,3,3));
 if(!adln)assert(!xelpConfigurePortProbe(&controller,1,3,3));
 if(rejected) {
  assert(!xelpConfigureDisplay(&controller,&config));
  assert(!xelpConfigurePortProbe(&controller,1,3,3));
  auto fake=config;fake.profile=XELP::identify(0x46D18086,0);fake.comboBackend=3;fake.legacy=fake.legacyBackend=0;
  assert(!xelpConfigureDisplay(&controller,&fake));
  assert(!probeBindingState && !controller.retains && !access.retains && !map.retains);
  assert(!tglProbePort(&port));
  printf("PASS rejected physical owner %s: no registration or legacy fallback.\n",argv[1]);return 0;
 }
 auto malformed=config;malformed.abi=1;assert(!xelpConfigureDisplay(&controller,&malformed));
 malformed=config;malformed.bytes--;assert(!xelpConfigureDisplay(&controller,&malformed));
 malformed=config;malformed.profile.display=XELP::Display::Unknown;assert(!xelpConfigureDisplay(&controller,&malformed));
 pci.identity^=0x10000;assert(!xelpConfigureDisplay(&controller,&config));pci.identity^=0x10000;
 map.address++;assert(!xelpConfigureDisplay(&controller,&config));map.address--;
 assert(!controller.retains && !access.retains && !map.retains && !probeBindingState);
 if(fake) {
  auto lied=config;lied.profile.identity=0x99998086;assert(!xelpConfigureDisplay(&controller,&lied));
  pci.property=&foreign;assert(!xelpSelectedDevice(&pci));assert(!xelpConfigureDisplay(&controller,&config));pci.property=&fakeID;
  const auto original=fakeID.value;fakeID.value=0xFFFF;assert(!xelpConfigureDisplay(&controller,&config));fakeID.value=original;
 }
 if(legacy)assert(xelpConfigurePortProbe(&controller,1,3,3));
 else assert(xelpConfigureDisplay(&controller,&config));
 assert(controller.retains==1 && access.retains==1 && map.retains==1);
 assert(xelpConfigureDisplay(&controller,&config));assert(controller.retains==1);
 malformed=config;malformed.combo=1;assert(!xelpConfigureDisplay(&controller,&malformed));
 if(adln){assert(xelpConfigurePortProbe(&controller,1,3,3));assert(!xelpConfigurePortProbe(&controller,2,3,3));}
 if(adlp){
  auto rec=reinterpret_cast<uint32_t *>(platform+0x20+24*3);
  const uint32_t values[]={3,9,2,1,0x800,0x202};memcpy(rec,values,sizeof(values));field<void *>(&port,0x548)=rec;
  for(unsigned ready=0;ready<16;++ready)for(unsigned hpd=0;hpd<2;++hpd){
   field<uint32_t>(mmio.data(),0x161500)=ready;field<uint32_t>(mmio.data(),0xc4000)=hpd<<24;
   assert(tglProbePort(&port)==(hpd && (ready&7)==4?4U:0U));
  }
  rec[2]=1;rec[4]=0x400;rec[5]=0x201;
  for(unsigned ready=0;ready<16;++ready)for(unsigned hpd=0;hpd<2;++hpd){
   field<uint32_t>(mmio.data(),0x161500)=ready;field<uint32_t>(mmio.data(),0xc4000)=hpd<<24;
   assert(tglProbePort(&port)==(hpd && !(ready&3)?4U:0U));
  }
  rec[1]=10;assert(!tglProbePort(&port));
  assert(!hardwareAllowed(&controller));puts("PASS actual ADL-P Display13 provider: physical identity, legacy TC probe, no Display12/GT fallback");return 0;
 }
 for(unsigned d=0;d<9;++d){auto rec=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
  const uint32_t values[]={d,adls?9:d+1,2,0,0x800,0x202};memcpy(rec,values,sizeof(values));
  field<void *>(&port,0x548)=rec;
  if(d>=3){*reinterpret_cast<uint32_t *>(mmio.data()+0xC4000)=1U<<(24+d-3);
   *reinterpret_cast<uint32_t *>(mmio.data()+0x1638A0)=0x10;
   const uint32_t bases[]={0x163000,0x16E000,0x16F000};const auto base=bases[(d-3)/2];
   *reinterpret_cast<uint32_t *>(mmio.data()+base+0x8A0)=0x10;
   *reinterpret_cast<uint32_t *>(mmio.data()+base+0x890)=1U<<((d-3)%2);}
  assert(tglProbePort(&port)==(adls?(d==3?1U:0U):adln?(d<2?1U:0U):(d<3?1U:4U)));
  if(adln){
   const uint32_t dp[]={d,d+1,1,0,0x400,0x201};memcpy(rec,dp,sizeof(dp));
   assert(tglProbePort(&port)==(d<2?1U:0U));
   if(d<2)for(unsigned n=0;n<6;++n){rec[n]^=1;assert(!tglProbePort(&port));rec[n]^=1;}
  }
  field<void *>(&port,0x578)=&foreign;assert(!tglProbePort(&port));field<void *>(&port,0x578)=&access;
  field<void *>(&port,0x548)=reinterpret_cast<void *>(1);assert(!tglProbePort(&port));
 }
 // No second ownership epoch, no rebinding to a changed platform.
 field<void *>(&controller,0xD40)=nullptr;assert(!xelpConfigureDisplay(&controller,&config));
 assert(controller.retains==1 && access.retains==1 && map.retains==1);
 printf("PASS actual %s provider entries: ABI/profile/map validation, publish-once pinning, idempotence, foreign/stale binding rejection and native port probe. CPU only.\n",argv[1]);
}
