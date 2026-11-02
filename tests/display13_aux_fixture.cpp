// Uses display13_sst_fixture's real binding/bridge setup. Only the transport
// completion and kernel imports are mocked; optional native callers execute
// their original get/put, retry, buffer-copy and result paths.
bool failGet=false,failPut=false,failCommand=false;unsigned auxCalls=0,auxResult=0;
bool disconnectDuringCommand=false,loseTCIdentity=false;
void getAux(void *power,unsigned ddi){
 assert(!locked&&power==display13FBPower.power);auto &c=display13FBPower;Display13FBPowerIO io{c.map};
 if(failGet)c.map->drop=0x45444;
 auto r=c.bridge.getAUX(io,ddi);c.map->drop=0;
 if(r.status!=Display13Power::Status::Complete)c.bridge.state.faulted=true;
}
void putAux(void *power,unsigned ddi){
 assert(!locked&&power==display13FBPower.power);auto &c=display13FBPower;Display13FBPowerIO io{c.map};
 if(failPut)c.map->drop=0x45444;
 auto r=c.bridge.putAUX(io,ddi);c.map->drop=0;
 if(r.status!=Display13Power::Status::Complete)c.bridge.state.faulted=true;
}
unsigned auxTransport(void *port,unsigned,uint32_t *packet,unsigned,bool){
 assert(!locked);++auxCalls;auto &c=display13FBPower;Display13FBPowerIO io{c.map};
 const auto ddi=Display13FBMemory::field<unsigned>(port,0x120);
 assert(c.bridge.auxBusy()&&c.bridge.auxHeld(io,ddi));
 const auto writes=c.map->writes.size();
 assert(c.bridge.suspend(io).status==Display13Power::Status::NotReady&&c.map->writes.size()==writes);
 assert(c.bridge.putAUX(io,ddi).status==Display13Power::Status::Unbalanced&&c.map->writes.size()==writes);
 // Nested peer-native transactions preserve the active command's scope.
 assert(c.bridge.getAUX(io,ddi).status==Display13Power::Status::Complete);
 assert(c.bridge.putAUX(io,ddi).status==Display13Power::Status::Complete&&c.bridge.auxBusy());
 packet[0]=0;packet[1]=0x12345678;
 if(failCommand)c.map->regs[0x45444]=0;
 if(disconnectDuringCommand)tcConnected=false;
 if(loseTCIdentity)tcOwned=false;
 return auxResult;
}
unsigned syntheticFront(void *port,unsigned,void *data,unsigned){
 auto &c=display13FBPower;const auto ddi=Display13FBMemory::field<unsigned>(port,0x120);uint32_t packet[5]{};
 getAux(c.power,ddi);auto r=XE::wrapDisplay13RunAUX(port,0,packet,4,false);putAux(c.power,ddi);
 if(!r)memcpy(data,packet+1,4);return r;
}
unsigned syntheticI2C(void *port,unsigned command,uint32_t *data,unsigned length,uint8_t){return syntheticFront(port,command,data,length);}
unsigned unsafeTransport(void*,unsigned,uint32_t *packet,unsigned,bool){++auxCalls;packet[0]=0;packet[1]=0x12345678;return 0;}
void waitAUX(unsigned){assert(!locked);}
void copyAUX(const void *src,void *dst,size_t bytes){memcpy(dst,src,bytes);}
void stackFail(){assert(false);}
void auxSetup(Fixture &f,Map &map,unsigned ddi,bool hdmi,unsigned mode){
 setup(f,map,ddi,0,2,20);auto &c=display13FBPower;state.display13FBAUXHooksReady=true;
 if(hdmi){state.adlnOutputWiring.dp=0;state.adlnOutputWiring.hdmi=3;state.adlnOutputWiring.ddc[0]=1;state.adlnOutputWiring.ddc[1]=2;
  assert(Display13FBTopology::apply(f.platform,sizeof(f.platform),f.donor,state.adlnOutputWiring));}
 state.display13AUXControllerSlot=reinterpret_cast<uintptr_t>(&c.controller);put(f.ctl,0xC30,c.lock);
 for(unsigned n=0;n<6;++n)put(f.port,0x594+4*n,0x64010U+0x100*ddi+4*n);
 put(f.port,0x550,mode);
 state.orgDisplay13ReadAUX=state.orgDisplay13WriteAUX=reinterpret_cast<uintptr_t>(syntheticFront);
 state.orgDisplay13I2CAUX=reinterpret_cast<uintptr_t>(syntheticI2C);state.orgDisplay13RunAUX=reinterpret_cast<uintptr_t>(auxTransport);
 failGet=failPut=failCommand=false;auxCalls=auxResult=0;
 disconnectDuringCommand=loseTCIdentity=false;
 // Probe must work after all DDI/pipe owners are gone.
 Display13FBPowerIO io{&map};assert(c.bridge.putDDI(io,ddi).status==Display13Power::Status::Complete);
 assert(c.bridge.putPG(io,1).status==Display13Power::Status::Complete);
 assert(!c.bridge.state.refs[0]);
}
int main(int argc,char **argv){
 uint8_t *image=nullptr;uint64_t canary=0x123456789ABCDEF0ULL;uintptr_t portVT[64]{};
 portVT[0x170/8]=reinterpret_cast<uintptr_t>(XE::wrapDisplay13RunAUX);
 if(argc>1){
  std::ifstream f(argv[1],std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
  image=static_cast<uint8_t *>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON,-1,0));assert(image!=MAP_FAILED);
  memcpy(image,bytes.data(),bytes.size());put(image,0xC80A8,reinterpret_cast<uintptr_t>(&canary));
  jump(image+0x5C4BA,reinterpret_cast<uintptr_t>(getAux));jump(image+0x5AE0A,reinterpret_cast<uintptr_t>(putAux));
  for(unsigned at:{0x1CDF6U,0x1D0F7U,0x1D3B2U,0x1D46DU,0x1D52AU})redirect(image,at,0x300000,reinterpret_cast<uintptr_t>(waitAUX));
  for(unsigned at:{0x1CE6CU,0x1D0AFU})redirect(image,at,0x300020,reinterpret_cast<uintptr_t>(copyAUX));
  for(unsigned at:{0x1CEABU,0x1D19CU})redirect(image,at,0x300040,reinterpret_cast<uintptr_t>(stackFail));
 }
 unsigned cases=0;
 // A selected DSI path must never reach native AUX get/put or HAL, even if
 // its port object has the donor's AUX-A register coordinates initialized.
 {Fixture f(0,0,0);Map map;auxSetup(f,map,0,false,1);
  state.adlnOutputWiring.dp&=~1U;state.adlnOutputWiring.panel=0;state.adlnOutputWiring.dsi=1;
  assert(Display13FBTopology::apply(f.platform,sizeof(f.platform),f.donor,state.adlnOutputWiring));
  const auto reads=map.reads.size(),writes=map.writes.size();uint32_t data[5]{0xabcdef01};
  assert(XE::wrapDisplay13ReadAUX(f.port,0x100,data,4)==kIOReturnNotReady);
  assert(XE::wrapDisplay13WriteAUX(f.port,0x100,data,4)==kIOReturnNotReady);
  assert(XE::wrapDisplay13I2CAUX(f.port,0,data,4,0)==kIOReturnNotReady);
  assert(XE::wrapDisplay13RunAUX(f.port,0,data,4,false)==kIOReturnNotReady);
  assert(!auxCalls && data[0]==0xabcdef01 && map.reads.size()==reads && map.writes.size()==writes);
  assert(!display13FBPower.bridge.state.faulted && !display13FBPower.bridge.nativeAUXRefs(0));
 }
 // Exact production completion checks: a disconnect during AUX is a failed
 // transaction, not a permanently faulted bridge. Lost ownership still is.
 for(unsigned ddi=3;ddi<=6;++ddi)for(bool lost:{false,true})for(unsigned nativeError:{0U,0xE00002BCU}) {
  Fixture f(0,0,ddi);Map map;auxSetup(f,map,ddi,false,3);auto &c=display13FBPower;
  tcOwned=true;disconnectDuringCommand=true;loseTCIdentity=lost;auxResult=nativeError;
  uint32_t data[5]{};
  assert(XE::wrapDisplay13ReadAUX(f.port,0x202,data,4)==(nativeError?nativeError:kIOReturnNotReady));
  assert(auxCalls==1 && !c.bridge.auxBusy() && c.bridge.state.faulted==lost);
  assert(!data[0]);
  if(!lost) {
   assert(!c.bridge.nativeAUXRefs(ddi));
   const auto calls=auxCalls;
   assert(XE::wrapDisplay13ReadAUX(f.port,0x202,data,4)==kIOReturnNotReady && auxCalls==calls);
   tcConnected=true;disconnectDuringCommand=false;auxResult=0;
   assert(!XE::wrapDisplay13ReadAUX(f.port,0x202,data,4) && data[0]==0x12345678);
   assert(!c.bridge.state.faulted && !c.bridge.nativeAUXRefs(ddi));
  }
 }
 for(unsigned ddi=0;ddi<2;++ddi)for(bool hdmi:{false,true})for(unsigned mode:{0U,1U})
 for(unsigned op=0;op<3;++op)for(unsigned fault=0;fault<5;++fault){
  Fixture f(0,0,ddi);Map map;auxSetup(f,map,ddi,hdmi,mode);auto &c=display13FBPower;
  if(image){nativePortObject=f.port;put(f.port,0,reinterpret_cast<uintptr_t>(portVT));put(image,0x138BA8,c.controller);
   state.orgDisplay13ReadAUX=reinterpret_cast<uintptr_t>(image+0x1CC72);
   state.orgDisplay13WriteAUX=reinterpret_cast<uintptr_t>(image+0x1CEB0);
   state.orgDisplay13I2CAUX=reinterpret_cast<uintptr_t>(image+0x1D2B6);}
  failGet=fault==1;failPut=fault==2;failCommand=fault==3;auxResult=fault==4?0xE00002BCU:0;
  uint32_t data[5]{};auto result=op==0?XE::wrapDisplay13ReadAUX(f.port,0x100,data,4):
   op==1?XE::wrapDisplay13WriteAUX(f.port,0x100,data,4):XE::wrapDisplay13I2CAUX(f.port,0,data,4,0);
  assert(result==(fault==4?auxResult:fault?kIOReturnNotReady:0));
  assert(!c.bridge.auxBusy() && (fault==4 && image ? auxCalls==7 : auxCalls==(fault==1?0U:1U)));
  if(!fault||fault==4){assert(!c.bridge.nativeAUXRefs(ddi)&&!c.bridge.state.refs[0]);}
  if(fault==1)assert(c.bridge.state.faulted&&!c.bridge.nativeAUXRefs(ddi));
  if(fault==2||fault==3)assert(c.bridge.state.faulted&&c.bridge.nativeAUXRefs(ddi)==1);
  nativePortObject=nullptr;++cases;
 }
 // All-ones power, retained main-link-only AUX, unbound objects and unknown
 // banks must not reach the transport, even when the firmware left AUX on.
 for(unsigned bad=0;bad<21;++bad){
  Fixture f(0,0,0);Map map;auxSetup(f,map,0,false,0);auto &c=display13FBPower;Display13FBPowerIO io{&map};
  assert(c.bridge.getAUX(io,0).status==Display13Power::Status::Complete);
  if(bad==0)state.display13FramebufferRequested=false;if(bad==1)state.display13FBAUXHooksReady=false;
  if(bad==2)state.xeProfile.display=XELP::Display::XeD;if(bad==3)state.adlnTopologyController=nullptr;
  if(bad==4)state.display13AUXControllerSlot=0;if(bad==5)bound=false;
  if(bad==6)c.bridge.state.faulted=true;if(bad==7)c.bridge.state.suspended=true;
  if(bad==8)put(f.ctl,0xD0,static_cast<void *>(f.port));if(bad==9)put(f.port,0x550,2U);
  if(bad==10)put(f.port,0x578,static_cast<void *>(f.port));if(bad==11)put(f.port,0x598,0x64114U);
  if(bad==12)put(f.port,0x120,1U);if(bad==13)map.regs[0x45444]=UINT32_MAX;
  if(bad==14)c.bridge.auxRefs[0]=0;if(bad==15)c.bridge.auxLease[0].owner=nullptr;
  if(bad==16)map.regs[0x45404]=0;if(bad==17)c.bridge.auxCommands[0]=UINT32_MAX;
  if(bad==18)put(f.ctl,0xB48,1U);if(bad==19)f.ctl[0x1BD8]=1;if(bad==20)put(f.ctl,0xC30,static_cast<void *>(nullptr));
  uint32_t packet[5]{};auto reads=map.reads.size();assert(XE::wrapDisplay13RunAUX(f.port,0,packet,4,false)==kIOReturnNotReady&&!auxCalls);
  if(bad<13)assert(map.reads.size()==reads);
 }
 // Two native callers waiting on the donor channel lock retain independent
 // command counts; the first completion cannot release the second caller.
 {Fixture f(0,0,0);Map map;auxSetup(f,map,0,false,0);auto &b=display13FBPower.bridge;Display13FBPowerIO io{&map};
  assert(b.getAUX(io,0).status==Display13Power::Status::Complete&&b.beginAUXCommand(io,0));
  assert(b.getAUX(io,0).status==Display13Power::Status::Complete&&b.beginAUXCommand(io,0));
  assert(b.auxCommands[0]==2&&b.endAUXCommand(io,0));
  assert(b.putAUX(io,0).status==Display13Power::Status::Complete&&b.auxBusy());
  assert(b.endAUXCommand(io,0)&&b.putAUX(io,0).status==Display13Power::Status::Complete);
  assert(!b.auxBusy()&&!b.state.refs[0]);}
 unsigned counterexamples=0;
 if(image){
  for(unsigned ddi=0;ddi<2;++ddi)for(unsigned op=0;op<3;++op)for(unsigned fault=1;fault<=2;++fault){
   Fixture f(0,0,ddi);Map map;auxSetup(f,map,ddi,false,0);auto &c=display13FBPower;
   nativePortObject=f.port;put(f.port,0,reinterpret_cast<uintptr_t>(portVT));put(image,0x138BA8,c.controller);
   portVT[0x170/8]=reinterpret_cast<uintptr_t>(unsafeTransport);failGet=fault==1;failPut=fault==2;
   uint32_t data[5]{};auto read=reinterpret_cast<decltype(&XE::wrapDisplay13ReadAUX)>(image+0x1CC72);
   auto write=reinterpret_cast<decltype(&XE::wrapDisplay13WriteAUX)>(image+0x1CEB0);
   auto i2c=reinterpret_cast<decltype(&XE::wrapDisplay13I2CAUX)>(image+0x1D2B6);
   const auto result=op==0?read(f.port,0x100,data,4):op==1?write(f.port,0x100,data,4):i2c(f.port,0,data,4,0);
   assert(!result&&auxCalls==1&&c.bridge.state.faulted);++counterexamples;nativePortObject=nullptr;
  }
  assert(counterexamples==12);munmap(image,0x400000);
  printf("PASS %u old native false-success counterexamples (failed AUX get/put)\n",counterexamples);
 }
 printf("PASS Display13 AUX: %u %s transactions; get/put/HAL fault propagation, native errors, no-DDI probe, active-command suspend/put guards, 21 rejected bindings\n",cases,image?"exact-donor":"synthetic");
}
