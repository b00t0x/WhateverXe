#include "kern_xelp_native_placement.hpp"
#include <cassert>
#include <vector>
using Bytes=std::vector<uint8_t>;
int main() {
 using A=XELPNative::SurfaceABI;
 const Bytes old[]={{0x8b,0x87,0xa4,0,0,0},{0x8b,0x87,0xb0,0,0,0},{0x8b,0x47,0x0c},{0x8b,0x87,0xb4,0,0,0}};
 const Bytes recent[]={{0x8b,0x87,0x80,0,0,0},{0x8b,0x87,0x98,0,0,0},{0x8b,0x47,0x10},{0x48,0x8b,0x87,0x78,1,0,0,0x0f,0xb6,0x40,0x6a}};
 for(auto expected:{A::Original,A::Attachment6A,A::Attachment6E})for(bool frame:{false,true})for(unsigned padding:{0U,17U}) {
  Bytes functions[4];const uint8_t *ptr[4];size_t length[4];
  for(unsigned i=0;i<4;++i) {
   auto &f=functions[i];f.assign(padding,0x90);
   if(frame)f.insert(f.end(),{0x55,0x48,0x89,0xe5});
   auto load=expected==A::Original?old[i]:recent[i];
   if(i==3 && expected==A::Attachment6E)load.back()=0x6e;
   f.insert(f.end(),load.begin(),load.end());if(frame)f.push_back(0x5d);f.push_back(0xc3);
   ptr[i]=f.data();length[i]=f.size();
  }
  for(bool framebuffer:{false,true}) {
   const auto result=framebuffer && expected!=A::Original ? A::RelocatedFields : expected;
   assert(XELPNative::surfaceABI(ptr,length,framebuffer)==result);
   for(unsigned i=0;i<4;++i) {
    const bool used=framebuffer ? (i==0 || i==2) : i!=2;
    const auto changed=used?A::Unknown:result;
    auto &f=functions[i];const auto at=padding+(frame?4:0)+2;
    f[at]^=1;assert(XELPNative::surfaceABI(ptr,length,framebuffer)==changed);f[at]^=1;
    --length[i];assert(XELPNative::surfaceABI(ptr,length,framebuffer)==changed);++length[i];
    ptr[i]=nullptr;assert(XELPNative::surfaceABI(ptr,length,framebuffer)==changed);ptr[i]=f.data();
   }
  }
 }
}
