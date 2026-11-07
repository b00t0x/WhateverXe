#!/usr/bin/env python3
"""Replay frozen release topology assertions through current production callers.

The removed APIs had narrower return values: TC interrupt bits only, and whether
a false native HDMI lane result was corrected. Adapters project those meanings
from the current hotplug policy and actual common lane wrapper. They do not
reimplement topology validation or HDMI admission. No hardware is accessed.
"""
from pathlib import Path
import hashlib,json,re,subprocess,tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'WhateverXe/kern_xe.cpp').read_text()
header=(root/'WhateverXe/kern_xe.hpp').read_text()
fields=header[header.index(' TGLHDMIWiring tglHDMIWiring;'):header.index('\tbool tglGMBusReady')]
functions='\n'.join(re.search(pattern,source,re.S).group() for pattern in [
    r'bool XE::tglLoadHDMIWiring\(.*?\n\}',r'bool XE::wrapTGLGetOSInformation\(.*?\n\}',
    r'uint32_t XE::wrapTGLGMBus\(.*?\n\}',r'bool XE::wrapFramebufferComputeLanes\(.*?\n\}',
    r'bool XE::framebufferHDMIEligible\(.*?\n\}'])
observations=source[source.index('struct TGLGMBusObservation {'):source.index('struct TGLMEIRecord {')]
fixture=(root/'tests/tgl_hdmi_wiring_fixture.cpp').read_text()
prefix=fixture[:fixture.index('int main(){')].replace('// FIELDS',fields).replace('// OBSERVATIONS',observations).replace('// FUNCTIONS',functions)
adapter=r'''
namespace Display13FBTopology {
template<class Wiring> uint32_t excludedTypeC(const void *p,uint32_t bytes,const Wiring &w) {
    return excludedHotplug(p,bytes,w)&0x3f000000U;
}
template<class Wiring> bool skipDPLanes(const void *p,const void *c,const Wiring &w,
                                      bool exact,bool native,uint32_t available,uint32_t lanes) {
    setup();object.display13FramebufferRequested=true;
    object.adlnPlatform=const_cast<void *>(p);object.adlnOutputWiring=w;
    testCaller=exact?0x100:0x101;nativeLanesResult=native;
    memcpy(port+0x548,&c,sizeof(c));const auto before=lanes;
    const bool result=XE::wrapFramebufferComputeLanes(port,nullptr,24,available,&lanes);
    assert(nativeLanesCalls==1 && lanes==before);
    return result && !native;
}
}
'''
records=json.loads((root/'tests/fixtures/released_topology.json').read_text())
with tempfile.TemporaryDirectory(prefix='xe-released-topology-') as tmp:
    tmp=Path(tmp)
    for record in records:
        original=(root/'tests/fixtures'/record['file']).read_bytes()
        assert hashlib.sha256(original).hexdigest()==record['sha256']
        cpp=tmp/'test.cpp';exe=tmp/'test'
        cpp.write_text(prefix+adapter+original.decode())
        subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',
                        '-fno-omit-frame-pointer','-I'+str(root/'WhateverXe'),str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        print('PASS frozen topology',record['tag'],record['commit'],flush=True)
