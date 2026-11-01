#pragma once
#include <Headers/kern_patcher.hpp>
namespace ShutdownObserver {
void install(KernelPatcher &patcher);
using DisplaySnapshot = void (*)(void *controller, void *fb, uint32_t *words);
void installDisplay12(KernelPatcher &,size_t,mach_vm_address_t,size_t,DisplaySnapshot);
void framebuffer(bool returned,void *controller,void *fb,uint32_t result,DisplaySnapshot);
}
