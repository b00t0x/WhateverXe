// Legacy/DP-alt TC lifetime. The host serializes native clients and proves
// firmware adoption/quiescence; this core never executes the TGL FIA setter.
#ifndef KERN_DISPLAY13_TC_MODE_HPP
#define KERN_DISPLAY13_TC_MODE_HPP
#include "kern_display13_tc.hpp"
#include "kern_display13_power_bridge.hpp"
namespace Display13TC {
struct Mode {
    enum class Phase : uint32_t {Idle,Active,Cleanup};
    struct Change {
        Status status {Status::Invalid};Result phy;
        Display13Power::Result power;uint32_t native {};
    };
    uintptr_t controller {},object {};unsigned ddi {};
    Phase phase {Phase::Idle};bool disconnectPending {};
    Ownership ownership;
    unsigned selectedMode {};Lanes lanes;
    // ADLP: register/lane access -> ownership -> AUX cold block.
    // DDI IO belongs to the clock lifecycle, after the PLL is running.
    // An already-powered firmware link is not evidence that these scopes can
    // be requested together on an unowned, cold PHY.
    Display13Power::Lease accessLease,auxLease;
    static uint32_t accessDomains() {return Display13Power::bit(Display13Power::Domain::PW2);}
    Mode()=default;Mode(const Mode&)=delete;Mode& operator=(const Mode&)=delete;
    static uint32_t domains(unsigned d) {
        Port p;return Port::resolve(d,p) ? Display13Power::ddiDomain(d)|
            Display13Power::auxDomain(d)|Display13Power::bit(Display13Power::Domain::PW2) : 0;
    }
    template<class IO> Change connect(IO &io,uintptr_t owner,uintptr_t identity,unsigned d,unsigned wanted=4) {
        Change r;
        if(!owner || !identity || !domains(d) || (wanted!=3 && wanted!=4))return r;
        if(controller && (owner!=controller || identity!=object || d!=ddi)){r.status=Status::InUse;return r;}
        if(!io.admissionReady()){r.status=Status::NotReady;return r;}
        if(phase==Phase::Cleanup){r.status=Status::InUse;return r;}
        if(phase==Phase::Active) {
            if(wanted!=selectedMode || io.nativeMode()!=selectedMode ||
               (wanted==3 && !lanes.unchanged(io,ownership.port))){r.status=Status::Readback;return r;}
            r.phy=ownership.acquire(io,owner,identity,d,false,wanted);
            r.status=r.phy.status;if(r.status==Status::Complete)disconnectPending=false;
            return r;
        }
        if(io.nativeMode()!=0){r.status=Status::NeedsAdoption;return r;}
        // Keep failed acquisitions bound, including their exact power/PHY
        // cleanup obligations. Never replace them with another native object.
        controller=owner;object=identity;ddi=d;selectedMode=wanted;phase=Phase::Cleanup;
        r.power=io.acquireDomains(accessLease,accessDomains());
        if(r.power.status!=Display13Power::Status::Complete){r.status=Status::NoPower;return r;}
        const bool firmware=io.inheritedVerified(owner,identity,d);
        r.phy=ownership.acquire(io,owner,identity,d,firmware,wanted);
        if(r.phy.status!=Status::Complete){r.status=r.phy.status;return r;}
        r.power=io.acquireDomains(auxLease,Display13Power::auxDomain(d));
        if(r.power.status!=Display13Power::Status::Complete){r.status=Status::NoPower;return r;}
        if(wanted==3) {
            lanes=Lanes::capture(io,ownership.port);
            if(!lanes.valid()){r.status=Status::NotReady;return r;}
        }
        r.native=io.nativeSetMode(wanted);
        if(r.native || io.nativeMode()!=wanted){r.status=Status::Readback;return r;}
        phase=Phase::Active;disconnectPending=false;r.status=Status::Complete;return r;
    }
    template<class IO> Change disconnect(IO &io,uintptr_t owner,uintptr_t identity,unsigned d) {
        Change r;
        if(!owner || !identity || !domains(d))return r;
        if(!controller){r.status=io.nativeMode()==0 ? Status::Complete : Status::NeedsAdoption;return r;}
        if(owner!=controller || identity!=object || d!=ddi){r.status=Status::InUse;return r;}
        disconnectPending=true;
        if(!io.quiescent(owner,identity,d)){r.status=Status::InUse;return r;}
        phase=Phase::Cleanup;
        if(io.nativeMode()!=0) {
            r.native=io.nativeSetMode(0);
            if(r.native || io.nativeMode()!=0){r.status=Status::Readback;return r;}
        }
        if(auxLease.owner) {
            r.power=io.releaseDomains(auxLease);
            if(r.power.status!=Display13Power::Status::Complete){r.status=Status::NoPower;return r;}
        }
        if(ownership.owner) {
            r.phy=ownership.release(io,owner,identity,d);
            if(r.phy.status!=Status::Complete){r.status=r.phy.status;return r;}
        }
        if(accessLease.owner) {
            r.power=io.releaseDomains(accessLease);
            if(r.power.status!=Display13Power::Status::Complete){r.status=Status::NoPower;return r;}
        }
        controller=object=0;ddi=selectedMode=0;lanes={};phase=Phase::Idle;disconnectPending=false;r.status=Status::Complete;return r;
    }
};
}
#endif
