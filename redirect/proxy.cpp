#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include "redirect.h"
static volatile bool proxy_ready=false; // set once install_redirect has returned inside DllMain
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        // Synchronous redirection only. Mod startup happens after loader return.
        try {install_redirect(self);} catch(...) {OutputDebugStringA("redirect: initialization exception");TerminateProcess(GetCurrentProcess(),125);return FALSE;}
        proxy_ready=true;
    }
    if(reason==DLL_PROCESS_DETACH) flush_redirect_log();
    return TRUE;
}
#ifdef COOP_PROXY_AVRT
// Proxy named avrt.dll: a system DLL the game imports (one wrapper around AvSetMmThreadCharacteristicsA, called by its
// audio render thread) but does not ship, so installing it replaces and renames nothing. Every export of the real
// System32\avrt.dll is forwarded by a generated jump stub (avrt_stubs.inc); the real DLL is loaded by full path.
// The first call of any export, after the loader has returned, starts the optional mod.
#include "avrt_stubs.inc"
extern "C" void* coop_fwd_table[COOP_PROXY_EXPORTS];
extern "C" void* coop_resolve(unsigned index) {
    if(index>=COOP_PROXY_EXPORTS) TerminateProcess(GetCurrentProcess(),125);
#ifdef MUTANT_FORWARD_BROKEN
    // Mutant: every export is forwarded to the wrong function (a no-op returning 0).
    struct Nop {static DWORD WINAPI f() {return 0;}};
    coop_fwd_table[index]=reinterpret_cast<void*>(&Nop::f);
    return coop_fwd_table[index];
#endif
    // Not inside the loader (a system DLL calling during its own initialisation): do not start the mod from there.
    if(proxy_ready) start_instance_mod();
    void* target=original_system_export(COOP_PROXY_DLL,coop_export_names[index]);
    coop_fwd_table[index]=target;
    return target;
}
#else
// The two pinned static game imports provide a post-loader bootstrap. No game
// tick hook is claimed. Other compiler exports retain their original forwarders.
extern "C" HRESULT WINAPI proxy_D3DCreateBlob(SIZE_T size, ID3DBlob** blob) {
    start_instance_mod();
    using F=HRESULT(WINAPI*)(SIZE_T,ID3DBlob**);
    return reinterpret_cast<F>(original_compiler_export("D3DCreateBlob"))(size,blob);
}
extern "C" HRESULT WINAPI proxy_D3DReflect(LPCVOID data,SIZE_T size,REFIID iid,void** reflector) {
    start_instance_mod();
    using F=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
    return reinterpret_cast<F>(original_compiler_export("D3DReflect"))(data,size,iid,reflector);
}
#endif
