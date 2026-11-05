// Exact TGL donor layout, with the native ICL single-pass recovery contract.
// The caller must qualify the binary and select host scheduler 5 first.
#ifndef kern_tgl_recovery_hpp
#define kern_tgl_recovery_hpp
#include <stdint.h>

namespace TGLRecovery {
template<typename T> inline T &field(void *p, unsigned offset) {
	return *reinterpret_cast<T *>(static_cast<uint8_t *>(p)+offset);
}

struct Result {
	uintptr_t ring, context, affected;
	uint32_t index, failuresBefore, failuresAfter;
	bool resetOK;
};

// ICL resetHardwareAndReplay: reset once, account a native
// failure, resume scheduling, return to IOAccel's error/event processing.
// TGL's lower reset still owns hardware reset, context replay and CSB handling.
// Do not append a new stamp or wait inside this recovery work-loop callback.
inline Result resetAndResume(void *channel) {
	void *ring=field<void *>(channel,0x130);
	void *context=field<void *>(ring,0x20);
	void *affected=field<void *>(context,0xB8); // ICL uses B0, NOT compatible.
	Result result {reinterpret_cast<uintptr_t>(ring),reinterpret_cast<uintptr_t>(context),
		reinterpret_cast<uintptr_t>(affected),field<uint32_t>(affected,0x20),
		field<uint32_t>(affected,0x17C),0,false};
	using Reset=bool (*)(void *,void *);
	result.resetOK=field<Reset>(field<void *>(ring,0),0x168)(ring,context);
	if (!result.resetOK) ++field<uint32_t>(affected,0x17C);
	result.failuresAfter=field<uint32_t>(affected,0x17C);
	// Reload after reset, as the native ICL caller does.
	void *owner=field<void *>(channel,0x18);
	void *scheduler=field<void *>(owner,0x1250); // ICL uses1280.
	using Resume=void (*)(void *);
	field<Resume>(field<void *>(scheduler,0),0x180)(scheduler);
	return result;
}
}
#endif
