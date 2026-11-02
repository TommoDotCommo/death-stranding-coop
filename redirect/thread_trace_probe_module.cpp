#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
// A real late DLL import table, rather than LoadLibrary(EXE) or a dynamic API
// lookup. These imports must remain native when only the main EXE is observed.
extern "C" __declspec(dllexport) int probe_message_imports() {
    MSG msg{};PeekMessageA(&msg,nullptr,0,0,PM_NOREMOVE);
    return GetMessageA(&msg,reinterpret_cast<HWND>(uintptr_t(0x1234)),0,0)==-1;
}
