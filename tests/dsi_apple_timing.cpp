#include "kern_dsi_apple_timing.hpp"
#include <cassert>
#include <cstdio>
using namespace XELP::DSI;
int main() {
    Profile p;p.status=ProfileStatus::Valid;p.mode={135255,1080,46,22,2,1920,82,40,2,86,154};
    AppleAdapter::Timing t;assert(AppleAdapter::makeTiming(p,t));
    assert(t.pixelClock==135255000&&t.minPixelClock==135241474&&t.maxPixelClock==135268526);
    assert(t.horizontalActive==1080&&t.horizontalBlanking==46&&t.horizontalSyncOffset==22&&t.horizontalSyncWidth==2);
    assert(t.verticalActive==1920&&t.verticalBlanking==82&&t.verticalSyncOffset==40&&t.verticalSyncWidth==2);
    assert(t.bitsPerColorComponent==2&&t.pixelEncoding==1&&t.numLinks==1);
    assert(!t.scalerFlags&&!t.horizontalScaled&&!t.verticalScaled&&!t.verticalBlankingExtension);
    p.mode.hsync=46;assert(!AppleAdapter::makeTiming(p,t)&&!t.pixelClock);
    p={};assert(!AppleAdapter::makeTiming(p,t));
    puts("DSI fixed Apple timing: Hz/range, portrait geometry, RGB8 bit fields, no scaling/DSC: PASS");
}
