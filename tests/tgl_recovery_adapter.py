"""Compile the production recovery wrapper and reset poll bridge for RAM replay."""
import ctypes as C
from pathlib import Path
import subprocess


def build(directory,headers=None):
    root=Path(__file__).resolve().parents[1]
    source=(root/'WhateverXe/kern_xe.cpp').read_text()
    a=source.index('void XE::wrapTGLRecovery(void *channel) {')
    b=source.index('\nextern "C" {\n\tuintptr_t tglCCSPSResume',a)
    bodies=source[a:b]
    a=source.index('\t\t\t\t\tstatic const uint8_t poll[]')
    b=source.index('\n\t\t\t\t}\n\t\t\t\tIORegistryEntry',a)
    installer=source[a:b].replace('reinterpret_cast<int64_t>(tglResetPollBridge)',
                                'static_cast<int64_t>(testBridge)')
    code=r'''
#include <cstdint>
#include <cstring>
#include <climits>
#include "kern_tgl_recovery.hpp"
#include "kern_tgl_reset_poll.hpp"
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
static uint64_t (*nowFn)();static void (*delayFn)();
static void clock_get_uptime(uint64_t *p){*p=nowFn();}
static void absolutetime_to_nanoseconds(uint64_t n,uint64_t *p){*p=n;}
static void IODelay(unsigned){delayFn();}
using mach_vm_address_t=uintptr_t;
struct KernelPatcher;
struct XE{
 static void wrapTGLRecovery(void *);
};
static uint32_t tglRecoveryCalls{},tglRecoveryReturns{};
static uint64_t tglRecoveryRecords[16][16]{};
static TGLRecovery::StampOps tglRecoveryStamp{};
BODIES
struct KernelPatcher {
 enum class Error{NoError};
 void routeBlock(uintptr_t site,const uint8_t *bytes,size_t n){memcpy(reinterpret_cast<void *>(site),bytes,n);}
 Error getError(){return Error::NoError;}void clearError(){}
};
extern "C" bool install(uintptr_t address,uintptr_t testBridge){
 bool recoveryOK=true;KernelPatcher patcher;
 INSTALLER
 return recoveryOK;
}
extern "C" void configure(uint64_t (*n)(),void (*d)()){nowFn=n;delayFn=d;}
extern "C" void stamps(TGLRecovery::SubmitStamp s,TGLRecovery::WaitStamp w){tglRecoveryStamp={s,w};}
extern "C" void recover(void *p){XE::wrapTGLRecovery(p);}
'''.replace('BODIES',bodies).replace('\n INSTALLER\n','\n'+installer+'\n')
    for name,contents in (headers or {}).items():
        (directory/name).write_text(contents)
    (directory/'adapter.cpp').write_text(code)
    subprocess.run(['clang++','-arch','x86_64','-std=c++17','-dynamiclib','-O1',
                    '-I'+str(root/'WhateverXe'),str(directory/'adapter.cpp'),
                    '-o',str(directory/'adapter.dylib')],check=True)
    library=C.CDLL(str(directory/'adapter.dylib'))
    library.install.argtypes=[C.c_size_t,C.c_size_t]
    library.install.restype=C.c_bool
    library.configure.argtypes=[C.c_void_p,C.c_void_p]
    library.stamps.argtypes=[C.c_void_p,C.c_void_p]
    return library
