#include <IOKit/graphics/IOGraphicsTypes.h>
#include "kern_tgl_edp_handoff.hpp"
#include <cassert>
#include <fstream>
#include <map>
#include <vector>
#include <sys/mman.h>

static const char *bootArgument = "";
static bool checkKernelArgument(const char *name) { return !strcmp(name,bootArgument); }

// PRODUCTION_STATE
using Compare = bool(*)(void*,void*,void*,void*,const void*,void*);
struct IORegistryEntry {
    static IORegistryEntry *getRegistryRoot() {static IORegistryEntry root; return &root;}
    void setProperty(const char*,void*,size_t) {}
};
static unsigned fault;
struct XE {
    void *tglObservationController;
    Compare orgTGLRetainedMode;
    bool orgTGLReadAUX=true;
    uint32_t (*orgTGLRegisterRead)(void*,uint32_t);
    static unsigned tglPanelDepthPort(void*) { return 0; }
    static unsigned wrapTGLReadAUX(void*,unsigned,void*,unsigned);
    // This fixture exercises boot adoption's publication boundary. The
    // retained-depth suite exercises the actual register transaction.
    static bool prepareDisplay12RGBMode(void*,void*,uint32_t*,bool) {return fault==13;}
    static bool selectTGLBootDepth(bool,void*,void*,void*,void*,const void*,void*);
} self,*callbackXE=&self;
#define FunctionCast(fn,p) (p)
// PRODUCTION_DECISION

static std::map<uint32_t,uint32_t> registers;
static uint32_t reg(void*,uint32_t r) {return registers.at(r);}
unsigned XE::wrapTGLReadAUX(void*,unsigned r,void *data,unsigned len) {
    auto out=static_cast<uint8_t*>(data); memset(out,0,len);
    switch(r) {
        case 0: out[0]=0x14;break;
        case 0x60: out[0]=1;break;
        case 0x160: out[0]=fault==2;break;
        case 0x100: out[0]=20;out[1]=0x84;break;
        case 0x202: out[0]=0x77;out[1]=fault==1?0x07:0x77;out[2]=1;break;
        case 0x10a: break;
        default: assert(false);
    }
    return 0;
}
static unsigned noMode(void*,int,void*,void*) {return 1;}
static bool noSDP(void*,void*,const void*) {return false;}
template<class T> void put(void *ptr,unsigned offset,T value) {
    memcpy(static_cast<char*>(ptr)+offset,&value,sizeof(value));
}
void redirect(uint8_t *p,uintptr_t dest) {
    p[0]=0x48;p[1]=0xb8;put(p,2,dest);p[10]=0xff;p[11]=0xe0;
}
int main(int argc,char **argv) {
    uint8_t *donor=nullptr;
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
        donor=static_cast<uint8_t*>(mmap(nullptr,0x400000,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_ANON|MAP_PRIVATE,-1,0));
        assert(donor!=MAP_FAILED && bytes.size()<0x400000);memcpy(donor,bytes.data(),bytes.size());
        redirect(donor+0x30de2,reinterpret_cast<uintptr_t>(noMode));
        redirect(donor+0x4eefc,reinterpret_cast<uintptr_t>(noSDP));
    }
    for(fault=0;fault<14;++fault) {
        // Actual TL156MDMP01 DDI and PIPE values. Rate, timings, precision
        // bounds and lane status are compatible synthetic inputs.
        registers={{0x45404,5},{0x70008,0xc0000000},{0x60400,0x8a230006},{0x60404,0},
            {0x64000,0x80000006},{0x46140,0x10000000},{0x60540,0x80040300},
            {0x78000,0},{0x78004,0},{0x164280,0},{0x46010,0xcc000000},{0x51004,0},
            {0x164284,337|(16384<<10)},{0x164288,(2<<2)|(1<<6)},
            {0x6001c,3199U<<16|1999U},{0x60030,27},{0x60034,40},
            {0x60040,6},{0x60044,5},{0x70030,0x01000050},{0x60410,1}};
        for(unsigned i=0;i<6;++i)registers[0x60000+4*i]=0x10203040+i;
        auto &boot=tglBootPanels[0];boot={};
        // Capture under the same boot policy as the framebuffer entry.
        bootArgument="-wex-tgl-edp-keep";
        captureTGLBootPanels([](auto r){return reg(nullptr,r);});
        assert(boot.phase==Available);
        uint32_t plan[60]{};
        alignas(8) uint8_t ctl[0x1000]{},fb[0x4400]{},path[0x3660]{},port[0x600]{},cfg[16]{};
        put(fb,0x1d0,ctl+0);put(ctl,0xc40,ctl+0);put(path,0x3644,2U);
        put(path,0x3648,port+0);put(port,0x548,cfg+0);
        self.tglObservationController=ctl;self.orgTGLRegisterRead=reg;
        self.orgTGLRetainedMode=donor ? reinterpret_cast<Compare>(donor+0x4f044) :
            +[](void*,void*,void *p,void *request,const void*,void*) {
                return fault==3 || static_cast<uint32_t*>(request)[1]!=Display13FBMemory::field<uint32_t>(p,0x12c);
            };
        if(donor)put(donor,0x138c50+0x120,uintptr_t(fault==3));
        IODetailedTimingInformationV2 timing{}; timing.pixelEncoding=1;timing.bitsPerColorComponent=2;
        timing.pixelClock=648000000;timing.dynamicRange=fault==4?2:1;
        plan[1]=boot.firmware.func;plan[12]=0x01000050;plan[3]=1;
        memcpy(plan+4,boot.firmware.timings,24);plan[10]=boot.firmware.source;
        memcpy(path+0x128,plan,sizeof(plan));
        plan[1]=0x8a030006;plan[12]=0x01800010;plan[3]=0x21;
        if(fault==5)plan[4]++;
        if(fault==6)registers[0x60040]++;
        if(fault==7)timing.pixelClock--;
        if(fault==8) {
            timing.minPixelClock=324000000;timing.maxPixelClock=700000000;
            timing.pixelClock/=2;
        }
        if(fault==9) {registers[0x60400]|=0xe;registers[0x64000]|=0xe;}
        if(fault==10)timing.dscCompressedBitsPerPixel=128;
        if(fault==11)timing.verticalBlankingExtension=100;
        if(fault==12)plan[3]|=0x4000;
        if(fault==13)plan[3]=1;
        uint32_t old[60];memcpy(old,plan,sizeof(plan));
        assert(self.orgTGLRetainedMode(ctl,fb,path,plan,&timing,nullptr));
        bool full=XE::selectTGLBootDepth(true,ctl,fb,path,plan,&timing,nullptr);
        bool eligible=fault==0 || fault==7;
        assert(full!=eligible);
        if(full) {
            if(fault==13)old[3]=0x21; // original 8 bpc, not the rejected 6-bpc plan
            assert(!memcmp(old,plan,sizeof(plan)));continue;
        }
        assert(plan[1]==0x8a230006 && plan[12]==0x01800050 && plan[3]==1);
        assert(plan[14]==6 && plan[15]==5 && plan[16]==27 && plan[17]==40);
        memcpy(plan,old,sizeof(plan));
        assert(!XE::selectTGLBootDepth(true,ctl,fb,path,plan,&timing,nullptr));
        assert(Display13FBMemory::field<uint64_t>(fb,0x4358)==540000000);
        if(fault==0)invalidateTGLBootPanels();else put(fb,0x4250,1U);
        memcpy(plan,old,sizeof(plan));
        assert(XE::selectTGLBootDepth(true,ctl,fb,path,plan,&timing,nullptr) && !memcmp(old,plan,sizeof(plan)));
    }
    if (!donor) return 0;
    fault=0;
    // Model an ordinary running 8-bpc GOP at 2560x1440@80, 4 lanes of HBR2.
    registers={{0x45404,5},{0x70008,0xc0000000},{0x60400,0x8a030006},{0x60404,0},
      {0x64000,0x80000006},{0x46140,0x10000000},{0x60540,0x80040300},
      {0x78000,0},{0x78004,0},{0x164280,0},{0x46010,0xcc000000},{0x51004,0},
      {0x164284,337|(16384<<10)},{0x164288,(2<<2)|(1<<6)},
      {0x6001c,2559U<<16|1439U},{0x60030,18},{0x60034,40},
      {0x60040,3},{0x60044,5},{0x70030,0x01000010},{0x60410,0x21}};
    const uint32_t raster[] = {2699U<<16|2559U,2699U<<16|2559U,2639U<<16|2599U,
                               1499U<<16|1439U,1499U<<16|1439U,1450U<<16|1444U};
    for (unsigned i=0; i<6; ++i) registers[0x60000+4*i]=raster[i];
    tglBootPanels[0]={};
    bootArgument="";
    captureTGLBootPanels([](uint32_t)->uint32_t {assert(false && "default handoff read MMIO");return 0;});
    IODetailedTimingInformationV2 timing{};
    timing.horizontalActive=2560; timing.horizontalBlanking=140;
    timing.horizontalSyncOffset=40; timing.horizontalSyncPulseWidth=40;
    timing.verticalActive=1440; timing.verticalBlanking=60;
    timing.verticalSyncOffset=5; timing.verticalSyncPulseWidth=6;
    timing.pixelClock=324000000; timing.pixelEncoding=1; timing.dynamicRange=1;
    timing.bitsPerColorComponent=4;  // RGB 10 bpc

    alignas(8) uint8_t ctl[0x1000]{},fb[0x4400]{},path[0x3660]{},port[0x600]{},cfg[16]{};
    put(fb,0x1d0,ctl+0); put(ctl,0xc40,ctl+0); put(path,0x3644,2U);
    put(path,0x3648,port+0); put(port,0x548,cfg+0);
    self.tglObservationController=ctl; self.orgTGLRegisterRead=reg;
    self.orgTGLRetainedMode=reinterpret_cast<Compare>(donor+0x4f044);
    put(donor,0x138c50+0x120,uintptr_t(0));

    uint32_t existing[60]{},desired[60]{},returned[60]{};
    existing[1]=registers.at(0x60400); existing[3]=0x21; existing[12]=0x01000010;
    memcpy(existing+4,raster,sizeof(raster)); existing[10]=registers.at(0x6001c);
    existing[14]=3; existing[15]=5; existing[16]=18; existing[17]=40;
    memcpy(path+0x128,existing,sizeof(existing));
    memcpy(desired,existing,sizeof(desired));
    desired[1]=0x8a130006; desired[3]=0x41; desired[12]=0x01800030;
    desired[16]=9; desired[17]=16;  // 9.72 Gbit/s of 17.28 Gbit/s payload.
    for (unsigned attempt=0; attempt<2; ++attempt) {
        memcpy(returned,desired,sizeof(returned));
        bool full=self.orgTGLRetainedMode(ctl,fb,path,returned,&timing,nullptr);
        assert(full && XE::selectTGLBootDepth(full,ctl,fb,path,returned,&timing,nullptr));
        assert(!memcmp(returned,desired,sizeof(returned)));
    }
}
