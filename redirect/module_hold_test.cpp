// Native Windows, game-free: one thread loads and frees a DLL in a loop while this one walks toolhelp module
// snapshots and reads every listed image's headers and import slots through with_module_held, the way patch_all
// does. Unheld (MUTANT_MODULE_UNHELD), a listed base is read after its image was unmapped and the read faults.
// The same source builds the DLL that is loaded and freed (-DHOLD_TEST_LIBRARY).
#ifdef HOLD_TEST_LIBRARY
extern "C" __declspec(dllexport) int module_hold_probe() {return 1;}
#else
#include "module_hold.h"
#include <tlhelp32.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

static LONG CALLBACK on_fault(EXCEPTION_POINTERS* e) {
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        // No CRT stream and no loader call here: the other thread may hold either lock.
        static const char text[]="module hold: access violation reading a listed module\n";
        DWORD n=0;WriteFile(GetStdHandle(STD_ERROR_HANDLE),text,sizeof(text)-1,&n,nullptr);
        TerminateProcess(GetCurrentProcess(),3);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// What patch() reads of a module: both headers, the import descriptors and every import slot.
static size_t read_imports(HMODULE module,bool& image) {
    auto b=reinterpret_cast<const BYTE*>(module);
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(b);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) {image=false;return 0;}
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(b+dos->e_lfanew);
    auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!dir.VirtualAddress) return 0;
    size_t slots=0;
    for(auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(b+dir.VirtualAddress);d->Name;++d)
        for(auto iat=reinterpret_cast<const IMAGE_THUNK_DATA64*>(b+d->FirstThunk);iat->u1.Function;++iat) ++slots;
    return slots;
}

static std::atomic<bool> stop{false};
static std::atomic<unsigned> cycles{0},load_failures{0};
static std::wstring library;

static DWORD WINAPI load_and_free(LPVOID) {
    while(!stop) {
        HMODULE m=LoadLibraryW(library.c_str());
        if(!m) {++load_failures;break;}
        FreeLibrary(m);++cycles;
    }
    return 0;
}

int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--interop-check")) return 0;
    AddVectoredExceptionHandler(1,on_fault);
    wchar_t self[32768];
    DWORD n=GetModuleFileNameW(nullptr,self,32768);
    if(!n||n>=32768) {std::fputs("module hold: setup failed (own path)\n",stderr);return 2;}
    library=self;library.resize(library.find_last_of(L'\\')+1);library+=L"module_hold_test_module.dll";
    HMODULE once=LoadLibraryW(library.c_str());
    if(!once) {std::fprintf(stderr,"module hold: setup failed (test library, error %lu)\n",GetLastError());return 2;}
    FreeLibrary(once);
    if(GetModuleHandleW(library.c_str())) {std::fputs("module hold: setup failed (the test library stays loaded)\n",stderr);return 2;}
    HANDLE worker=CreateThread(nullptr,0,load_and_free,nullptr,0,nullptr);
    if(!worker) {std::fputs("module hold: setup failed (thread)\n",stderr);return 2;}
    // The run ends when the race has been seen `wanted` times: a snapshot listed a module that was gone before the
    // read. Without one such case the test proves nothing, so the time limit is a failure, not a pass.
    const unsigned wanted=20;
    const ULONGLONG limit=GetTickCount64()+30000;
    unsigned snapshots=0,gone=0;size_t slots=0;bool image=true;DWORD snapshot_error=0;
    while(gone<wanted && image && !load_failures && GetTickCount64()<limit) {
        HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        if(snap==INVALID_HANDLE_VALUE) {
            if(GetLastError()==ERROR_BAD_LENGTH) continue; // the documented race with a concurrent load; patch_all retries it too
            snapshot_error=GetLastError();break;
        }
        ++snapshots;
        MODULEENTRY32W m{};m.dwSize=sizeof m;
        if(Module32FirstW(snap,&m)) do {
            if(!redirect::with_module_held(m.hModule,[&](HMODULE module){slots+=read_imports(module,image);})) ++gone;
        } while(Module32NextW(snap,&m));
        CloseHandle(snap);
    }
    stop=true;WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    if(snapshot_error) {std::fprintf(stderr,"module hold: snapshot failed (error %lu)\n",snapshot_error);return 2;}
    if(load_failures) {std::fputs("module hold: the test library stopped loading\n",stderr);return 2;}
    if(!image) {std::fputs("module hold: a listed base no longer holds an image\n",stderr);return 4;}
    if(gone<wanted) {
        std::fprintf(stderr,"module hold: only %u of %u listed modules went away before the read in %u snapshots (%u load/free cycles)\n",
            gone,wanted,snapshots,cycles.load());
        return 1;
    }
    std::printf("module hold ok: %u snapshots, %u listed modules gone before the read, %u load/free cycles, %zu import slots read\n",
        snapshots,gone,cycles.load(),slots);
    return 0;
}
#endif
