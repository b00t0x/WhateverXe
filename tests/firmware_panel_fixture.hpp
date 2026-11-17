// Independent Intel wire-format fixture: 16 profiles, one internal DP-A and
// one HDMI-B. Profile 9 is identified by mailbox-5 EDID, not a host override.
#ifndef TEST_FIRMWARE_PANEL_FIXTURE_HPP
#define TEST_FIRMWARE_PANEL_FIXTURE_HPP
#include <vector>
#include <cstring>
namespace PanelFixture {
inline void w16(std::vector<uint8_t>&b,size_t at,unsigned v){b[at]=v;b[at+1]=v>>8;}
inline void w32(std::vector<uint8_t>&b,size_t at,unsigned v){w16(b,at,v);w16(b,at+2,v>>16);}
struct Input {
 std::vector<uint8_t> vbt,op;size_t options,edp,power,backlight,ptr=0,data;
 size_t block(unsigned id,unsigned n){size_t a=vbt.size();vbt.resize(a+3+n);vbt[a]=id;w16(vbt,a+1,n);return a+3;}
 void finish(){w16(vbt,24,vbt.size());w16(vbt,68,vbt.size()-48);vbt[26]=0;uint8_t sum=0;for(auto v:vbt)sum+=v;vbt[26]=-sum;memcpy(op.data()+0x400,vbt.data(),vbt.size());}
 void edidChecksum(){op[0x1c04+127]=0;uint8_t sum=0;for(unsigned i=0;i<128;++i)sum+=op[0x1c04+i];op[0x1c04+127]=-sum;}
 Input(bool pointers=false,bool duplicate=false):vbt(70),op(8192) {
  memcpy(vbt.data(),"$VBT",4);w16(vbt,22,48);w32(vbt,28,48);
  memcpy(vbt.data()+48,"BIOS_DATA_BLOCK ",16);w16(vbt,64,250);w16(vbt,66,22);
  auto g=block(2,5+39*(duplicate?3:2));vbt[g+4]=39;
  auto c=g+5;w16(vbt,c,8);w16(vbt,c+2,0x78c6);vbt[c+16]=10;vbt[c+25]=0x40;
  auto h=c+39;w16(vbt,h+2,0x60d2);vbt[h+16]=1;vbt[h+19]=2;
  if(duplicate)memcpy(vbt.data()+h+39,vbt.data()+c,39);
  options=block(40,2);vbt[options]=255;
  edp=block(27,212);power=block(44,56);backlight=block(43,129);vbt[backlight]=6;
  if(pointers)ptr=block(41,148);
  data=block(42,66*16);
  for(unsigned n=0;n<16;++n) {
   for(unsigned i=0;i<5;++i)w16(vbt,edp+10*n+2*i,100*n+10+i);
   vbt[backlight+1+6*n]=2;w16(vbt,backlight+2+6*n,200+n);vbt[backlight+113+n]=2;
   w16(vbt,data+66*n+36,0xffff);
   for(unsigned i=0;i<10;++i)vbt[data+66*n+56+i]=n*10+i+1;
   if(pointers){vbt[ptr]=3;for(unsigned k=0;k<3;++k){w16(vbt,ptr+1+9*n+3*k,data-48+66*n+(k==0?0:k==1?38:56));vbt[ptr+3+9*n+3*k]=k==0?38:k==1?18:10;}}
  }
  memcpy(op.data(),"IntelGraphicsMem",16);w32(op,16,8);w32(op,0x58,0x10);w32(op,0x1c00,1);
  const uint8_t signature[]={0,255,255,255,255,255,255,0};memcpy(op.data()+0x1c04,signature,8);
  memcpy(op.data()+0x1c0c,vbt.data()+data+66*9+56,10);op[0x1c04+20]=0x80;op[0x1c04+18]=1;op[0x1c04+19]=4;
  edidChecksum();finish();
 }
};
}
#endif
