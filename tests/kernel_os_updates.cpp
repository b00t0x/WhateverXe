#include "kern_xelp_os.hpp"
#include <cassert>
#include <cstdio>

int main() {
    using namespace XELP;
    assert(!hostOS(OS::Unknown) && !hostOS(OS::Donor));
    for (unsigned os=unsigned(OS::BigSur);os<=unsigned(OS::Tahoe);++os)
        assert(hostOS(static_cast<OS>(os)));
    assert(!hostOS(static_cast<OS>(unsigned(OS::Tahoe)+1)));
    assert(kernelOS(nullptr)==OS::Unknown);
    assert(kernelOS("")==OS::Unknown);
    assert(kernelOS("Darwin Kernel Version 24.6.1: update/RELEASE_X86_64")==OS::Sequoia);
    assert(kernelOS("Darwin Kernel Version 25.6.1: update/RELEASE_X86_64")==OS::Tahoe);
    assert(kernelOS("Darwin Kernel Version 25.7.0: update/RELEASE_X86_64")==OS::Tahoe);
    // Metamorphic family acceptance: vary servicing minor/patch and arbitrary
    // build metadata, without adding known Apple builds to an allowlist.
    for(unsigned major=20;major<=25;++major) for(unsigned minor=6;minor<(major<25 ? 7U : 9U);++minor)
        for(unsigned patch=0;patch<100;patch+=7) {
            char version[160];
            snprintf(version,sizeof(version),"Darwin Kernel Version %u.%u.%u: synthetic-%u/RELEASE_X86_64",major,minor,patch,minor*109+patch);
            assert(kernelOS(version)==static_cast<OS>(unsigned(OS::BigSur)+major-20));
        }
    const char *rejected[] {
        "Darwin Kernel Version 23.5.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 24.5.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 24.7.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.5.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 26.0.0: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.6.0: update/DEBUG_X86_64",
        "Darwin Kernel Version 25.6.0: update/RELEASE_ARM64",
        "Darwin Kernel Version 25.6.0: update/RELEASE_X86_64.extra",
        "Darwin Kernel Version 25.6.0:",
        "Darwin Kernel Version 25.6.0: ",
        "Darwin Kernel Version 25.6: update/RELEASE_X86_64",
        "Darwin Kernel Version 25.6.9999999999999: update/RELEASE_X86_64",
    };
    for (auto version:rejected) assert(kernelOS(version)==OS::Unknown);
}
