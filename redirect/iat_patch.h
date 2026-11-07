#pragma once
// Import-table writes, serialised across threads.
//
// VirtualProtect works per page. Two threads that each make a slot writable, write it and put the old protection
// back can interleave so that one thread's restore lands between the other's protect and write: the write then
// faults. Live 2026-11-07: a text-services thread (MSCTF) loaded ApplicationTargetedFeatureDatabase.dll while another
// thread patched modules; the proxy faulted on the import write (access violation, game crash dialog). Every
// import-table write goes through write_import, which holds one process-wide lock for the whole sequence.
#include <windows.h>

namespace redirect {

enum class IatWrite {ok, protect_failed, write_failed, restore_failed};

inline SRWLOCK& iat_lock() {static SRWLOCK lock=SRWLOCK_INIT;return lock;}

inline IatWrite write_import(ULONG_PTR* slot,ULONG_PTR value) {
#ifndef MUTANT_IAT_UNLOCKED
    AcquireSRWLockExclusive(&iat_lock());
#endif
    IatWrite result=IatWrite::ok;
    DWORD old=0,ignored=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)) result=IatWrite::protect_failed;
    else {
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(slot),reinterpret_cast<PVOID>(value));
        if(*slot!=value) result=IatWrite::write_failed;
        if(!VirtualProtect(slot,sizeof(void*),old,&ignored) && result==IatWrite::ok) result=IatWrite::restore_failed;
    }
#ifndef MUTANT_IAT_UNLOCKED
    ReleaseSRWLockExclusive(&iat_lock());
#endif
    return result;
}

}
