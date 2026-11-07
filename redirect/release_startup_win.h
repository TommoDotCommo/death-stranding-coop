#pragma once
#include "startup_policy.h"
inline bool release_ticket_selected=false;

inline bool release_passthrough() {
    const char* line="Death Stranding co-op: off (no valid launcher session); system DLL pass-through.\n";
    OutputDebugStringA(line);
    DWORD written;auto err=GetStdHandle(STD_ERROR_HANDLE);
    if(err&&err!=INVALID_HANDLE_VALUE)WriteFile(err,line,DWORD(strlen(line)),&written,nullptr);
    return false;
}

// Called before installing any hooks. No file is created on the pass-through path.
inline bool release_redirect_enabled() {
#ifndef COOP_PROXY_AVRT
    return true; // Keep the environment-driven lab proxy fail-closed.
#else
    bool explicitEnvironment=false;
    for(auto key:release_startup::Keys) {
        auto k=release_startup::wide(key);
        SetLastError(ERROR_SUCCESS);
        if(GetEnvironmentVariableW(k.c_str(),nullptr,0)||GetLastError()!=ERROR_ENVVAR_NOT_FOUND)explicitEnvironment=true;
    }
    if(explicitEnvironment)return true;
    release_startup::Values ticket;
    std::wstring file;
    bool valid=false;
    try {
        wchar_t local[32768],exe[32768];
        auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        auto e=GetModuleFileNameW(nullptr,exe,32768);
        if(n&&n<32768&&e&&e<32768) {
            file=std::wstring(local)+L"\\DeathStrandingCoop\\launcher-session.ini";
            HANDLE f=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE) {
                char bytes[16385];DWORD count=0;bool ok=ReadFile(f,bytes,sizeof(bytes),&count,nullptr)!=FALSE;CloseHandle(f);
                if(ok&&count<sizeof(bytes)) {
                    ticket=release_startup::parse(std::string(bytes,count));
                    FILETIME ft;GetSystemTimeAsFileTime(&ft);
                    uint64_t seconds=((uint64_t(ft.dwHighDateTime)<<32)|ft.dwLowDateTime)/10000000-11644473600ULL;
                    valid=release_startup::valid(ticket,exe,seconds);
                }
            }
        }
    } catch(...) {valid=false;}
    if(release_startup::decide(true,false,valid)==release_startup::Mode::passthrough) {
        return release_passthrough();
    }
    // A ticket is single-use. If consumption fails, remain inert rather than apply a partial setup.
    if(!valid||!DeleteFileW(file.c_str())) {
#ifdef MUTANT_MISSING_CONFIG_CLOSES
        return true;
#else
        return release_passthrough();
#endif
    }
    for(auto key:release_startup::Keys) {
        auto k=release_startup::wide(key),v=release_startup::wide(ticket.at(key));
        if(!SetEnvironmentVariableW(k.c_str(),v.c_str()))return release_passthrough();
    }
    release_ticket_selected=true;
    return true;
#endif
}
