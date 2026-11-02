#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include "redirect.h"
BOOL WINAPI DllMain(HINSTANCE self,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        // Synchronous redirection only. Mod startup happens after loader return.
        try {install_redirect(self);} catch(...) {OutputDebugStringA("redirect: initialization exception");TerminateProcess(GetCurrentProcess(),125);return FALSE;}
    }
    if(reason==DLL_PROCESS_DETACH) flush_redirect_log();
    return TRUE;
}
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
