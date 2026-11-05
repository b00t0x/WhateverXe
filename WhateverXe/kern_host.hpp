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
 bool surfaceSeen {},graphicsSeen {},accelSeen {};
 struct PendingDonor {size_t index;mach_vm_address_t address;size_t size;};
 PendingDonor pendingDonors[2] {};unsigned pendingDonorCount {};
 bool dependenciesSeen() const {return surfaceSeen && (!systemWakeRecovery || (graphicsSeen && accelSeen));}
 void flushDonors(KernelPatcher &);
 mach_vm_address_t sleepFBCount {},sleepFBAt {};
 uint32_t repeatedSleep {};
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
