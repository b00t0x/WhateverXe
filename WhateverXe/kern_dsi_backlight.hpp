// Native PWM backlight for the supported DSI panel profile. The Apple adapter
// owns serialization/power and keeps requested brightness across panel-off.
// PWM register layout and VBT minimum scaling follow Linux intel_backlight.c.
#ifndef KERN_DSI_BACKLIGHT_HPP
#define KERN_DSI_BACKLIGHT_HPP
#include "kern_dsi_profile.hpp"
#include "kern_xelp_panel_pwm.hpp"
namespace XELP { namespace DSI {
struct Backlight {
    static constexpr uint32_t Control=0xC8250,Period=0xC8254,Duty=0xC8258,Maximum=65535;
    PanelPWM pwm;
    uint32_t control {},minimum {},requested {},applied {},requests {},writes {};
    bool initialized {},faulted {};
    bool capture(const Profile &p,uint32_t ctl,uint32_t period,uint32_t duty) {
        if(initialized || !p.valid() || p.controller || p.precision!=8 ||
           ctl==UINT32_MAX || !(ctl&(1U<<31)) || !period || period==UINT32_MAX || duty>period)return false;
        pwm={period,ctl&(1U<<29)};control=ctl;
        const auto vbtMin=p.minimum>64?64:p.minimum;
        minimum=uint32_t((uint64_t(period)*vbtMin+127)/255);
        applied=duty;requested=uint32_t((uint64_t(duty)*Maximum+period/2)/period);
        initialized=true;return true;
    }
    bool request(uintptr_t value) {
        if(!initialized || faulted || value>Maximum)return false;
        requested=static_cast<uint32_t>(value);++requests;return true;
    }
    uint32_t target() const {
        uint32_t duty=0;pwm.duty(requested,duty);
        // Apple brightness zero is dark. Positive requests respect the panel's
        // VBT minimum; turning the panel off is a separate lifecycle operation.
        return requested && duty<minimum?minimum:duty;
    }
    template<class IO> bool apply(IO &io) {
        if(!initialized || faulted || !io.ready() || !io.readable(Control) ||
           !io.readable(Period) || !io.readable(Duty))return false;
        if(io.read(Control)!=control || io.read(Period)!=pwm.period){faulted=true;return false;}
        const auto duty=target();io.write(Duty,duty);++writes;
        if(!io.ready() || io.read(Duty)!=duty){faulted=true;return false;}
        applied=duty;return true;
    }
};
}}
#endif
