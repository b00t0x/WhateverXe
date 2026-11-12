// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <IOKit/IORegistryEntry.h>
#include <libkern/c++/OSData.h>

// Runtime contract: EFI plugins are absent from the pristine Boot/System KCs
// used to build Graphics' AuxKC. Do not add a static WEX import/dependency.
// Only Lilu's accepted pluginStart publishes this token; disabled plugins do
// not. A new incompatible activation contract must use a different ABI value.
namespace WEXGraphicsActivation {
static constexpr char Property[]="WhateverXeGraphicsABI";
static constexpr uint32_t ABI=1;
inline void publish() {
    uint32_t abi=ABI;
    IORegistryEntry::getRegistryRoot()->setProperty(Property,&abi,sizeof(abi));
}
inline bool ready() {
    auto data=OSDynamicCast(OSData,IORegistryEntry::getRegistryRoot()->getProperty(Property));
    if(!data || data->getLength()!=sizeof(uint32_t) || !data->getBytesNoCopy())return false;
    uint32_t abi=0;memcpy(&abi,data->getBytesNoCopy(),sizeof(abi));
    return abi==ABI;
}
}
