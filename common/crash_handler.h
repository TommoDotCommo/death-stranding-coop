#pragma once
// Small, chained UNHANDLED-exception recorder. No first-chance handler, heap,
// loader work or log locks in the exception path. A prestarted thread writes
// MiniDumpNormal; the faulting thread waits at most two seconds.
#include "diag_log.h"
#ifdef _WIN32
#include <dbghelp.h>
namespace diag::crash {
using DumpFn=BOOL(WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
inline Session* session=nullptr;
inline std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> previous{nullptr};
inline DumpFn dump=nullptr;
inline HANDLE request=nullptr,done=nullptr;
inline wchar_t dumpPath[32768]{};
inline EXCEPTION_RECORD record{};inline CONTEXT context{};
inline EXCEPTION_POINTERS pointers{&record,&context};
inline DWORD faultThread=0;inline LONG crashing=0;
inline DWORD WINAPI Worker(void*) {
    if(WaitForSingleObject(request,INFINITE)!=WAIT_OBJECT_0)return 1;
    HANDLE f=CreateFileW(dumpPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION ei{faultThread,&pointers,FALSE};
        if(dump)dump(GetCurrentProcess(),GetCurrentProcessId(),f,
#ifdef MUTANT_DIAG_CRASH_FULL
            MiniDumpWithFullMemory,
#else
            MiniDumpNormal,
#endif
            &ei,nullptr,nullptr);
        CloseHandle(f);}
    SetEvent(done);return 0;
}
inline LONG WINAPI Filter(EXCEPTION_POINTERS* ep) {
#ifndef MUTANT_DIAG_CRASH_OFF
    if(!InterlockedExchange(&crashing,1)) {
        if(session)session->MarkCrashed();
        if(ep&&ep->ExceptionRecord&&ep->ContextRecord&&request) {
            record=*ep->ExceptionRecord;record.ExceptionRecord=nullptr;context=*ep->ContextRecord;faultThread=GetCurrentThreadId();
            SetEvent(request);WaitForSingleObject(done,2000);
        }
    }
#endif
    auto next=previous.load();
#ifndef MUTANT_DIAG_CRASH_CHAIN
    if(next&&next!=Filter)return next(ep);
#else
    (void)next;
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}
inline bool Install(Session& log) {
    session=&log;
    auto p=fs::path(log.Base()).concat(".dmp").wstring();if(p.size()>=32768)return false;
    std::wcscpy(dumpPath,p.c_str());
    wchar_t system[32768];auto n=GetSystemDirectoryW(system,32768);
    if(n&&n<32750){std::wcscat(system,L"\\dbghelp.dll");auto module=LoadLibraryW(system);
        if(module)dump=reinterpret_cast<DumpFn>(reinterpret_cast<void*>(GetProcAddress(module,"MiniDumpWriteDump")));}
    request=CreateEventW(nullptr,FALSE,FALSE,nullptr);done=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    HANDLE worker=request&&done?CreateThread(nullptr,0,Worker,nullptr,0,nullptr):nullptr;
    if(worker)CloseHandle(worker);
    previous=SetUnhandledExceptionFilter(Filter);
    log.Line(dump&&worker?"crash recorder: MiniDumpNormal ready":"crash recorder: marker only; dump writer unavailable");
    return dump&&worker;
}
}
#else
namespace diag::crash {inline bool Install(Session&){return false;}}
#endif
