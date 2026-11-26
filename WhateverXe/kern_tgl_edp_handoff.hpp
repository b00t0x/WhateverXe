// Initial Tiger Lake RGB eDP handoff. Read only the active pipe and Combo DDI.
#ifndef KERN_TGL_EDP_HANDOFF_HPP
#define KERN_TGL_EDP_HANDOFF_HPP
#include "kern_display13_handoff.hpp"

namespace TGLEDPHandoff {
using Display13Handoff::Stream;
using Display13Handoff::Receiver;
using Display13FBMemory::field;

template<class Read> bool capture(Read read, unsigned pipe, Stream &s) {
    if (pipe >= 4) return false;
    const auto power = read(0x45404);
    // Tiger Lake pipe A belongs to PW1, pipes B/C/D to PW3/4/5.
    const unsigned well[] = {0, 2, 3, 4};
    if (power == UINT32_MAX || !(power & (1U << (2 * well[pipe])))) return false;
    const unsigned bank = pipe * 0x1000;
    const auto conf = read(0x70008 + bank);
    if (conf == UINT32_MAX || (conf & 0xc0000000) != 0xc0000000) return false;
    s.func = read(0x60400 + bank);
    const auto port = (s.func >> 27) & 15;
    if ((port != 1 && port != 2) || (s.func & 0x87008200) != 0x82000000 ||
        ((s.func >> 20) & 7) > 2 || read(0x60404 + bank) != 0) return false;
    if (s.lanes() != 1 && s.lanes() != 2 && s.lanes() != 4) return false;
    const unsigned ddi = port - 1;
    const auto buffer = read(0x64000 + ddi * 0x100);
    const auto clock = read(0x46140 + 4 * pipe);
    s.transport = read(0x60540 + bank);
    if ((buffer & 0x800000c0) != 0x80000000 || ((buffer >> 1) & 7) + 1 != s.lanes() ||
        (clock >> 28) != port || (s.transport & ~0x40040U) != 0x80000300U) return false;
    // A powered-down DSC domain cannot own a running compressor.
    if ((pipe || (power & 4)) && ((read(0x78000 + pipe * 0x200) & 0xf0300000) ||
                                  (read(0x78004 + pipe * 0x200) & 0x80008000))) return false;
    const auto mux = read(0x164280);
    s.mux = (mux >> (2 * ddi)) & 3;
    if ((mux & (1U << (10 + ddi))) || s.mux > 1) return false;
    s.pll = read(0x46010 + 4 * s.mux);
    const unsigned refs[] = {24000,19200,38400};
    const unsigned ref = read(0x51004) >> 29;
    if (ref >= 3 || s.pll == UINT32_MAX || (s.pll & 0xcc000000) != 0xcc000000) return false;
    s.symbolKHz = Display13Handoff::comboRate(refs[ref], read(0x164284 + 8 * s.mux), read(0x164288 + 8 * s.mux));
    for (unsigned i = 0; i < 6; ++i) {
        s.timings[i] = read(0x60000 + bank + 4 * i);
        if (s.timings[i] == UINT32_MAX) return false;
    }
    s.source = read(0x6001c + bank);
    s.dataM = read(0x60030 + bank); s.dataN = read(0x60034 + bank);
    s.linkM = read(0x60040 + bank); s.linkN = read(0x60044 + bank);
    s.pipeMisc = read(0x70030 + bank) & 0x080008fc;
    return s.symbolKHz && !(s.pipeMisc & 0x08000800) && s.source != UINT32_MAX &&
        s.dataM && !(s.dataM & 0x81000000) && (s.dataM & 0xffffff) && (s.dataM & 0xffffff) <= s.dataN &&
        s.dataN && !(s.dataN & 0xff000000) && s.linkM && !(s.linkM & 0xff000000) &&
        s.linkN && !(s.linkN & 0xff000000);
}

// Restrict the alternative to the same SDR mode, same lane count and port,
// and a depth promotion. The original timing and the full-mode plan remain
// owned by the donor. It gets the last word on the normalized partial plan.
inline bool propose(const Stream &stream, const Receiver &sink, const void *timing, uint32_t *plan) {
    const unsigned depth = (stream.func >> 20) & 7, desired = (plan[1] >> 20) & 7;
    const unsigned bpc[] = {8,10,6}, enc[] = {2,4,1}, msa[] = {0x20,0x40,0};
    if (depth > 2 || desired > 2 || bpc[depth] >= bpc[desired] ||
        ((stream.func ^ plan[1]) & ~0x700000U) || field<uint16_t>(timing,0x88) != 1 ||
        field<uint16_t>(timing,0x8a) != enc[desired] || field<uint16_t>(timing,0x8e) > 1 ||
        field<uint32_t>(timing,0x84) || field<uint16_t>(timing,0x90) || (plan[3] & 0xc006) ||
        (plan[12] & 0x08000800)) return false;
    plan[1] = stream.func;
    plan[3] = (plan[3] & ~0xe0U) | msa[depth];
    plan[12] = (plan[12] & ~0xfcU) | (stream.pipeMisc & 0xfc);
    IODetailedTimingInformationV2 retainedTiming;
    memcpy(&retainedTiming, timing, sizeof(retainedTiming));
    retainedTiming.bitsPerColorComponent = enc[depth];
    // The same raster at a different refresh is a new mode, even when
    // the panel advertises a wider range of legal clocks.
    if ((retainedTiming.minPixelClock && retainedTiming.minPixelClock > retainedTiming.pixelClock) ||
        (retainedTiming.maxPixelClock && retainedTiming.maxPixelClock < retainedTiming.pixelClock)) return false;
    retainedTiming.minPixelClock = retainedTiming.maxPixelClock = retainedTiming.pixelClock;
    Stream measured = stream;
    // PIPE_MISC BPC is unused on TGL when dithering is off.
    if (!(measured.pipeMisc & 0x10)) measured.pipeMisc = (measured.pipeMisc & ~0xe0U) | (depth << 5);
    return Display13Handoff::normalize(measured,sink,&retainedTiming,plan);
}
}
#endif
