#pragma once
// A module taken from a toolhelp snapshot, kept loaded while it is read.
//
// The snapshot lists base addresses, not references. A module that another thread unloads after the snapshot is
// unmapped by the time its header is read. Live 2026-11-13 (second PC): access violation reading the DOS header of
// a base that was no longer in the module list; the main thread was patching after a LoadLibraryA from dxgi while
// another thread loaded and released modules, and the game ended about ten minutes into the session. Every walk
// over snapshot entries reads a module through with_module_held, which takes a loader reference for the read.
#include <windows.h>

namespace redirect {

// use(module) with the image held loaded. False, and no call, when no image starts at `module` any more (unloaded
// since the snapshot, or the range now belongs to another image). The reference is released before returning; if
// it was the last one, the module's own unload runs on this thread.
template<class Use> bool with_module_held(HMODULE module,Use&& use) {
#ifdef MUTANT_MODULE_UNHELD
    use(module);return true; // the old behaviour: the snapshot's address is trusted
#else
    HMODULE held=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(module),&held) || held!=module) {
        if(held) FreeLibrary(held);
        return false;
    }
    use(module);
    FreeLibrary(held);
    return true;
#endif
}

}
