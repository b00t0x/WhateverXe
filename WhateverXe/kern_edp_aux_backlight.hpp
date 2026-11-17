// Standard VESA eDP backlight transactions. Register/capability facts from
// Linux v6.12 drm_dp.h and drm_dp_helper.c; independent implementation.
// IO serializes with panel power and owns the native AUX/power lease. No port
// pointers or asynchronous work survive a transaction. Intel HDR/OLED nits
// calibration is a separate protocol, not an implicit fallback.
#ifndef KERN_EDP_AUX_BACKLIGHT_HPP
#define KERN_EDP_AUX_BACKLIGHT_HPP
#include <stdint.h>
namespace EDPAuxBacklight {
struct Caps {bool set {},enable {},wide {};uint32_t maximum {};};
enum class Action {Level,Enable,Disable};
template<class IO> bool probe(IO &io,Caps &c) {
 uint8_t caps[3] {};if(!io.read(0x700,caps,3) || !(caps[1]&1))return false;
 c.set=caps[2]&2;c.enable=caps[1]&4;c.wide=caps[2]&4;
 // AUX enable and AUX level are independent. PWM-only level requires the
 // caller's physical PWM backend; no automatic success for a missing wire.
 if(!c.set && !(caps[2]&1))return false;
 if(!c.set)return true;
 uint8_t bits=0;if(!io.read(0x724,&bits,1))return false;bits&=31;
 if(!bits || bits>16 || (!c.wide && bits>8))return false;
 c.maximum=(1U<<bits)-1;return true;
}
template<class IO> bool enable(IO &io,bool value) {
 uint8_t v=0;if(!io.read(0x720,&v,1))return false;
 v=value?uint8_t(v|1):uint8_t(v&~1U);
 if(!io.write(0x720,&v,1))return false;
 uint8_t after=0;return io.read(0x720,&after,1) && after==v;
}
template<class IO> bool apply(IO &io,const Caps &c,Action action,uint32_t brightness) {
 if(brightness>65535)return false;
 if(action==Action::Disable)return !c.enable || enable(io,false);
 uint8_t mode=0;if(!io.read(0x721,&mode,1))return false;
 // Preserve panel-specific frequency and unrelated controls. Replace the
 // product/preset selector with the negotiated AUX or PWM level interface.
 mode=uint8_t((mode&~3U)|(c.set?2:0));
 if(!io.write(0x721,&mode,1))return false;
 uint8_t after=0;if(!io.read(0x721,&after,1) || after!=mode)return false;
 if(c.set) {
  const auto level=uint32_t(uint64_t(brightness)*c.maximum/65535);
  uint8_t data[2]={uint8_t(c.wide?level>>8:level),uint8_t(c.wide?level:0)},check[2] {};
  if(!io.write(0x722,data,2) || !io.read(0x722,check,c.wide?2:1) ||
     check[0]!=data[0] || (c.wide && check[1]!=data[1]))return false;
 }
 return action!=Action::Enable || !c.enable || enable(io,true);
}
}
#endif
