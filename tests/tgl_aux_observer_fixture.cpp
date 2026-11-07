#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define arrsize(a) (sizeof(a)/sizeof((a)[0]))
#include <sys/mman.h>
#include "kern_tgl_tc_boot.hpp"
struct IOService;

struct TGLAUXRecord { uint64_t ns, thread; uint32_t word[28]; };
static_assert(sizeof(TGLAUXRecord) == 128, "AUX observation schema");
int mutex; int *tglAUXLock = &mutex;
TGLAUXRecord tglAUXFirst[9][256] {}, tglAUXLast[9][128] {};
uint32_t tglAUXCounts[9] {},tglAUXControlCounts[9] {};
TGLAUXRecord tglAUXControl[9][64] {};
bool quiet;
bool tglQuietTraining() { return quiet; }
void clock_get_uptime(uint64_t *p) { *p = 42; }
void absolutetime_to_nanoseconds(uint64_t a, uint64_t *p) { *p = a; }
void *current_thread() { return reinterpret_cast<void *>(7); }
void IOSimpleLockLock(int *) {}
void IOSimpleLockUnlock(int *) {}
struct XE {
 bool adlsDisplaySelected=false;struct {bool valid=false;unsigned dp=0,panel=0;}adlsWiring;
 // AUX_FIELDS
 uintptr_t tglPortByDDI, tglEnablePG, tglDisablePG;
 uintptr_t orgTGLEnableAUXPower, orgTGLDisableAUXPower;
 static void wrapTGLEnableAUXPower(void *, uint32_t);
 static void wrapTGLDisableAUXPower(void *, uint32_t);
};
XE object, *callbackXE = &object;
#define FunctionCast(f,p) reinterpret_cast<decltype(&f)>(p)
// PRODUCTION_FUNCTIONS

alignas(8) uint8_t port[0x1000] {}, manager[0x80] {}, power[0x80] {};
uint32_t config[8] {};
uint8_t *mmio;
const size_t apertureSize = 0x170000;
unsigned enabled, disabled, pgGets, pgPuts;
bool accessible;
uint32_t &ref(unsigned offset) { return *reinterpret_cast<uint32_t *>(power + offset); }
void *findPort(uint32_t ddi) { assert(ddi == 4); return port; }
void getPG(void *p, uint32_t pg) {
 assert(p == power && pg == 3); ++pgGets; ++ref(0x28);
 assert(mprotect(mmio, apertureSize, PROT_READ | PROT_WRITE) == 0); accessible = true;
}
void putPG(void *p, uint32_t pg) {
 assert(p == power && pg == 3 && ref(0x28)); ++pgPuts;
 if (!--ref(0x28)) { assert(mprotect(mmio, apertureSize, PROT_NONE) == 0); accessible = false; }
}
void enableAUX(void *p, uint32_t ddi) {
 assert(p == power && ddi == 4 && accessible); ++enabled; ++ref(0x64);
}
void disableAUX(void *p, uint32_t ddi) {
 assert(p == power && ddi == 4); ++disabled;
 if (ref(0x64)) --ref(0x64);
}
void checkRecord(const TGLAUXRecord &r, unsigned ddi, unsigned event, bool done) {
 assert(r.ns == 42 && r.thread == 7);
 assert(r.word[1] == event && r.word[2] == done && r.word[4] == ddi);
 assert(r.word[27] == 0x4D454D31); // MEM1: no hardware samples
 for (unsigned i = 10; i <= 23; ++i) assert(r.word[i] == 0);
 assert(r.word[26] == 0);
}
int main() {
 mmio = static_cast<uint8_t *>(mmap(nullptr, apertureSize, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0));
 assert(mmio != MAP_FAILED);
 *reinterpret_cast<uint32_t **>(port + 0x548) = config;
 *reinterpret_cast<uint8_t **>(port + 0x578) = manager;
 *reinterpret_cast<uint32_t *>(port + 0x594) = 0x64410;
 *reinterpret_cast<uint8_t **>(manager + 0x50) = mmio;
 *reinterpret_cast<uint32_t *>(manager + 0x60) = apertureSize;
 object.tglPortByDDI = (uintptr_t)findPort; object.tglEnablePG = (uintptr_t)getPG;
 object.tglDisablePG = (uintptr_t)putPG; object.orgTGLEnableAUXPower = (uintptr_t)enableAUX;
 object.orgTGLDisableAUXPower = (uintptr_t)disableAUX;
 config[0] = 4;
 // Real wrapper reads before first power get, then after final power put.
 // Old observer faults at the first pre-enable record, before native is run.
 XE::wrapTGLEnableAUXPower(power, 4);
 XE::wrapTGLEnableAUXPower(power, 4);
 XE::wrapTGLDisableAUXPower(power, 4);
 assert(accessible && object.tglAUXParentRefs[4] == 1);
 XE::wrapTGLDisableAUXPower(power, 4);
 assert(!accessible && object.tglAUXParentRefs[4] == 0);
 assert(enabled == 2 && disabled == 2 && pgGets == 2 && pgPuts == 2);
 // Unowned EFI reference and an unbalanced disable never put our parent.
 ref(0x64) = 1;
 XE::wrapTGLDisableAUXPower(power, 4);
 XE::wrapTGLDisableAUXPower(power, 4);
 assert(pgPuts == 2 && !accessible && ref(0x64) == 0);
 unsigned boundaries=0;
 for(unsigned i=0;i<tglAUXCounts[4] && i<256;++i){
  const auto &record=tglAUXFirst[4][i];
  if(record.word[1]!=5 && record.word[1]!=6)continue;
  checkRecord(record,4,boundaries<4?5:6,boundaries%2);++boundaries;
 }
 assert(boundaries==12); // additional parent-PG telemetry does not replace boundaries
 // Every event/port is memory-only, even with an inaccessible manager.
 *reinterpret_cast<uint8_t **>(port + 0x578) = mmio;
 unsigned checked = 0;
 for (unsigned ddi = 0; ddi < 9; ++ddi) {
  config[0] = ddi;
  for (unsigned event = 0; event < 7; ++event) for (bool done : {false, true}) {
   tglAUXRecord(port, event, done, 3, 4, 5, 6, 7);
   auto &r = tglAUXLast[ddi][(tglAUXCounts[ddi] - 1) % 128];
   checkRecord(r, ddi, event, done);
   assert(r.word[3] == 3 && r.word[7] == 4 && r.word[8] == 5 && r.word[24] == 6 && r.word[25] == 7);
   ++checked;
  }
 }
 const auto count = tglAUXCounts[8]; quiet = true;
 tglAUXRecord(port, 0, false, 0, 0, 0); assert(tglAUXCounts[8] == count+1); quiet = false;
 tglAUXRecord(nullptr, 0, false, 0, 0, 0);
 config[0] = 9; tglAUXRecord(port, 0, false, 0, 0, 0);
 assert(tglAUXCounts[8] == count+1);
 munmap(mmio, apertureSize);
 printf("PASS: actual AUX power wrappers preserve 2 get/put pairs, EFI/unbalanced handling; %u port/event records with inaccessible MMIO/manager; record schema and quiet-training retention\n", checked);
}
