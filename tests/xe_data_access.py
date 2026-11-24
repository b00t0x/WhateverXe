#!/usr/bin/env python3
"""Exercise the production access decision with mocked kernel signing queries."""
import pathlib, re, subprocess, sys, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
source = (root/'WhateverXe/kern_xe_data_access.cpp').read_text()
functions = source[source.index('bool knownOS()'):source.index('int checkOpen(')]
harness = r'''
#include "kern_xe_data_policy.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
namespace KernelVersion { enum { Catalina=19, BigSur, Monterey, Ventura, Sonoma, Sequoia, Tahoe }; }
int version=20; int getKernelVersion() { return version; }
bool ready=true, processPresent=true, platform=true, valid=true, pathError=false;
const char *identity="com.apple.WebKit.GPU";
const char *target="/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/P";
const char *imagePath="/arbitrary/updated/Safari/WebKitGPU";
int imageReads=0, releases=0;
void *currentProcess() { return processPresent ? (void*)2 : nullptr; }
void *executableVnode(void*) { ++imageReads; return (void*)3; }
int getPath(void *node,char *path,int*) { if(pathError)return 1; strcpy(path,node==(void*)1?target:imagePath);return 0; }
int putVnode(void*) { ++releases; return 0; }
const char *getIdentity(void*) { return identity; }
int getPlatform(void*) { return platform; }
int getValid(void*) { return valid; }
const char *(*signingIdentity)(void*)=getIdentity;
int (*platformBinary)(void*)=getPlatform;
int (*signatureValid)(void*)=getValid;
'''+functions+r'''
int main(int argc, char **argv) {
    for (int i=1; i<argc; ++i) {
        target=argv[i];
        assert(allowed((void*)1,true));
        assert(!allowed((void*)1,false));
    }
    target="/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/P";
    for(version=19;version<=26;++version) {
        assert(knownOS()==(version>=20&&version<=25));
        if(!knownOS())continue;
        for(const char *location : {"/System/Library/Frameworks/WebKit.framework/service",
            "/System/Volumes/Preboot/Cryptexes/App/staged/service", "/Applications/Safari Copy.app/service", "/tmp/service"}) {
            imagePath=location;
            assert(allowed((void*)1,true));
            assert(imageReads==0); // WebKit's executable path is never queried.
        }
        platform=false; assert(!allowed((void*)1,true)); platform=true;
        valid=false; assert(!allowed((void*)1,true)); valid=true;
        identity="com.apple.WebKit.WebContent"; assert(!allowed((void*)1,true));
        identity=nullptr; assert(!allowed((void*)1,true)); identity="com.apple.WebKit.GPU";
        signingIdentity=nullptr; assert(!allowed((void*)1,true)); signingIdentity=getIdentity;
        platformBinary=nullptr; assert(!allowed((void*)1,true)); platformBinary=getPlatform;
        signatureValid=nullptr; assert(!allowed((void*)1,true)); signatureValid=getValid;
        assert(!allowed((void*)1,false));
    }
    ready=false; assert(!allowed((void*)1,true)); ready=true;
    assert(!allowed(nullptr,true));
    processPresent=false; assert(!allowed((void*)1,true)); processPresent=true;
    pathError=true; assert(!allowed((void*)1,true)); pathError=false;
    target="/Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS/unlisted";
    assert(!allowed((void*)1,true));
    target="/Library/GPUBundles/XeCompilerAdapter.bundle/Contents/MacOS/adapter.dylib";
    imagePath="/System/Library/Frameworks/Metal.framework/Versions/A/XPCServices/MTLCompilerService.xpc/Contents/MacOS/MTLCompilerService";
    for(version=20;version<=25;++version) assert(allowed((void*)1,true)==(version>=23));
    imagePath="/tmp/MTLCompilerService"; assert(!allowed((void*)1,true));
    assert(imageReads==releases);
    puts("PASS: production access decision, six OS gates, relocation, trust failures, compiler boundary");
}
'''
def va_dependencies(expanded):
    bundle = pathlib.Path('Library/GPUBundles/AppleIntelTGLGraphicsVADriver.bundle/Contents/MacOS')
    for family in ('BigSur', 'Monterey', 'Ventura', 'Sonoma', 'Sequoia', 'Tahoe'):
        payload = expanded / (family+'.pkg') / 'Payload'
        pending = [bundle / 'AppleIntelTGLGraphicsVADriver']
        visited = set()
        while pending:
            image = pending.pop()
            if image in visited:
                continue
            visited.add(image)
            output = subprocess.check_output(['otool','-arch','x86_64','-l',payload/image], text=True)
            for command in re.split(r'^Load command \d+$',output,flags=re.M):
                if not re.search(r'^\s*cmd LC_(LOAD|REEXPORT|LOAD_UPWARD)_DYLIB$',command,re.M):
                    continue
                name = re.search(r'^\s*name (.+) \(offset \d+\)$',command,re.M).group(1)
                if name.startswith('@loader_path/'):
                    pending.append(image.parent / name.removeprefix('@loader_path/'))
                elif name.startswith('/Library/GPUBundles/'):
                    pending.append(pathlib.Path(name[1:]))
                elif not name.startswith(('/System/Library/', '/usr/lib/')):
                    raise ValueError(f'{family}: unresolved dependency {name}')
        print(f'{family}: '+', '.join(sorted(p.name for p in visited)),flush=True)
        yield from ('/'+str(p) for p in sorted(visited))

harness = harness.replace('#include <cstring>', '#include <cstring>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as temp:
    cpp=pathlib.Path(temp)/'test.cpp'; exe=pathlib.Path(temp)/'test'
    cpp.write_text(harness)
    subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-I'+str(root/'WhateverXe'),str(cpp),'-o',str(exe)],check=True)
    paths = list(va_dependencies(pathlib.Path(sys.argv[1]))) if len(sys.argv)>1 else []
    subprocess.run([str(exe),*paths],check=True)
