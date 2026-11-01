// P2 adoption journal, separate from Apple objects and MMIO. The adapter owns
// serialization, immutable profile storage, and opaque resource handles.
// This is an internal contract, not the future separately loaded kext ABI.
#ifndef KERN_DSI_SESSION_HPP
#define KERN_DSI_SESSION_HPP
#include "kern_dsi_readout.hpp"
namespace XELP { namespace DSI {
struct Capabilities {
    enum Bit : uint32_t {
        Profile=1U<<0,Readout=1U<<1,FixedMode=1U<<2,InternalDisplay=1U<<3,
        BindResources=1U<<4,SwitchSurface=1U<<5,Stop=1U<<6,IRQ=1U<<7,
        Commands=1U<<8,PanelPower=1U<<9,Backlight=1U<<10
    };
    // Independent namespace: never borrow bits from DisplayContract::Mask.
    uint32_t version {1},bytes {sizeof(Capabilities)},installed {};
    static constexpr uint32_t Adoption=Profile|Readout|FixedMode|InternalDisplay|BindResources|SwitchSurface|Stop|IRQ;
    bool admits(uint32_t required) const {return version==1 && bytes==sizeof(*this) && (installed&required)==required;}
};
enum class SessionState : uint32_t {Unbound,FirmwareActive,Switching,Streaming,Quiesced,Faulted,Released};
enum class SessionError : uint32_t {None,Contract,Binding,Transition,Switch,Stop};
struct Ticket {uint64_t epoch {},serial {};};
struct SurfaceProof {
    Ticket ticket;Identity identity;
    uint64_t backing {};bool liveSurfaceMatches {},frameAdvanced {},timingMatches {};
};
struct StopProof {
    Ticket ticket;Identity identity;
    bool planeStopped {},irqDrained {},dsiStopped {},clockDetached {};
};
struct Session {
    SessionState state {SessionState::Unbound};SessionError firstError {SessionError::None};
    Identity identity;
    uint64_t epoch {},serial {},lease {},currentBacking {},pendingBacking {},retiredBacking {};
    Ticket ticket() const {return {epoch,serial};}
    bool accepts(Ticket t) const {return t.epoch==epoch && t.serial==serial && epoch && serial;}
    bool fail(SessionError error) {if(firstError==SessionError::None)firstError=error;state=SessionState::Faulted;return false;}
    bool bind(const Capabilities &caps,const Readout &snapshot,const Profile &profile,
              uint64_t generation,uint64_t sharedLease,uint64_t firmwareBacking) {
        // A rejected bind must not consume caller-owned handles.
        if(state!=SessionState::Unbound || !caps.admits(Capabilities::Adoption) ||
           !generation || !sharedLease || !firmwareBacking || !snapshot.identity.valid() || !snapshot.matches(profile))return false;
        epoch=generation;serial=1;lease=sharedLease;currentBacking=firmwareBacking;
        identity=snapshot.identity;state=SessionState::FirmwareActive;return true;
    }
    bool beginSurface(uint64_t backing) {
        if((state!=SessionState::FirmwareActive && state!=SessionState::Streaming) ||
           !backing || backing==currentBacking || retiredBacking || serial==UINT64_MAX)return false;
        pendingBacking=backing;++serial;state=SessionState::Switching;return true;
    }
    bool completeSurface(const SurfaceProof &proof) {
        // Stale callbacks do not mutate the current transaction or its error.
        if(state!=SessionState::Switching || !accepts(proof.ticket))return false;
        if(!proof.identity.same(identity) || proof.backing!=pendingBacking ||
           !proof.liveSurfaceMatches || !proof.frameAdvanced || !proof.timingMatches)return fail(SessionError::Switch);
        retiredBacking=currentBacking;currentBacking=pendingBacking;pendingBacking=0;
        state=SessionState::Streaming;return true;
    }
    bool abort(Ticket t,SessionError error) {
        if(!accepts(t) || error==SessionError::None || state==SessionState::Released || state==SessionState::Quiesced)return false;
        return fail(error); // both backings remain held if a switch may have reached hardware
    }
    // Returns one cleanup obligation to the adapter, at most once. The adapter
    // frees backing outside native/owner lock inversions and records completion.
    uint64_t takeRetiredBacking() {
        if(state!=SessionState::Streaming)return 0;
        const auto result=retiredBacking;retiredBacking=0;return result;
    }
    bool stopped(const StopProof &proof) {
        if(state==SessionState::Unbound || state==SessionState::Released || state==SessionState::Quiesced ||
           !accepts(proof.ticket))return false;
        if(!proof.identity.same(identity) || !proof.planeStopped || !proof.irqDrained ||
           !proof.dsiStopped || !proof.clockDetached)return fail(SessionError::Stop);
        state=SessionState::Quiesced;return true;
    }
    struct Cleanup {uint64_t lease {},currentBacking {},pendingBacking {},retiredBacking {};};
    Cleanup takeCleanup() {
        if(state!=SessionState::Quiesced)return {};
        Cleanup result {lease,currentBacking,pendingBacking,retiredBacking};
        lease=currentBacking=pendingBacking=retiredBacking=0;state=SessionState::Released;return result;
    }
};
}}
#endif
