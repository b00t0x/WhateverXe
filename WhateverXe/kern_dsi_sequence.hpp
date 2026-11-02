// Bounded VBT v3 sequence description. No hardware operations or allocations.
// Layout reference: Intel intel_bios.c/intel_dsi_vbt.c, Linux v7.0.
// Spans borrow immutable VBT storage: its owner must outlive this description.
#ifndef KERN_DSI_SEQUENCE_HPP
#define KERN_DSI_SEQUENCE_HPP
#include <stdint.h>
#include <stddef.h>
namespace XELP { namespace DSI {
struct Span { const uint8_t *data {}; size_t size {}; };
inline bool nativePanelGPIO(unsigned role) {return role<=2 || (role>=5 && role<=7);}
// Firmware may combine operations in other lifecycle sequences or omit them.
// Only the fully empty span means absent; malformed spans still reject.
inline bool absentSequence(Span bytes) {return !bytes.data && !bytes.size;}
enum class SequenceStatus : uint32_t {
    Valid, Bounds, Version, Panel, Duplicate, Sequence, Operation, Packet, GPIO
};
struct Sequences {
    SequenceStatus status {SequenceStatus::Bounds};
    Span sequence[12] {};
    uint32_t packets {}, delays {}, gpios {};
    uint64_t delayUs {};
    bool valid() const {return status==SequenceStatus::Valid;}
};
inline bool longWrite(uint8_t type) {return type==0x29 || type==0x39;}
inline uint32_t sequenceU32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16 | uint32_t(p[3])<<24;
}
inline SequenceStatus validateSequence(Span bytes, Sequences &counts) {
    if(!bytes.data || !bytes.size || bytes.size>65536)return SequenceStatus::Bounds;
    for(size_t at=0;at<bytes.size;) {
        const auto op=bytes.data[at++];
        if(!op)return at==bytes.size ? SequenceStatus::Valid : SequenceStatus::Bounds;
        if(at==bytes.size)return SequenceStatus::Bounds;
        const size_t n=bytes.data[at++];
        if(n>bytes.size-at)return SequenceStatus::Bounds;
        const auto p=bytes.data+at;at+=n;
        switch(op) {
            case 1: {
                if(n<4)return SequenceStatus::Packet;
                const size_t length=uint32_t(p[2]) | uint32_t(p[3])<<8;
                // Initial profile: LP commands, VC0, port A. Other transport
                // flags require a separate host contract, never silent coercion.
                if(p[0] || length!=n-4)return SequenceStatus::Packet;
                if(!((p[1]==0x05 && length==1) || (p[1]==0x15 && length==2) ||
                     (longWrite(p[1]) && length>=1)))return SequenceStatus::Packet;
                ++counts.packets;break;
            }
            case 2:
                if(n!=4)return SequenceStatus::Operation;
                ++counts.delays;counts.delayUs+=sequenceU32(p);break;
            case 3:
                // v3 native display GPIO: index, GPIO role, value. The index
                // is retained in the bytes; it is not a gpiochip identifier.
                if(n!=3 || !nativePanelGPIO(p[1]) || p[2]>1)return SequenceStatus::GPIO;
                ++counts.gpios;break;
            default:return SequenceStatus::Operation;
        }
    }
    return SequenceStatus::Bounds; // required END byte absent
}
inline Sequences parseSequences(Span block, uint8_t selectedPanel) {
    Sequences r;
    if(!block.data || block.size<5 || block.size>65536)return r;
    if(block.data[0]!=3){r.status=SequenceStatus::Version;return r;}
    // BDB v3 size excludes the version and size header (five bytes).
    if(sequenceU32(block.data+1)!=block.size-5)return r;
    if(selectedPanel>=6){r.status=SequenceStatus::Panel;return r;}
    uint32_t panels=0;Span selected;
    for(size_t at=5;at<block.size;) {
        if(block.size-at<5)return r;
        const auto panel=block.data[at];const size_t n=sequenceU32(block.data+at+1);at+=5;
        if(panel>=6){r.status=SequenceStatus::Panel;return r;}
        if(panels&(1U<<panel)){r.status=SequenceStatus::Duplicate;return r;}
        panels|=1U<<panel;
        if(!n || n>block.size-at)return r;
        if(panel==selectedPanel)selected={block.data+at,n};
        at+=n;
    }
    if(!selected.data){r.status=SequenceStatus::Panel;return r;}
    // Parse into a temporary: a failed preflight never publishes executable
    // spans or partial counts to a later interpreter.
    Sequences candidate;
    for(size_t at=0;at<selected.size;) {
        const auto id=selected.data[at++];
        if(!id) {
            if(at!=selected.size)return r;
            candidate.status=SequenceStatus::Valid;return candidate;
        }
        if(id>=12){r.status=SequenceStatus::Sequence;return r;}
        if(candidate.sequence[id].data){r.status=SequenceStatus::Duplicate;return r;}
        if(selected.size-at<4)return r;
        const size_t n=sequenceU32(selected.data+at);at+=4;
        if(!n || n>selected.size-at)return r;
        Span bytes {selected.data+at,n};
        r.status=validateSequence(bytes,candidate);
        if(r.status!=SequenceStatus::Valid)return r;
        r.status=SequenceStatus::Bounds;
        candidate.sequence[id]=bytes;at+=n;
    }
    return r;
}
}}
#endif
