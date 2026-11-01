#include "kern_xelp_probe.hpp"
#include "kern_xelp_donor.hpp"
#include "kern_tgl_fb_binding.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace XELP;
struct IO {
 std::map<uint32_t,uint32_t> regs;std::vector<uint32_t> reads;
 bool readable(uint32_t r){return regs.count(r);}
 uint32_t read(uint32_t r){reads.push_back(r);return regs.at(r);}
};
int main() {
 auto candidate=identify(0x46a68086,12,0x9a498086);
 assert(candidate.mobileDisplay13() && candidate.displayAdmitted() && candidate.admitted());
 assert(candidate.family==Family::AlderLakeP && candidate.display==Display::XeLPD && candidate.gt==GT::ADLPMobile);
 assert(identify(0x46a68086,12).displayAdmitted());
 assert(identify(0x46a38086,12,0x9a498086).displayAdmitted());

 // ABI3 explicitly describes fixed TC HDMI; capability query prevents an
 // old provider from accepting a configuration whose implementation it lacks.
 for(unsigned d=3;d<=6;++d)for(unsigned ready=0;ready<16;++ready)for(unsigned h=0;h<2;++h) {
  alignas(8) uint8_t platform[0x1c0] {};
  ProbeConfig c;c.profile=identify(0x46d18086,0);c.comboBackend=3;c.legacy=1U<<d;c.legacyBackend=0x78;
  ProbeBinding b{1,2,reinterpret_cast<uintptr_t>(platform),c};assert(b.valid());
  auto rec=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
  const uint32_t native[]={d,d+6,2,1,0x800,0x202};memcpy(rec,native,sizeof(native));
  IO io;io.regs={{0xc4000,h<<(21+d)},{0x161500+4*(d-3),ready}};
  assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec)==(h && (ready&7)==4?4U:0U));
  assert(io.reads[0]==0x161500+4*(d-3));
  for(unsigned f:{0U,1U,2U,3U,4U,5U}) {
   auto old=rec[f];rec[f]=~0U;io.reads.clear();
   assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec) && io.reads.empty());rec[f]=old;
  }
  auto bad=c;bad.legacyBackend=0;assert(!bad.valid());bad=c;bad.typeC=c.legacy;assert(!bad.valid());
 }
 static_assert(sizeof(Profile)==28 && sizeof(ProbeConfig)==60,"Keep existing provider ABI3 layout");
 for(auto id:{0xA7808086U,0xA7828086U,0xA7888086U,0xA78B8086U}) {
  auto p=identify(id,12);
  assert(displayAdapter(p.family)==DisplayAdapter::ADLS && p.gt==GT::ADLSSmall && p.admitted());
  ProbeConfig c;c.profile=p;c.combo=0x19;c.comboBackend=0x19;
  assert(c.valid()); // Registered desktop GT uses the common Display adapter.
  assert(!identify(id,12,0x46908086).admitted());
 }

 for(uint32_t id=0;id<65536;++id)for(unsigned rev:{0U,1U,4U,8U,12U,255U}) {
  const auto p=identify((id<<16)|0x8086,rev);
  const bool tgl=id==0x9A59 || id==0x9AC0 || id==0x9AC9 || id==0x9AD9 || id==0x9AF8 || id==0x9A40 || id==0x9A49 || id==0x9A78 || id==0x9A60 || id==0x9A68 || id==0x9A70,adln=id>=0x46D0 && id<=0x46D4;
  const bool adls=id==0x4680 || id==0x4682 || id==0x4688 || id==0x468A || id==0x468B || id==0x4690 || id==0x4692 || id==0x4693;
  const bool rpls=id==0xA780 || id==0xA781 || id==0xA782 || id==0xA783 || id==0xA788 || id==0xA789 || id==0xA78A || id==0xA78B;
  const bool mobile=id==0x4626 || id==0x4628 || id==0x462a || (id>=0x46a0 && id<=0x46a2) || (id>=0x46b0 && id<=0x46b2) || (id>=0x46c0 && id<=0x46c2) || id==0xa720 || id==0xa721 || id==0x46a3 || id==0x46a6 || id==0x46a8 || id==0x46aa || id==0x46b3 || id==0x46c3 || id==0xa7a0 || id==0xa7a1 || (id>=0xa7a8 && id<=0xa7ad);
  assert(p.admitted()==(tgl || adln || adls || rpls || mobile));
  if(mobile){assert(p.mobileDisplay13());assert(!identify((id<<16)|0x8086,rev,0x46d18086).admitted());}
  if(adls||rpls)assert(!identify((id<<16)|0x8086,rev,0x9A498086).admitted());
  assert(!adln || p.display==Display::XeLPD);
  assert(!identify((id<<16)|0x1234,rev).admitted());
 }
 alignas(8) uint8_t platform[0x1C0] {};
 ProbeConfig c;c.profile=identify(0x9A498086,1);c.combo=2;c.comboBackend=7;c.legacyBackend=0x1F8;c.legacy=0x1F8;
 ProbeBinding b {1,2,reinterpret_cast<uintptr_t>(platform),c};assert(b.valid());
 unsigned cases=0;
 // Independent Linux FIA layouts and exact MMIO read order for every TC.
 for(unsigned d=3;d<=8;++d)for(unsigned h=0;h<2;++h)for(unsigned mod=0;mod<2;++mod)
 for(unsigned ready=0;ready<2;++ready)for(unsigned alt=0;alt<4;++alt) {
  const unsigned tc=d-3,instance=mod?tc/2:0,index=mod?tc%2:tc<4?tc:0;const uint32_t bases[]={0x163000,0x16E000,0x16F000};
  const auto liveReg=bases[instance]+0x8A0,readyReg=bases[instance]+0x890;
  IO io;io.regs={{0xC4000,h<<(24+tc)},{0x1638A0,mod<<4},{liveReg,(mod<<4)|(alt<<(5+8*index))},{readyReg,ready<<index}};
  // For FIA1 the per-port fields share its topology word.
  io.regs[liveReg]=(mod<<4)|(alt<<(5+8*index));
  io.regs[0x1638A0]|=mod<<4;
  auto record=reinterpret_cast<uint32_t *>(platform+0x20+24*d);record[0]=d;
  const auto mode=probeMode(io,b,2,reinterpret_cast<uintptr_t>(record),record);
  assert(mode==(h && (mod || tc<4) && ready && !alt?4U:0U));
  std::vector<uint32_t> expected {0xC4000};
  if(h){expected.push_back(0x1638A0);if(mod || tc<4){if(instance)expected.push_back(liveReg);expected.push_back(readyReg);}}
  assert(io.reads==expected);++cases;
 }
 // Selection never grants an unknown, Type-C or excluded port access.
 c.legacy=0;c.typeC=1U<<4;b.config=c;IO io;
 uint32_t *rec=reinterpret_cast<uint32_t *>(platform+0x20+24*4);rec[0]=4;
 assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec) && io.reads.empty());
 for(unsigned bad=0;bad<5;++bad){auto x=c;if(bad==0)x.abi=1;if(bad==1)x.bytes--;
  if(bad==2)x.profile.display=Display::XeLPD;if(bad==3)x.legacy=x.typeC;if(bad==4)x.legacyBackend=1U<<9;
  assert(!x.valid());}
 c={};c.profile=identify(0x46D18086,0);c.combo=3;c.comboBackend=3;b.config=c;
 for(unsigned d=0;d<9;++d){auto r=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
  const uint32_t v[]={d,d+1,2,0,0x800,0x202};__builtin_memcpy(r,v,sizeof(v));
  assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(r),r)==(d<2?1U:0U));}
 assert(io.reads.empty());auto other=b;other.access=3;assert(!b.sameBinding(other));assert(b.sameBinding(b));
 // Display13 Combo protocol/port arrangements compose without FIA reads.
 unsigned dpCases=0;
 for(unsigned selected=1;selected<4;++selected)for(unsigned dpMask=0;dpMask<4;++dpMask){
  if(dpMask&~selected)continue;
  c.combo=selected;c.comboBackend=3;b.config=c;
  for(unsigned d=0;d<2;++d){
   auto rec=reinterpret_cast<uint32_t *>(platform+0x20+24*d);const bool dp=dpMask&(1U<<d);
   const uint32_t record[]={d,d+1,dp?1U:2U,0,dp?0x400U:0x800U,dp?0x201U:0x202U};memcpy(rec,record,sizeof(record));
   assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec)==((selected&(1U<<d))?1U:0U));
   if(dp)for(unsigned field=0;field<6;++field){rec[field]^=1;
    assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(rec),rec));rec[field]^=1;}
   ++dpCases;
  }
 }
 assert(dpCases==16&&io.reads.empty());
 c={};c.profile=identify(0x46908086,12);c.combo=8;c.comboBackend=0x19;b.config=c;assert(c.valid());
 for(unsigned d=0;d<9;++d){auto r=reinterpret_cast<uint32_t *>(platform+0x20+24*d);
  const uint32_t v[]={d,9,2,0,0x800,0x202};memcpy(r,v,sizeof(v));
  assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(r),r)==(d==3?1U:0U));}
 c.combo=24;c.comboBackend=0x19;b.config=c;assert(c.valid());
 auto dp=reinterpret_cast<uint32_t *>(platform+0x80);const uint32_t dpc[]={4,10,1,0,0x400,0x201};memcpy(dp,dpc,sizeof(dpc));
 assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(dp),dp)==1);
 for(unsigned i=0;i<6;++i){dp[i]^=1;assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(dp),dp));dp[i]^=1;}
 c.combo=8;c.comboBackend=0x19;b.config=c;assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(dp),dp));
 assert(io.reads.empty()); // ADL-S must never probe TGL FIA for its Combo TC1.
 for(unsigned bad=0;bad<5;++bad){auto x=c;if(bad==0)x.combo=2;if(bad==1)x.comboBackend=0x79;
  if(bad==2)x.legacy=8;if(bad==3)x.typeC=8;if(bad==4)x.profile.revision=256;assert(!x.valid());}
 assert(!identify(0x46D18086,12,0x46908086).admitted());
 assert(!identify(0x46908086,12,0x46808086).admitted());
 // Panel mode requires selected Combo A and the exact native panel record.
 c.combo=1;b.config=c;auto panel=reinterpret_cast<uint32_t*>(platform+0x20);
 const uint32_t panelRecord[]={0,0,0,0,2,0x18};memcpy(panel,panelRecord,sizeof(panelRecord));
 assert(probeMode(io,b,2,reinterpret_cast<uintptr_t>(panel),panel)==1 && io.reads.empty());
 for(unsigned field=0;field<6;++field){panel[field]^=1;assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(panel),panel));panel[field]^=1;}
 c.combo=8;b.config=c;assert(!probeMode(io,b,2,reinterpret_cast<uintptr_t>(panel),panel));
 // CPU association and power-consumer accounting are independent of transport.
 NativePorts ports {platform,(1U<<0)|(1U<<4)|(1U<<6)};
 assert(ports.bound(platform+0x20+24*4,4));assert(!ports.bound(platform+0x20+24*3,4));
 TGLFBBinding::PublishedPath paths[3];uintptr_t owners[]={1,2,3};unsigned dd[]={6,0,4};
 for(unsigned p=0;p<3;++p){TGLFBBinding::Readout r {TGLFBBinding::Status::Complete,2-p,p,dd[p],5*p};
  assert(TGLFBBinding::publish(paths[p],owners[p],10+p,r,p));}
 auto map=TGLFBBinding::powerPipes(paths,owners,ports.selected);
 assert(map.valid && map.known==ports.selected && map.mask[6]==1 && map.mask[0]==2 && map.mask[4]==4);
 owners[1]=0;paths[1]={};map=TGLFBBinding::powerPipes(paths,owners,ports.selected);
 assert(map.valid && map.mask[0]==7 && map.mask[6]==1 && map.mask[4]==4);
 for(uint32_t n:{0U,1U,4095U,4096U,0x4000000U,0x4001000U,UINT32_MAX})
  assert(XELPDonor::displayPoolRequest(1,n)==(n && !(n&4095) && n<=0x4000000));
 puts("PASS exhaustive PCI/revision admission, typed ABI/profile rejection, TGL six-port result/read-order parity, no Type-C reads, ADL-N Combo parity, three-pipe mixed-port publication/retirement.");
}
