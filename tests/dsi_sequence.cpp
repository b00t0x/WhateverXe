#include "kern_dsi_sequence.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
using namespace XELP::DSI;
using Bytes=std::vector<uint8_t>;
void word(Bytes &b,uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(n>>(8*i));}
Bytes panel(const Bytes &ops,uint8_t id=3){Bytes b{id};word(b,ops.size());b.insert(b.end(),ops.begin(),ops.end());b.push_back(0);return b;}
Bytes block(const Bytes &data){Bytes b{3};word(b,data.size()+5);b.push_back(0);word(b,data.size());b.insert(b.end(),data.begin(),data.end());return b;}
Sequences parse(const Bytes &b){return parseSequences({b.data(),b.size()},0);}
void rejected(const Bytes &b){auto r=parse(b);assert(!r.valid());assert(!r.packets&&!r.delays&&!r.gpios);for(auto s:r.sequence)assert(!s.data);}
int main(int argc,char **argv) {
    // Synthetic standard DCS command, delay and native GPIO; no vendor blob.
    Bytes ops {1,5,0,5,1,0,0x29,2,4,0x10,0x27,0,0,3,3,0,2,1,0};
    auto b=block(panel(ops));auto r=parse(b);
    assert(r.valid()&&r.packets==1&&r.delays==1&&r.gpios==1&&r.delayUs==10000);
    for(size_t n=0;n<b.size();++n)rejected(Bytes(b.begin(),b.begin()+n));
    for(auto pair:std::vector<std::pair<unsigned,uint8_t>>{{0,4},{1,255},{5,6},{10,12},{11,255}}) {
        auto bad=b;bad[pair.first]=pair.second;rejected(bad);
    }
    for(auto op:Bytes{4,5,6,7,255}){auto bad=ops;bad[0]=op;rejected(block(panel(bad)));}
    for(auto flags:Bytes{1,2,8,32,255}){auto bad=ops;bad[2]=flags;rejected(block(panel(bad)));}
    auto bad=ops;bad[3]=6;rejected(block(panel(bad))); // read unsupported
    bad=ops;bad[4]=2;rejected(block(panel(bad))); // wrong packet length
    bad=ops;bad.pop_back();rejected(block(panel(bad))); // no operation END
    bad=ops;bad.push_back(0);rejected(block(panel(bad))); // bytes after END
    bad=ops;bad[16]=3;rejected(block(panel(bad))); // unimplemented GPIO role
    for(unsigned role:{0U,1U,2U,5U,6U,7U}) {
        auto gpio=ops;gpio[16]=role;assert(parse(block(panel(gpio))).valid());
    }
    for(unsigned role:{3U,4U,8U,9U,255U}) {
        auto gpio=ops;gpio[16]=role;rejected(block(panel(gpio)));
    }
    auto data=panel(ops);data.pop_back();auto second=panel(ops);data.insert(data.end(),second.begin(),second.end());
    rejected(block(data)); // duplicate sequence
    auto duplicate=b;duplicate.insert(duplicate.end(),b.begin()+5,b.end());
    const auto size=duplicate.size()-5;for(unsigned i=0;i<4;++i)duplicate[1+i]=size>>(8*i);
    rejected(duplicate); // duplicate panel
    assert(parseSequences({b.data(),b.size()},1).status==SequenceStatus::Panel);
    assert(!parseSequences({nullptr,5},0).valid());
    if(argc==2) {
        std::ifstream f(argv[1],std::ios::binary);assert(f);
        Bytes real{std::istreambuf_iterator<char>(f),{}};auto q=parse(real);
        assert(q.valid()&&q.packets==359&&q.gpios==8&&q.delays==11&&q.delayUs==486000);
        assert(!q.sequence[3].data&&q.sequence[2].data&&q.sequence[10].data&&q.sequence[11].data);
        printf("saved profile: packets=%u delays=%u GPIO=%u delayUs=%llu\n",q.packets,q.delays,q.gpios,(unsigned long long)q.delayUs);
    }
    puts("DSI sequence bounds/unsupported operations/transactional publication: PASS");
}
