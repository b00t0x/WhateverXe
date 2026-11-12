// Copyright (c) 2026 b00t0x. BSD-3-Clause.
#pragma once
#include <Headers/kern_patcher.hpp>
namespace XeDataAccess {
void init();
KernelPatcher::KextInfo &dependency();
void processKernel(KernelPatcher &patcher);
bool processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);
}
