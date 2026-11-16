// Exact TGL donor layout, with single-pass reset and bounded BCS completion.
// The caller must qualify the binary and select host scheduler 5 first.
#ifndef kern_tgl_recovery_hpp
#define kern_tgl_recovery_hpp
#include <stdint.h>

namespace TGLRecovery {
template<typename T> inline T &field(void *p, unsigned offset) {
	return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+offset);
}

using SubmitStamp=void (*)(void *);
using WaitStamp=bool (*)(void *,uint32_t *,uint32_t,uint32_t);
struct StampOps { SubmitStamp submit; WaitStamp wait; };
enum class StampStatus : uint8_t { NotRun, Complete, TimedOut };

struct Result {
	uintptr_t ring, context, affected;
	uint32_t index, failuresBefore, failuresAfter;
	bool resetOK;
	uint8_t engine;
	StampStatus stamp;
	uint32_t targetStamp, observedStamp;
};

// Keep the ICL single-pass contract for a failed reset and non-BCS engines.
// On successful Blit reset, replay the donor's bounded completion sequence:
// resume, increment the affected event stamp, submit and wait once. A timeout
// accounts a failure and returns; never retry under IOAccel's recovery locks.
inline Result resetAndResume(void *channel,const StampOps &stamp) {
	void *ring=field<void *>(channel,0x130);
	void *context=field<void *>(ring,0x20);
	void *affected=field<void *>(context,0xB8); // ICL uses B0, NOT compatible.
	Result result {reinterpret_cast<uintptr_t>(ring),reinterpret_cast<uintptr_t>(context),
		reinterpret_cast<uintptr_t>(affected),affected ? field<uint32_t>(affected,0x20) : 0,
		affected ? field<uint32_t>(affected,0x17C) : 0,0,false,0,StampStatus::NotRun,0,0};
	using Reset=bool (*)(void *,void *);
	result.resetOK=field<Reset>(field<void *>(ring,0),0x168)(ring,context);
	result.engine=field<uint8_t>(ring,0x40); // Donor's Blit ring type is 2.
	if (affected) {
		if (!result.resetOK) ++field<uint32_t>(affected,0x17C);
		result.failuresAfter=field<uint32_t>(affected,0x17C);
	}
	// Reload after reset, as the native ICL caller does.
	void *owner=field<void *>(channel,0x18);
	void *scheduler=field<void *>(owner,0x1250); // ICL uses1280.
	using Resume=void (*)(void *);
	field<Resume>(field<void *>(scheduler,0),0x180)(scheduler);
	if (result.resetOK && result.engine==2 && affected) {
		// Native resume can change owner state; reload exactly as the TGL caller.
		owner=field<void *>(channel,0x18);
		void *events=field<void *>(owner,0x380);
		using IncrementStamp=void (*)(void *,int);
		field<IncrementStamp>(field<void *>(events,0),0x1D8)(events,field<int>(affected,0x20));
		stamp.submit(affected);
		result.targetStamp=field<uint32_t>(affected,0x178);
		// Donor sleepForStamp returns true for timeout, false if stamp reached.
		const bool timedOut=stamp.wait(ring,&result.observedStamp,result.targetStamp,1000000);
		result.stamp=timedOut ? StampStatus::TimedOut : StampStatus::Complete;
		if (timedOut) ++field<uint32_t>(affected,0x17C);
		result.failuresAfter=field<uint32_t>(affected,0x17C);
	}
	return result;
}
}
#endif
