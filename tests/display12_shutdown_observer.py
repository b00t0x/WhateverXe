#!/usr/bin/env python3
from pathlib import Path
import re,subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'WhateverXe/kern_xe.cpp').read_text();o=(r/'WhateverXe/kern_shutdown_observer.cpp').read_text()
snapshot=re.search(r'void XE::snapshotDisplay12Shutdown\(.*?\n\}',s,re.S)[0]
wrapper=re.search(r'static uint32_t wrapDisplay12Shutdown\(.*?\n\}',o,re.S)[0]
code=r'''
#include <cassert>
#include <cstring>
#include "kern_shutdown_tc_observation.hpp"
namespace ShutdownTCObservation {State storage;}
#include "kern_tgl_port_map.hpp"
struct Lock{bool busy=false;};bool IOLockTryLock(Lock*l){if(l->busy)return false;l->busy=true;return true;}void IOLockUnlock(Lock*l){assert(l->busy);l->busy=false;}
struct TGLTCNativeBinding{template<class T>static T field(void*p,unsigned off){T v;memcpy(&v,(char*)p+off,sizeof(v));return v;}};
struct {void*controller;Lock*lock;struct{unsigned phase=1,total=1;bool bootstrapHeld=true;}cold;}tglTCBootstrap;
struct Functions{bool valid=true;void**controllerSlot;void*(*portByDDI)(unsigned);bool complete(){return valid;}};
struct XE{Functions tglTCNativeFunctions;void*tglObservationController;unsigned tglAUXParentRefs[9]{};static void snapshotDisplay12Shutdown(void*,void*,uint32_t*);};XE*callbackXE;
SNAPSHOT
using Snapshot=void(*)(void*,void*,uint32_t*);Snapshot display12Snapshot=XE::snapshotDisplay12Shutdown;
unsigned events[2]{},calls=0,nativeCalls=0;uint32_t result=0;
void framebuffer(bool done,void*c,void*f,uint32_t ret,Snapshot fn){events[calls++]=done?ret:0;uint32_t data[48]{};fn(c,f,data);}
uint32_t native(void*,void*){++nativeCalls;return result;}auto orgDisplay12Shutdown=native;
WRAPPER
char ports[4][0x560]{};void*get(unsigned d){return d>=3&&d<7?ports[d-3]:nullptr;}
template<class T>void put(void*p,unsigned off,T v){memcpy((char*)p+off,&v,sizeof(v));}
int main(){
 using namespace ShutdownTCObservation;record(0x163894,3,false);mode(3,0,0);assert(sample(0x163894,false)==0&&state().mode0[0]==0);
 state().enabled=true;record(0x1234,3,false);assert(state().sequence==0);
 record(0x1638a0,0x10,false);record(0x163894,1,false);record(0x163894,0,true);
 mode(3,0,0x1234);mode(7,0,0);assert(state().mode0[0]==1);
 char c[0xc50]{},power[0x90]{},fb[0x200]{};void*cp=c;put(c,0xc8,(void*)power);put(power,0x10,cp);put(fb,0x1dc,1U);
 for(unsigned i=0;i<4;++i){put(ports[i],0x550,3U);put(power,0x54+4*(i+3),i+1);}
 XE x;callbackXE=&x;x.tglObservationController=c;x.tglTCNativeFunctions={true,&cp,get};Lock lock;tglTCBootstrap.controller=c;tglTCBootstrap.lock=&lock;
 uint32_t out[48]{};XE::snapshotDisplay12Shutdown(c,fb,out);assert(out[1]==3&&out[2]==1&&out[4]==3&&out[6]==1&&out[7]==1);
 assert(out[8]==3&&out[9]==1&&out[11]==1&&out[12]==1&&out[13]==0&&out[14]==2&&out[15]==3&&out[16]==1&&out[17]==0x1234);
 // No hardware access exists in the sampler; a busy cold lock is not waited on.
 lock.busy=true;memset(out,0,sizeof(out));XE::snapshotDisplay12Shutdown(nullptr,nullptr,out);assert(out[4]==1&&out[5]==UINT32_MAX&&lock.busy);
 for(unsigned n=0;n<2;++n){calls=nativeCalls=0;result=n?0xdead:0;assert(wrapDisplay12Shutdown(c,fb)==result&&nativeCalls==1&&calls==2&&events[1]==result);}
 cp=nullptr;memset(out,0,sizeof(out));XE::snapshotDisplay12Shutdown(c,fb,out);assert(out[4]==0);
}
'''.replace('SNAPSHOT',snapshot).replace('WRAPPER',wrapper)
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++14','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(d/'test.cpp'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
# Match the kernel's separate-translation-unit compilation model. An inline
# local-static singleton is TU-local with -fapple-kext, unlike the userland test.
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp)
 (d/'writer.cpp').write_text('#include "kern_shutdown_tc_observation.hpp"\nnamespace ShutdownTCObservation {State storage;}\nvoid enable(){ShutdownTCObservation::state().enabled=true;}')
 (d/'reader.cpp').write_text('#include "kern_shutdown_tc_observation.hpp"\nvoid observe(){ShutdownTCObservation::record(0x163894,1,false);}')
 (d/'main.cpp').write_text('#include <cassert>\n#include "kern_shutdown_tc_observation.hpp"\nvoid enable();void observe();extern "C" int main(){enable();observe();assert(ShutdownTCObservation::sample(0x163894,false)==0x100000001ULL);return 0;}')
 for name in ['writer','reader','main']:
  subprocess.run(['clang++','-std=c++17','-O2','-fapple-kext','-fno-exceptions','-fno-rtti','-I'+str(r/'WhateverXe'),'-c',str(d/(name+'.cpp')),'-o',str(d/(name+'.o'))],check=True)
 subprocess.run(['clang++',*[str(d/(n+'.o')) for n in ['writer','reader','main']],'-o',str(d/'cross-tu')],check=True)
 subprocess.run([str(d/'cross-tu')],check=True)
assert 'ready[2]!=63' in o and 'record(72,0,"display preflight"' in o
assert 'finishDisplay13TCShutdown' not in wrapper and 'record(' not in snapshot
print('PASS Display12 passive cache, memory-only sampler, busy-lock bypass, original shutdown result/call count')
