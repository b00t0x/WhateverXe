#include "kern_dsi_boot.hpp"
#include "kern_display13_boot.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <fstream>
#include <vector>
using namespace XELP::DSI;
struct IO {
 std::map<uint32_t,uint32_t> regs;
 std::vector<uint32_t> reads,writes;
 XELPMemory::Layout memory=XELPMemory::decode(0x03C1,0x7A800000,0);
 uint32_t ptes=0,badPTE=UINT32_MAX,missing=UINT32_MAX,changeAt=UINT32_MAX,changeReg=0;
 bool driftOnWrite=false,strict=false;
 IO(){
  assert(XELPMemory::reservation(memory,memory.base+memory.bytes-0x100000));
  regs={{0x7B008,0xC0000000},{0x6B400,0x80030006},{0x45404,0xFC0F},{0x70008,0},
   {0x6B030,0x20130300},{0x6B094,1},{0x6B404,0},{0x6B034,0},{0x64000,0x80000000},{0x164280,0x1E07801},
   {0x46014,0xCC000000},{0x16428C,0x3001FB},{0x164290,0xE84},
   {0x6B000,0x04650437},{0x6B008,0x044F044D},{0x6B00C,0x07D1077F},{0x6B014,0x07A907A7},
   {0x51004,0x40000000},{0x70180,0x94000008},{0x70188,68},{0x70190,0x077F0437}};
 }
 bool readable(uint32_t reg){return reg<0x1000000 && reg!=missing && (!strict || regs.count(reg) || reg>=0x800000);}
 uint32_t read(uint32_t reg){assert(readable(reg));reads.push_back(reg);return regs[reg];}
 uint64_t readPTE(uint32_t reg){
  assert(reg==0x800000+8*ptes);const auto index=ptes++;
  if(index==changeAt)regs[changeReg]^=1;
  return memory.pte(uint64_t(index)*4096)^(index==badPTE?4096:0);
 }
 void write(uint32_t reg,uint32_t v){writes.push_back(reg);regs[reg]=v;if(driftOnWrite)regs[0x6B000]^=1;}
};
Profile profile(){Profile p;p.status=ProfileStatus::Valid;p.lanes=4;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};return p;}
Boot capture(IO &io){return captureBoot(io,AlderLakeN::readDisplay(io,true),profile(),io.memory,true);}
int main(int argc,char **argv){
 // The pre-handoff audit reports real qualification failures without writes
 // or promoting its scratch certificate into an active owner.
 {
  IO observed;auto normal=AlderLakeN::readDisplay(observed,true);Boot scratch;BootAudit audit;
  observed.reads.clear();auditBoot(observed,normal,profile(),observed.memory,false,scratch,audit);
  assert(audit.words[0]==1 && audit.words[1]==uint32_t(BootStatus::Unavailable));
  assert(observed.reads.empty() && !observed.ptes && observed.writes.empty());
  Profile invalid;
  auditBoot(observed,normal,invalid,observed.memory,true,scratch,audit);
  assert(observed.reads.empty() && !observed.ptes);
  auditBoot(observed,normal,profile(),observed.memory,true,scratch,audit);
  assert(scratch.complete() && audit.words[12]==observed.memory.bytes/4096);
  assert(audit.words[22]==1080 && audit.words[23]==1920 && audit.words[24]==4352);
  assert(audit.words[29]==1 && audit.words[30]==811500 && observed.writes.empty());
 }
 for(bool dsb:{false,true}) {
  IO observed;auto normal=AlderLakeN::readDisplay(observed,true);Boot scratch;BootAudit audit;
  if(dsb)observed.regs[0x70B08]=1;else observed.badPTE=17;
  auditBoot(observed,normal,profile(),observed.memory,true,scratch,audit);
  if(dsb)assert(audit.words[1]==uint32_t(BootStatus::Peers) && audit.words[7]==0x70B08 && audit.words[8]==1 && !observed.ptes);
  else assert(audit.words[1]==uint32_t(BootStatus::PTE) && audit.words[9]==0x800000+8*17 &&
      audit.words[12]==17 && audit.words[10]!=audit.words[11] && audit.words[11]==observed.memory.pte(17*4096));
  assert(observed.writes.empty());
 }
 {
  IO observed;auto normal=AlderLakeN::readDisplay(observed,true);Boot scratch;BootAudit audit;
  observed.changeAt=4;observed.changeReg=0x45404;
  auditBoot(observed,normal,profile(),observed.memory,true,scratch,audit);
  assert(audit.words[1]==uint32_t(BootStatus::Changed) && observed.reads.back()==0x45404 && observed.writes.empty());
 }
 IO io;auto boot=capture(io);assert(boot.status==BootStatus::Candidate && !boot.complete());
 assert(boot.geometry.width==1080 && boot.geometry.height==1920 && boot.geometry.pitch==4352 && boot.dpt==0);
 assert(verifyBootPrefix(io,boot,io.memory) && boot.complete() && boot.verifiedPages==io.memory.bytes/4096);
 assert(io.writes.empty() && io.ptes==boot.verifiedPages);
 // Candidate capture accepts no unsupported active peer or scaler/DSC path.
 for(auto change:std::map<uint32_t,uint32_t>{{0x7B808,0x80000000},{0x71008,0x80000000},{0x61400,0x80000000},
  {0x70080,1},{0x70280,0x80000000},{0x70780,0x80000000},{0x71180,0x80000000},
  {0x68180,0x80000000},{0x78000,0x80000000},{0x70B08,1},{0x71D08,0x80000000},
  {0x70180,0x94000408},{0x7018C,1},{0x701A4,1},{0x701AC,4096},{0x7019C,1},{0x70188,16},
  {0x6B000,0x04650438},{0x51004,0},{0x42084,UINT32_MAX}}){
  IO bad;bad.regs[change.first]=change.second;assert(!capture(bad).usable() && !bad.ptes && bad.writes.empty());
 }
 for(unsigned index:{0U,8191U,24575U}){
  IO bad;auto b=capture(bad);bad.badPTE=index;assert(!verifyBootPrefix(bad,b,bad.memory));
  assert(b.status==BootStatus::PTE && b.verifiedPages==index && bad.ptes==index+1 && bad.writes.empty());
 }
 for(auto reg:{0x70180U,0x70188U,0x70190U,0x7019CU,0x701ACU,0x42084U,0x6B000U,0x164280U,0x45404U}){
  IO bad;auto b=capture(bad);bad.changeAt=4;bad.changeReg=reg;bad.reads.clear();
  assert(!verifyBootPrefix(bad,b,bad.memory) && b.status==BootStatus::Changed && bad.writes.empty());
  if(reg==0x45404)assert(bad.reads.back()==0x45404); // no DSI/pipe read after power loss
 }
 for(auto reg:{0x70B08U,0x7B808U,0x701ACU,0x51004U}){IO bad;bad.missing=reg;assert(!capture(bad).usable());}
 IO invalid;invalid.memory.reservationKnown=false;assert(capture(invalid).status==BootStatus::Memory);
 IO changed;auto b=capture(changed);auto m=changed.memory;m.allocationEnd-=4096;
 assert(!verifyBootPrefix(changed,b,m) && !changed.ptes);
 // The existing unused-TC cleanup must also retain a DSI source witness.
 for(bool drift:{false,true}){
  IO tc;Display13OutputWiring w;w.valid=true;w.dsi=1;w.hdmi=8;w.ddc[3]=9;
  tc.regs[0x64300]=0xC0;tc.regs[0x161500]=0xC;tc.regs[0x46110]=0;
  auto b=capture(tc);assert(b.usable());auto normal=AlderLakeN::readDisplay(tc,true);
  tc.driftOnWrite=drift;auto result=Display13Boot::releaseUnusedTC(tc,normal,true,w,&b.source);
  if(drift)assert(result.status==Display13Boot::Status::Readback && tc.writes.size()==1);
  else assert(result.status==Display13Boot::Status::Complete && result.cleared==8 && tc.regs[0x64300]==0x80);
  assert(tc.regs[0x70180]==b.control && tc.regs[0x7019C]==b.surface && tc.regs[0x164280]==b.source.mux);
 }
 if(argc==2){
  IO saved;saved.regs.clear();saved.strict=true;std::ifstream f(argv[1]);assert(f);uint32_t a,v;
  while(f>>std::hex>>a>>v)assert(saved.regs.emplace(a,v).second);assert(f.eof());
  // The saved report omits DSB CTRL and real PTEs. Supply explicit synthetic
  // idle DSBs/PTEs for this geometry replay; never call it a live certificate.
  for(unsigned p=0;p<3;++p)for(unsigned d=0;d<3;++d)saved.regs.emplace(0x70B08+0x1000*p+0x100*d,0);
  auto actual=capture(saved);assert(actual.usable());
  assert(actual.geometry.width==1080 && actual.geometry.height==1920 && actual.geometry.pitch==4352);
  assert(verifyBootPrefix(saved,actual,saved.memory));
  puts("saved GOP register geometry accepted; DSB idle and stolen PTEs are synthetic, not collected evidence");
 }
 puts("PASS DSI GOP certificate: native portrait pitch, full prefix, peer exclusion, source drift, unused TC isolation; no scanout/PTE writes");
}
