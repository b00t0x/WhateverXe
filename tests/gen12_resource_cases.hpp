// Test oracle: Gen12 SSEU fuse bit meanings, not the decoder's admission formula.
// Linux v6.12 intel_sseu.c/intel_gt_regs.h and Intel TGL PRM Vol. 4:
// one DSS has two native subslices; EU disable bits describe pairs;
// URB capacity is 64 KiB per enabled L3 bank, independently of EU population.
// These are positive capacity examples. Unlisted encodings are NOT negative
// specifications. In particular this file does not justify rejecting every
// odd bank population or every other EU-disable population.
#ifndef TEST_GEN12_RESOURCE_CASES_HPP
#define TEST_GEN12_RESOURCE_CASES_HPP
#include <cstdint>
namespace ResourceCases {
struct Capacity {uint32_t fuses[5];unsigned dss,eus,banks;};
constexpr Capacity positive[]={
 {{1,0x1f,0,0x88,0xe00fa},5,80,6}, // retained physical fuse readout
 {{1,0x07,0,0xc0,0xe00fe},3,48,6}, // PRM capacity example encoded as a synthetic fuse vector
 {{1,0x3f,0,0,0xe00fa},6,96,8},    // retained physical fuse readout
 // Synthetic examples derived from independent resource fields, not SKUs.
 {{1,0x01,0,0xf0,0xe00fa},1,16,4},
 {{1,0x03,0xc0,0xf0,0xe00fe},2,24,4},
 {{1,0x03,0,0xf0,0xe00fa},2,32,4},
 {{1,0x0f,0,0,0xe00fa},4,64,8},
 {{1,0x0f,0xc0,0xc0,0xe00fa},4,48,6},
 {{1,0x3f,0xc0,0xf0,0xe00fa},6,72,4},
 {{1,0x1f,0,0,0xe00fa},5,80,8},
};
inline uint32_t rotate(uint32_t value,unsigned shift,unsigned width) {
 const auto mask=(1U<<width)-1;shift%=width;
 return ((value<<shift)|(value>>((width-shift)%width)))&mask;
}
template<class Decode> void check(Decode decode) {
 for(const auto &input:positive)for(unsigned d=0;d<6;++d)
 for(unsigned e=0;e<8;++e)for(unsigned b=0;b<8;++b) {
  auto variant=input;
  // Fuse positions may change without changing independent populations.
  variant.fuses[1]=rotate(input.fuses[1],d,6);
  variant.fuses[2]=rotate(input.fuses[2],e,8);
  variant.fuses[3]=rotate(input.fuses[3],b,8);
  XELP::GTResources result;
  assert(decode(variant.fuses,result));
  assert(result.slices==1 && result.dss==input.dss && result.eus==input.eus);
  assert(result.nativeSubslices==2*input.dss && result.l3Banks==input.banks);
  assert(result.urbKiB==64*input.banks);
 }
}
}
#endif
