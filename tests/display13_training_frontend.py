#!/usr/bin/env python3
"""Production Display13 temporary binding, framing and deferred clock frontend.

Uses real native object contracts and the power bridge with mock MMIO; no claim
about physical output or completeness follows from these regression cases.
"""
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text();h=(r/'WhateverXe/kern_xe.hpp').read_text()
def fn(sig):return re.search(re.escape(sig)+r'.*?\n\}',s,re.S).group()
functions='\n'.join(fn(x) for x in ['uint8_t *XE::display13SSTPort(', 'bool XE::display13HDMIPort(', 'bool XE::display13MainLinkAUX(', 'uint32_t XE::wrapDisplay13EnablePort(', 'uint32_t XE::wrapDisplay13DisablePort(', 'uint32_t XE::wrapDisplay13SignalLevels(', 'uint32_t XE::wrapDisplay13TrainingPattern(', 'bool XE::display13TrainingPath(', 'uint32_t XE::wrapDisplay13PortTraining(', 'bool XE::display13DeferClock(', 'bool XE::display13StopTranscoder('])
binding=fn('static TGLFBBinding::Readout adlnInspectPathBinding(')
native=(r/'tests/tgl_fb_binding.cpp').read_text().split('int main()')[0].replace('port[0x580]','port[0x700]')
code=(r/'tests/display13_sst_fixture.cpp').read_text().split('int main(')[0]
code=code.replace('// NATIVE_OBJECTS',native).replace('// BINDING',binding)
code=code.replace('struct Context {','struct Context { bool bootPanelDSCCheckPending=false;')
lease=re.search(r'\tstruct TGLTrainingLease \{.*?\n\t\};',h,re.S).group()
code=code.replace('struct XE {','''struct XE {
 LEASE
 TGLTrainingLease tglTrainingLeases[9];
 bool display13TrainingReady=false,display13FramingReady=false;
 unsigned trainingLeaseDP13Mask=0;IORecursiveLock *tglPathPoolLock=nullptr;
 uintptr_t orgDisplay13PortTraining=0,orgTGLPathLookup=0,tglPortByDDI=0,display13DPImage=0;
 static uint32_t wrapDisplay13PortTraining(void*,void*);
 static bool display13BootPanelDSCOff(void*,unsigned){assert(false);return false;}
 static bool display13DeferClock(void*,uint64_t,uint32_t,uintptr_t);
'''.replace('LEASE',lease))
code=code.replace('static bool display13StopTranscoder(void*){assert(false);return false;}','static bool display13StopTranscoder(void*);')
code=code.replace('static bool display13TrainingPath(void*,uint32_t&,uint32_t&){return false;}','static bool display13TrainingPath(void*,uint32_t&,uint32_t&);')
code=code.replace('// FUNCTIONS',re.search(r'struct Display13DeferredClock .*?static Display13DeferredClock display13DeferredClocks\[3\];',s,re.S).group()+'\n'+functions)
prefix='''#include <mutex>
#include <cassert>
#include <cstdint>
using IORecursiveLock=std::recursive_mutex;
void IORecursiveLockLock(IORecursiveLock*p){p->lock();}void IORecursiveLockUnlock(IORecursiveLock*p){p->unlock();}
void *current_thread(){return (void*)1;}unsigned delayUs=0;void IODelay(unsigned n){delayUs+=n;}
void adlnFault(bool &v,unsigned){v=true;}
'''
extra=r'''
void *testPort;void *testLookup(void*p){return p==testPort?currentPath:nullptr;}
void *testByDDI(unsigned ddi){return Display13FBMemory::field<unsigned>(testPort,0x120)==ddi?testPort:nullptr;}
unsigned nativeTrain(void*p,void*){assert(p==testPort&&!locked);return XE::wrapDisplay13TrainingPattern(currentPath,1);}
int main(){IORecursiveLock pool;
 for(unsigned ddi:{0U,1U,3U,4U,5U,6U})for(unsigned pipe=0;pipe<3;++pipe)for(unsigned cap=0;cap<2;++cap)for(unsigned wanted=0;wanted<2;++wanted){
  Fixture f(2-pipe,pipe,ddi);Map map;setup(f,map,ddi,pipe,4,6,ddi<2);testPort=f.port;
  state.display13TrainingReady=state.display13FramingReady=state.display13ClockOrderReady=true;
  state.trainingLeaseDP13Mask=0x7b;state.tglPathPoolLock=&pool;state.display13DPImage=0x100000;
  state.orgTGLPathLookup=(uintptr_t)testLookup;state.tglPortByDDI=(uintptr_t)testByDDI;state.orgDisplay13PortTraining=(uintptr_t)nativeTrain;
  put(f.path,0x32c8,static_cast<void*>(nullptr));put(f.path,0x3644,1U);f.port[0x115]=1;f.port[0x10]=cap;f.port[0x11a]=wanted;
  auto &lease=state.tglTrainingLeases[ddi];lease.path=f.path;lease.port=f.port;lease.fb=f.fb;lease.power=display13FBPower.power;
  lease.busy=lease.powerOwned=lease.prepared=true;lease.trainingThread=current_thread();lease.ddi=ddi;lease.pipe=pipe;
  unsigned d=99,p=99;assert(XE::display13TrainingPath(f.path,d,p)&&d==ddi&&p==pipe);
  lease.trainingThread=nullptr;assert(!XE::display13TrainingPath(f.path,d,p));lease.trainingThread=current_thread();
  lease.adopted=true;assert(!XE::display13TrainingPath(f.path,d,p));lease.adopted=false;
  put(f.fb,0x49e8,1U);assert(!XE::display13TrainingPath(f.path,d,p));put(f.fb,0x49e8,0U);
  if(ddi<2)put(f.port,0x550,0U); // native constructor, before setPortMode
  assert(!XE::wrapDisplay13PortTraining(f.port,nullptr));assert(f.port[0x11a]==(cap&wanted));
  assert((map.regs[0x60540+0x1000*pipe]&0x40000U)==((cap&wanted)<<18));
  const auto clock=0x46140+4*pipe;assert(map.regs[clock]==((ddi+(ddi>=3?3:1))<<28));
  assert(!XE::display13DeferClock(f.access,clock,0,state.display13DPImage+0x4d991));
  assert(XE::display13DeferClock(f.access,clock,0,state.display13DPImage+0x4d990));assert(map.regs[clock]==((ddi+(ddi>=3?3:1))<<28));
  assert(!display13FBPower.clockFaulted);
  assert(XE::display13DeferClock(f.access,clock,0,state.display13DPImage+0x4d990));
  assert(!display13FBPower.clockFaulted && map.regs[clock]==((ddi+(ddi>=3?3:1))<<28));
  // Conflicting values stay suppressed and never replace the pending request.
  assert(XE::display13DeferClock(f.access,clock,1,state.display13DPImage+0x4d990));
  assert(display13FBPower.clockFaulted && display13DeferredClocks[pipe].value==0);
  display13FBPower.clockFaulted=false;
  map.regs[0x70008+0x1000*pipe]=0;map.regs[0x64000+0x100*ddi]=0x80000000;
  assert(!XE::display13StopTranscoder(f.port)&&map.regs[clock]==((ddi+(ddi>=3?3:1))<<28));
  map.regs[0x64000+0x100*ddi]=0x80;map.regs[0x60540+0x1000*pipe]&=~0x80000000U;
  assert(XE::display13StopTranscoder(f.port)&&map.regs[clock]==0&&!display13DeferredClocks[pipe].pending);
 }
 puts("PASS production Display13 pre-plane reservation binding, sink framing and DDI-before-transcoder-clock stop; mock MMIO only");
}
'''
with tempfile.TemporaryDirectory(prefix='display13-training-front-') as d:
 p=Path(d);(p/'test.cpp').write_text(prefix+code+extra)
 subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

 # Restoring the old non-idempotent queue must fail at the no-fault assertion.
 before='(!deferred.pending || (deferred.port==port && deferred.value==value))'
 assert before in code
 with tempfile.TemporaryDirectory(prefix='display13-defer-fault-') as d:
  p=Path(d);(p/'fault.cpp').write_text(prefix+code.replace(before,'!deferred.pending')+extra)
  subprocess.run(['clang++','-std=c++17','-O1','-I'+str(r/'WhateverXe'),str(p/'fault.cpp'),'-o',str(p/'fault')],check=True)
  result=subprocess.run([str(p/'fault')],capture_output=True,text=True)
  assert result.returncode!=0 and 'Assertion failed:' in result.stderr
  print('PASS rejected semantic fault: repeated deferred stop faults')
