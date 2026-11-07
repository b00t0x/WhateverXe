#pragma once
#include <stdint.h>
#include <stddef.h>

// Host IOGraphics/IOAccelerator are OS components, not a fixed Intel donor.
// Match instruction operands and call targets; never their image offsets.
namespace HostPowerContract {
using Decode = size_t (*)(const uint8_t *,size_t);
inline bool equal(const uint8_t *a,const uint8_t *b,size_t n) {
    for(size_t i=0;i<n;++i)if(a[i]!=b[i])return false;
    return true;
}
inline uint32_t word(const uint8_t *p) {
    return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
}
inline size_t extent(const uint8_t *p,size_t available,Decode decode) {
    for(size_t i=0;i<available;) {
        const auto n=decode(p+i,available-i);if(!n)return 0;
        if(n==1 && p[i]==0xC3)return i+1;
        i+=n;
    }
    return 0;
}
inline size_t occurrences(const uint8_t *p,size_t size,const uint8_t *pattern,size_t length,Decode decode) {
    size_t hits=0;
    for(size_t i=0;i<size;) {
        if(length<=size-i && equal(p+i,pattern,length))++hits;
        const auto n=decode(p+i,size-i);if(!n)return 0;
        i+=n;
    }
    return hits;
}
inline bool paging(const uint8_t *p,size_t size,Decode decode) {
    const uint8_t self[]={0x49,0x89,0xFD}; // r13 = IOFramebuffer this
    const uint8_t eventR12[]={0x41,0x89,0xF4}; // r12d = esi
    const uint8_t eventRBX[]={0x89,0xF3}; // ebx = esi
    const uint8_t eventR14[]={0x41,0x89,0xDE}; // r14d = ebx
    const uint8_t bit[]={0x41,0x80,0x8D,0x99,1,0,0,0x20};
    if(occurrences(p,size,self,sizeof(self),decode)!=1 ||
       occurrences(p,size,bit,sizeof(bit),decode)!=2)return false;
    const bool r12=occurrences(p,size,eventR12,sizeof(eventR12),decode)==1;
    const bool r14=occurrences(p,size,eventRBX,sizeof(eventRBX),decode)==1 &&
        occurrences(p,size,eventR14,sizeof(eventR14),decode)==1;
    if(r12==r14)return false;
    unsigned wake=0,on=0;
    for(size_t i=0;i<size;) {
        if(size-i>=10 && p[i]==0x41 && p[i+1]==0x83 && p[i+2]==(r12?0xFC:0xFE)) {
            // DidWake falls through the != branch; its paging write must be
            // in that case. DidPowerOn branches directly to the other write.
            if(p[i+3]==4 && p[i+4]==0x0F && p[i+5]==0x85) {
                const auto end=int64_t(i+10)+int32_t(word(p+i+6));
                if(end>int64_t(i+10) && end<=int64_t(size)) {
                    size_t stop=i+10;
                    while(stop<size_t(end) && p[stop]!=0xEB && p[stop]!=0xE9) {
                        const auto n=decode(p+stop,size_t(end)-stop);if(!n)return false;
                        stop+=n;
                    }
                    if(occurrences(p+i+10,stop-(i+10),bit,sizeof(bit),decode)==1)++wake;
                }
            }
            if(p[i+3]==8) {
                size_t branch=i+4;
                const uint8_t argument[]={0x48,0x8B,0x5D,0xD0};
                // The saved event argument may be loaded before the branch;
                // MOV leaves the comparison flags intact.
                if(size-branch>=sizeof(argument) && equal(p+branch,argument,sizeof(argument)))branch+=sizeof(argument);
                if(size-branch>=2 && p[branch]==0x74) {
                    const auto target=int64_t(branch+2)+int8_t(p[branch+1]);
                    if(target>=0 && target+sizeof(bit)<=size && equal(p+target,bit,sizeof(bit)))++on;
                }
            }
        }
        const auto n=decode(p+i,size-i);if(!n)return false;
        i+=n;
    }
    return wake==1 && on==1;
}
struct SleepSite {
    size_t offset {}, length {};
    unsigned owner {}, success {};
    bool valid {};
};
inline bool savedRegister(unsigned reg) { return reg==3 || (reg>=12 && reg<=15); }
// Decode mov rdi,[callee-saved owner+disp32]. R12 needs an explicit SIB.
inline size_t machineLoad(const uint8_t *p,size_t size,unsigned &owner) {
    if(size<7 || (p[0]!=0x48 && p[0]!=0x49) || p[1]!=0x8B || (p[2]&0xF8)!=0xB8)return 0;
    owner=(p[2]&7)+((p[0]&1)?8:0);
    if(!savedRegister(owner))return 0;
    const size_t disp=(owner&7)==4 ? 4 : 3;
    if(size<disp+4 || (disp==4 && p[3]!=0x24) || word(p+disp)!=0x378)return 0;
    return disp+4;
}
inline bool ownerFromThis(const uint8_t *p,size_t size,unsigned owner,Decode decode) {
    const uint8_t save[]={uint8_t(0x48|(owner>=8)),0x89,uint8_t(0xF8|(owner&7))};
    return occurrences(p,size,save,sizeof(save),decode)==1;
}
inline SleepSite sleepSite(const uint8_t *p,size_t size,uintptr_t address,uintptr_t count,Decode decode) {
    SleepSite result;unsigned hits=0,calls=0;
    for(size_t i=0;i<size;) {
        const auto n=decode(p+i,size-i);if(!n || n>size-i)return {};
        if(n==5 && p[i]==0xE8 && address+i+5+int32_t(word(p+i+1))==count)++calls;
        unsigned owner=0;
        const auto load=machineLoad(p+i,size-i,owner);
        const auto call=i+load;
        if(load && size-call>=5 && p[call]==0xE8 && address+call+5+int32_t(word(p+call+1))==count) {
            auto next=call+5;
            unsigned high=0;
            if(next<size && p[next]==0x41){high=8;++next;}
            // The success flag belongs to the native loop, not the owner.
            // Do not rewrite it, or assume a particular pairing of registers.
            if(size-next>=6 && (p[next]&0xF8)==0xB0 && p[next+1]==1) {
                const unsigned success=(p[next]&7)+high;
                next+=2;
                if(savedRegister(success) && success!=owner && p[next]==0x85 && p[next+1]==0xC0 && p[next+2]==0x74) {
                    const auto end=int64_t(next+4)+int8_t(p[next+3]);
                    if(end>int64_t(next+4) && end<=int64_t(size) && ownerFromThis(p,i,owner,decode)) {
                        unsigned bounds=0;
                        for(size_t j=next+4;j<size_t(end);) {
                            unsigned loopOwner=0;const auto len=machineLoad(p+j,size_t(end)-j,loopOwner);
                            if(len && loopOwner==owner && size_t(end)-j>=len+5 && p[j+len]==0xE8 &&
                               address+j+len+5+int32_t(word(p+j+len+1))==count)++bounds;
                            const auto k=decode(p+j,size_t(end)-j);if(!k || k>size_t(end)-j)return {};
                            j+=k;
                        }
                        if(bounds==1){result={i,load+5,owner,success,true};++hits;}
                    }
                }
            }
        }
        i+=n;
    }
    return hits==1 && calls==2 ? result : SleepSite{};
}
inline bool sleepFields(const uint8_t *sleep,size_t sleepSize,const uint8_t *mode,size_t modeSize,Decode decode,unsigned owner) {
    const uint8_t depthRBX[]={0x8B,0x83,0x0C,1,0,0,0x8D,0x48,1,0x89,0x8B,0x0C,1,0,0};
    const uint8_t depthR12[]={0x41,0x8B,0x84,0x24,0x0C,1,0,0,0x8D,0x48,1,0x41,0x89,0x8C,0x24,0x0C,1,0,0};
    return ownerFromThis(sleep,sleepSize,owner,decode) &&
        ((ownerFromThis(mode,modeSize,3,decode) && occurrences(mode,modeSize,depthRBX,sizeof(depthRBX),decode)==1) ||
         (ownerFromThis(mode,modeSize,12,decode) && occurrences(mode,modeSize,depthR12,sizeof(depthR12),decode)==1));
}
}
