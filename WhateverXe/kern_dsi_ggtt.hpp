// Configure shared Display13 scanout addressing before native display users.
// Linux v6.12 intel_fb_modifier_uses_dpt excludes LINEAR; tiled native surfaces
// use GGTT addresses and need CHICKEN_MISC_DISABLE_DPT (i915_reg.h).
#ifndef KERN_DSI_GGTT_HPP
#define KERN_DSI_GGTT_HPP
#include "kern_dsi_boot.hpp"
namespace XELP { namespace DSI {
struct GGTTMode {
    enum class Status : uint32_t {Unprepared,Source,Readback,Changed,Complete};
    Status status {Status::Unprepared};uint32_t before {},after {},writes {};
    bool ready() const {return status==Status::Complete;}
};
// Caller owns startup serialization, source power/PLL, and the global display
// mode. No external output or native engine may have started. This changes no
// PTE, plane, timing, or DSI register. Preserve every unrelated chicken bit.
// Do not restore the global mode when only DSI stops: external tiled consumers
// share it. A failed write remains recorded for controller-level cleanup.
template<class IO> bool prepareGGTT(IO &io,Boot &boot,const XELPMemory::Layout &memory,GGTTMode &mode) {
    if(mode.status!=GGTTMode::Status::Unprepared)return false;
    mode.status=GGTTMode::Status::Source;
    if(!boot.complete() || !bootUnchanged(io,boot,memory))return false;
    mode.before=boot.dpt;mode.after=mode.before|(1U<<30);
    if(mode.before!=mode.after) {
        io.write(0x42084,mode.after);++mode.writes;
        mode.status=GGTTMode::Status::Readback;
        if(!io.readable(0x42084) || io.read(0x42084)!=mode.after) {
            boot.status=BootStatus::Changed;return false;
        }
    }
    // Extend the certificate only after exact posting readback. No caller may
    // keep using the earlier dpt word as proof once this write has happened.
    boot.dpt=mode.after;mode.status=GGTTMode::Status::Changed;
    if(!bootUnchanged(io,boot,memory)){boot.status=BootStatus::Changed;return false;}
    mode.status=GGTTMode::Status::Complete;return true;
}
}}
#endif
