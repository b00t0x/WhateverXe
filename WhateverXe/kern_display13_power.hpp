// Display 13 request ownership, independent of the TGL donor's hierarchical
// PG counters. Linux v6.12 intel_display_power_map.c (xelpd) / i915_reg.h /
// intel_display_power_well.c define the domains, request bits and fuse waits.
// This is not the native PowerWell ABI adapter. Its caller must serialize all
// requests and quiesce a pipe/its interrupts before releasing its last lease.
#ifndef kern_display13_power_hpp
#define kern_display13_power_hpp
#include <stdint.h>

namespace Display13Power {
enum class Domain : uint32_t { PW1, PW2, PipeA, PipeB, PipeC, DDIB, AUXB, DDIA, AUXA, DDIC, AUXC, DDITC1, AUXTC1, DDITC2, AUXTC2, DDITC3, AUXTC3, DDITC4, AUXTC4, Count };
constexpr unsigned Count = static_cast<unsigned>(Domain::Count);
constexpr uint32_t bit(Domain d) { return 1U << static_cast<unsigned>(d); }
constexpr uint32_t All = (1U << Count) - 1;
constexpr uint32_t LegacyDomains = (1U << 7) - 1; // Existing B backend scope.
constexpr uint32_t BootHDMIA = bit(Domain::PipeA) | bit(Domain::DDIA);
constexpr uint32_t BootHDMIB = bit(Domain::PipeA) | bit(Domain::DDIB);
constexpr uint32_t Driver = 0x45404, DDI = 0x45454, AUX = 0x45444, Fuses = 0x42000;
// gen9_wait_for_power_well_fuses passes 1 to intel_de_wait_for_set.
// That API reaches intel_wait_for_register(timeout_ms), despite the 1/5 us
// hardware timing comment. Keep an explicit microsecond budget here.
constexpr unsigned FuseTimeoutUs = 1000;
constexpr bool tcAux(unsigned d) { return d>=12 && d<Count && !(d&1); }
constexpr bool tcDDI(unsigned d) { return d>=11 && d<Count && (d&1); }
constexpr uint32_t control(unsigned d) { return (d == 5 || d == 7 || d == 9 || tcDDI(d)) ? DDI : (d == 6 || d == 8 || d == 10 || tcAux(d)) ? AUX : Driver; }
constexpr unsigned index(unsigned d) { return d>=11 ? 3+(d-11)/2 : d < 2 ? d : d >= 9 ? 2 : d >= 7 ? 0 : d >= 5 ? 1 : d + 3; }
constexpr uint32_t request(unsigned d) { return 2U << (2 * index(d)); }
constexpr uint32_t enabled(unsigned d) { return 1U << (2 * index(d)); }
// XE_LPD: DDI/AUX A use index0 (request1/state0), B use index1 (request3/state2), C index2 (request5/state4).
// Unlike PG wells it has no fuse wait or Type-C PHY negotiation.
constexpr uint32_t fuse(unsigned d) { return 1U << (26 - index(d)); }

// PW_A and Combo A/B/C DDI/AUX use PW1; PW_B/C also use shared PW2. In particular,
// PW2 is not a prerequisite of PW_A in the XE_LPD power-domain map.
inline uint32_t dependencies(uint32_t domains) {
    if (domains & ~All) return 0;
    // XE_LPD PW2 includes both TC DDI lanes and AUX, even on pipe A.
    // Releasing an AUX transaction must not gate an active TMDS main link.
    constexpr uint32_t tcMask=All&~((1U<<11)-1);
    if (domains & (bit(Domain::PipeB) | bit(Domain::PipeC) | tcMask)) domains |= bit(Domain::PW2);
    if (domains) domains |= bit(Domain::PW1);
    return domains;
}

enum class Status : uint32_t {
    Complete, NotReady, InvalidDomains, Unavailable, Overflow, Unbalanced,
    FuseTimeout, StateTimeout, RequestLost, ReleaseFailed, UnqualifiedTCAUX, AuxHealthTimeout
};
struct Result {
    Status status {Status::NotReady};
    uint32_t domain {Count}, writes {}, rollbackFailed {}, forcedOn {};
    bool isolated {}; // Failed new TC leaf, rollback and retained scopes proven.
};
struct State {
    bool ready {}, faulted {}, suspended {};
    uint32_t allowed {LegacyDomains};
    uint32_t refs[Count] {};
    // Requests set by this object, versus requests found already set. Never
    // clear firmware/predecessor driver requests without an explicit handoff.
    uint32_t owned {}, borrowed {}, deferredTC {};
    uint32_t initialDriver {}, initialDDI {}, initialAUX {}, initialFuses {};
};
struct Lease {
    Lease() = default;
    Lease(const Lease &) = delete;
    Lease &operator=(const Lease &) = delete;
    State *owner {};
    uint32_t domains {};
};

template<class IO> bool valid(IO &io, uint32_t reg, uint32_t &value) {
    if (!io.readable(reg)) return false;
    value = io.read(reg);
    return value != UINT32_MAX;
}

// Read-only proof for a caller already holding leases. The same owner lock
// must cover this snapshot and the subsequent register operation. Hardware
// left on by firmware is not a logical reference; no acquisition or polling
// is performed here. TC ownership/health is a separate transport contract.
template<class IO> bool held(IO &io,const State &s,uint32_t domains) {
    if(!s.ready || s.faulted || s.suspended || !domains || (domains&~All))return false;
    domains=dependencies(domains);
    if(domains&~s.allowed)return false;
    uint32_t driverMask=0,ddiMask=0,auxMask=0,fuseMask=1U<<27;
    for(unsigned d=0;d<Count;++d)if(domains&(1U<<d)) {
        if(!s.refs[d] || !((s.owned|s.borrowed)&(1U<<d)))return false;
        const auto mask=request(d)|enabled(d);
        if(control(d)==DDI)ddiMask|=mask;
        else if(control(d)==AUX)auxMask|=mask;
        else {driverMask|=mask;fuseMask|=fuse(d);}
    }
    const uint32_t registers[]={Driver,DDI,AUX,Fuses},masks[]={driverMask,ddiMask,auxMask,fuseMask};
    for(unsigned i=0;i<4;++i)if(masks[i]) {
        uint32_t value;if(!valid(io,registers[i],value) || (value&masks[i])!=masks[i])return false;
    }
    return true;
}

// No writes and no adoption of pre-existing driver requests. BIOS/KVMR/debug
// ownership is left to the hardware and is never rewritten by this layer.
template<class IO> bool initialize(IO &io, State &s,uint32_t allowed=LegacyDomains) {
    if (s.ready || s.owned || !allowed || (allowed&~All) || dependencies(allowed)!=allowed) return false;
    uint32_t driver, ddi, aux, fuses;
    if (!valid(io, Driver, driver) || !valid(io, DDI, ddi) || !valid(io, AUX, aux) || !valid(io, Fuses, fuses)) return false;
    s = {};s.allowed=allowed;
    s.initialDriver = driver; s.initialDDI = ddi; s.initialAUX = aux; s.initialFuses = fuses;
    for (unsigned d = 0; d < Count; ++d)
        if ((allowed&(1U<<d)) && ((control(d)==DDI ? ddi : control(d)==AUX ? aux : driver) & request(d))) s.borrowed |= 1U << d;
    s.ready = true;
    return true;
}

template<class IO> Status wait(IO &io, uint32_t reg, uint32_t mask, unsigned timeoutUs) {
    for (unsigned elapsed = 0;; ++elapsed) {
        uint32_t value;
        if (!valid(io, reg, value)) return Status::Unavailable;
        if (value & mask) return Status::Complete;
        if (elapsed == timeoutUs) return reg == Fuses ? Status::FuseTimeout : Status::StateTimeout;
        io.delayUs(1);
    }
}

// Clear only our request; state may remain on for a BIOS/DMC/KVMR/debug
// requester. A posting read proves request release, not hardware shutdown.
template<class IO> Status clear(IO &io, State &s, unsigned d, Result &r) {
    const uint32_t mask = 1U << d, reg = control(d), req = request(d);
    if (!(s.owned & mask)) return Status::Complete;
    uint32_t value;
    if (!valid(io, reg, value)) return Status::Unavailable;
    if (value & req) { io.write(reg, value & ~req); ++r.writes; }
    if (!valid(io, reg, value)) return Status::Unavailable;
    if (value & req) return Status::ReleaseFailed;
    s.owned &= ~mask;
    if (value & enabled(d)) r.forcedOn |= mask;
    return Status::Complete;
}

// A TC AUX request alone is not a usable transport. The host must supply
// its ADL-P/N AUX control (legacy, not TBT) and serialized DKL uC-health IO.
// Existing Combo IO has no such contract and cannot silently enable TC AUX.
template<class IO> auto tcAuxSupported(IO &io,int) -> decltype(io.tcAuxSupported(),bool()) {return io.tcAuxSupported();}
template<class IO> bool tcAuxSupported(IO &,long) {return false;}
template<class IO> auto prepareTCAux(IO &io,unsigned tc,int) -> decltype(io.prepareTCAux(tc),bool()) {return io.prepareTCAux(tc);}
template<class IO> bool prepareTCAux(IO &,unsigned,long) {return false;}
template<class IO> auto tcAuxHealthy(IO &io,unsigned tc,int) -> decltype(io.tcAuxHealthy(tc),bool()) {return io.tcAuxHealthy(tc);}
template<class IO> bool tcAuxHealthy(IO &,unsigned,long) {return false;}
template<class IO> Status enableTCAux(IO &io,unsigned d) {
    if(!prepareTCAux(io,(d-12)/2,0))return Status::UnqualifiedTCAUX;
    return Status::Complete;
}
template<class IO> Status waitTCAuxHealth(IO &io,unsigned d) {
    for(unsigned us=0;;++us) {
        if(tcAuxHealthy(io,(d-12)/2,0))return Status::Complete;
        if(us==1000)return Status::AuxHealthTimeout;
        io.delayUs(1);
    }
}

// Isolation is opt-in for a caller that can propagate a failed acquisition.
// Native void getPower wrappers must retain controller-wide failure behavior.
template<class IO> Result acquire(IO &io, State &s, Lease &lease, uint32_t domains,bool isolateTC=false) {
    Result r;
    if (!s.ready || s.faulted || s.suspended) return r;
    if (lease.owner || !domains || (domains & ~s.allowed)) { r.status = Status::InvalidDomains; return r; }
    domains = dependencies(domains);
    for(unsigned d=0;d<Count;++d)if((domains&(1U<<d)) && tcAux(d) && !tcAuxSupported(io,0)) {
        r.status=Status::UnqualifiedTCAUX;r.domain=d;return r;
    }
    uint32_t value;
    // Preflight the entire transaction before its first write/reference.
    const uint32_t registers[] {Driver, DDI, AUX, Fuses};
    for (auto reg : registers)
        if (!valid(io, reg, value)) { r.status = Status::Unavailable; return r; }
    for (unsigned d = 0; d < Count; ++d)
        if ((domains & (1U << d)) && s.refs[d] == UINT32_MAX) {
            r.status = Status::Overflow; r.domain = d; return r;
        }
    const uint32_t previousOwned=s.owned;
    uint32_t added = 0;
    r.status = Status::Complete;
    for (unsigned d = 0; d < Count; ++d) {
        const uint32_t mask = 1U << d;
        if (!(domains & mask)) continue;
        r.domain = d;
        if (d == 0) {
            r.status = wait(io, Fuses, 1U << 27, FuseTimeoutUs); // PG0 before PW1.
            if (r.status != Status::Complete) break;
        }
        if (!valid(io, control(d), value)) { r.status = Status::Unavailable; break; }
        if (s.refs[d] && !(s.deferredTC & mask) && !(value & request(d))) { r.status = Status::RequestLost; break; }
        if(tcAux(d) && (!s.refs[d] || (s.deferredTC&mask))) {
            r.status=enableTCAux(io,d);if(r.status!=Status::Complete)break;
        }
        if (!(value & request(d))) {
            io.write(control(d), value | request(d)); ++r.writes;
            s.owned |= mask; added |= mask;
        } else if (!(s.owned & mask)) s.borrowed |= mask;
        if (!valid(io, control(d), value)) { r.status = Status::Unavailable; break; }
        if (!(value & request(d))) { r.status = Status::RequestLost; break; }
        // WA_14017248603 uses the ADL-P/N TC AUX 500ms enable budget.
        r.status = wait(io, control(d), enabled(d), tcAux(d)?500000:1000);
        if (r.status != Status::Complete) break;
        if(tcAux(d)) {
            r.status=waitTCAuxHealth(io,d);if(r.status!=Status::Complete)break;
        }
        if (d < 5) {
            r.status = wait(io, Fuses, fuse(d), FuseTimeoutUs);
            if (r.status != Status::Complete) break;
        }
    }
    if (r.status == Status::Complete) {
        for (unsigned d = 0; d < Count; ++d) if (domains & (1U << d)) ++s.refs[d];
        s.deferredTC &= ~domains;
        lease.owner = &s; lease.domains = domains;
        r.domain = Count;
        return r;
    }
    // A newly requested, unshared TC leaf may fail without invalidating other
    // outputs. Only isolate bounded ack/health timeouts after proving rollback
    // and every previously held scope. Lost/borrowed/shared resources, unknown
    // MMIO and failed rollback still poison the controller.
    const bool local=isolateTC && (tcDDI(r.domain)||tcAux(r.domain)) && !s.refs[r.domain] &&
        (added&(1U<<r.domain)) && !(s.borrowed&(1U<<r.domain)) &&
        (r.status==Status::StateTimeout || r.status==Status::AuxHealthTimeout);
    for (unsigned d = Count; d-- > 0;) {
        if (!(added & (1U << d))) continue;
        if (clear(io, s, d, r) != Status::Complete) {
            r.rollbackFailed = added & s.owned;
            break;
        }
    }
    uint32_t retained=0;
    for(unsigned d=0;d<Count;++d)if(s.refs[d])retained|=1U<<d;
    s.faulted=!local || r.rollbackFailed || s.owned!=previousOwned ||
        (retained && !held(io,s,retained));
    r.isolated=!s.faulted;
    return r;
}

template<class IO> Result release(IO &io, State &s, Lease &lease) {
    Result r;
    if (!s.ready || s.faulted || s.suspended) return r;
    if (lease.owner != &s || !lease.domains) { r.status = Status::Unbalanced; return r; }
    const auto domains = lease.domains;
    for (unsigned d = 0; d < Count; ++d) if ((domains & (1U << d)) && !s.refs[d]) {
        r.status = Status::Unbalanced; r.domain = d; return r;
    }
    for (unsigned d = Count; d-- > 0;) {
        if (!(domains & (1U << d))) continue;
        r.domain = d;
        if (s.refs[d] == 1) {
            r.status = clear(io, s, d, r);
            if (r.status != Status::Complete) { s.faulted = true; return r; }
        }
        if(!--s.refs[d])s.deferredTC &= ~(1U<<d);
        lease.domains &= ~(1U << d);
    }
    lease.owner = nullptr;
    r.status = Status::Complete; r.domain = Count;
    return r;
}
// Native override(false) temporarily removes physical requests without
// changing logical get/put counts. Caller proves all affected users and IRQs
// quiescent. Predecessor requests remain borrowed; only our requests are cut.
template<class IO> Result suspend(IO &io,State &s) {
    Result r;
    if(!s.ready || s.faulted)return r;
    if(s.suspended) {r.status=Status::Complete;return r;}
    uint32_t value;
    const uint32_t registers[] {Driver,DDI,AUX,Fuses};
    for(auto reg : registers)if(!valid(io,reg,value)) {
        r.status=Status::Unavailable;return r;
    }
    for(unsigned d=0;d<Count;++d)if(s.owned&(1U<<d)) {
        if(!valid(io,control(d),value) || !(value&request(d))) {
            r.status=Status::RequestLost;r.domain=d;s.faulted=true;return r;
        }
    }
    for(unsigned d=Count;d-- >0;) {
        r.domain=d;
        r.status=clear(io,s,d,r);
        if(r.status!=Status::Complete) {s.faulted=true;return r;}
    }
    s.suspended=true;r.status=Status::Complete;r.domain=Count;return r;
}
// Restore the current held scopes, in dependency order, without duplicate
// gets. A request lost during power collapse is acquired anew and becomes
// ours; a surviving borrowed request is never adopted merely by this call.
template<class IO,class PrepareDDIA> Result restore(IO &io,State &s,bool deferTC,PrepareDDIA prepareDDIA) {
    Result r;
    if(!s.ready || s.faulted)return r;
    uint32_t value;
    const uint32_t registers[] {Driver,DDI,AUX,Fuses};
    for(auto reg : registers)if(!valid(io,reg,value)) {
        r.status=Status::Unavailable;return r;
    }
    for(unsigned d=0;d<Count;++d)if(s.refs[d] && !deferTC && tcAux(d) && !tcAuxSupported(io,0)) {
        r.status=Status::UnqualifiedTCAUX;r.domain=d;return r;
    }
    r.status=Status::Complete;
    for(unsigned d=0;d<Count;++d)if(s.refs[d]) {
        // A parked internal-only pipe needs no Type-C lane or AUX engine.
        // A Type-C client reacquires its leaf after its port mode is ready.
        if(deferTC && (tcDDI(d) || tcAux(d))) {
            if(!valid(io,control(d),value)){r.status=Status::Unavailable;break;}
            if(!(value&request(d))) {
                s.deferredTC|=1U<<d;s.owned&=~(1U<<d);s.borrowed&=~(1U<<d);
            }
            continue;
        }
        r.domain=d;
        if(!d)r.status=wait(io,Fuses,1U<<27,FuseTimeoutUs);
        if(r.status!=Status::Complete)break;
        if(!valid(io,control(d),value)) {r.status=Status::Unavailable;break;}
        if(d==unsigned(Domain::DDIA) && !prepareDDIA(value)) {r.status=Status::Unavailable;break;}
        if(tcAux(d)) {r.status=enableTCAux(io,d);if(r.status!=Status::Complete)break;}
        if(!(value&request(d))) {
            if(!s.suspended) {r.status=Status::RequestLost;break;}
            io.write(control(d),value|request(d));++r.writes;s.owned|=1U<<d;
        }
        if(!valid(io,control(d),value)) {r.status=Status::Unavailable;break;}
        if(!(value&request(d))) {r.status=Status::RequestLost;break;}
        r.status=wait(io,control(d),enabled(d),tcAux(d)?500000:1000);
        if(r.status==Status::Complete && tcAux(d))r.status=waitTCAuxHealth(io,d);
        if(r.status!=Status::Complete)break;
        if(d<5)r.status=wait(io,Fuses,fuse(d),FuseTimeoutUs);
        if(r.status!=Status::Complete)break;
    }
    if(r.status==Status::Complete) {s.suspended=false;r.domain=Count;}
    else s.faulted=true; // Keep partial restoration and its prerequisites.
    return r;
}
template<class IO> Result restore(IO &io,State &s,bool deferTC=false) {
    return restore(io,s,deferTC,[](uint32_t){return true;});
}
}
#endif
