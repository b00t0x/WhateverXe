// Copyright © 2018 vit9696. Derived from WhateverGreen (BSD-3-Clause).
#pragma once
#include "kern_xe.hpp"
#include <IOKit/graphics/IOFramebuffer.h>
class XeHost {
public:
 void init();
private:
 static XeHost *callbackHost;
 XE xe;
 bool active {},systemWakeRecovery {};
 bool surfaceSeen {},graphicsSeen {},accelSeen {},providerSeen {};
 bool dependenciesSeen(size_t index) const {return surfaceSeen &&
  (!xe.isIntelFramebuffer(index) || !xe.requiresIntelProvider() || providerSeen) &&
  (!systemWakeRecovery || (graphicsSeen && accelSeen));}
 void processLoadedDependency(KernelPatcher &,KernelPatcher::KextInfo &);
 mach_vm_address_t sleepFBCount {};
 uint32_t repeatedSleep {},sleepNotifications {},wakeNotifications {};
 IOSimpleLock *sleepScopeLock {};
 struct SleepEntry {IOService *fb {};bool pending {};};
 SleepEntry sleepEntries[3] {};
 struct SleepScope {
  SleepScope *next {};uintptr_t thread {};IOService *fb {};
  bool reached {},skipped {};
 };
 SleepScope *sleepScopes {};
 mach_vm_address_t orgDisplayChange {};
 static IOReturn wrapDisplayChange(void *,void *,IOFramebuffer *,int,void *);
 static uint32_t pendingSleepDisplays(void *);
 mach_vm_address_t orgWSAAAttribute {};
 static IOReturn wrapWSAAAttribute(IOFramebuffer *,uint32_t,uint32_t);
 bool backlightEnabled {};
 IOPCIDevice *backlightGPU {};
 mach_vm_address_t orgPanelSetDisplay {};
 static bool wrapPanelSetDisplay(IOService *,IOService *);
 void processKernel(KernelPatcher &);
 void processKext(KernelPatcher &,size_t,mach_vm_address_t,size_t);
 enum { FB_DETECT, FB_RESET, FB_COPY, FB_ZEROFILL, FB_NONE, FB_TOTAL };
 uint32_t resetFramebuffer {FB_DETECT};
	struct vc_info {
		unsigned int   v_height;        /* pixels */
		unsigned int   v_width;         /* pixels */
		unsigned int   v_depth;
		unsigned int   v_rowbytes;
		unsigned long  v_baseaddr;
		unsigned int   v_type;
		char           v_name[32];
		uint64_t       v_physaddr;
		unsigned int   v_rows;          /* characters */
		unsigned int   v_columns;       /* characters */
		unsigned int   v_rowscanbytes;  /* Actualy number of bytes used for display per row*/
		unsigned int   v_scale;
		unsigned int   v_rotate;
		unsigned int   v_reserved[3];
	};

 vc_info consoleVinfo {};
 uint8_t *consoleBuffer {}, *gIOFBVerboseBootPtr {};
 mach_vm_address_t orgFramebufferInit {};
 bool gotConsoleVinfo {};
 static void wrapFramebufferInit(IOFramebuffer *);
};
