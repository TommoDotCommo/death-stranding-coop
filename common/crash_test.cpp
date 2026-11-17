// Native Windows subprocess fixtures; never loads the game.
#include "crash_handler.h"
#include "../redirect/crash_record.h"
#include <fstream>
#include <iostream>
namespace {
diag::Session* active=nullptr;
std::wstring chainPath;
constexpr DWORD TestException=0xe1230011;
void Mark(){active->MarkCrashed();}
HMODULE Load(wchar_t*,size_t) {
    wchar_t path[MAX_PATH];auto n=GetSystemDirectoryW(path,MAX_PATH);
    if(!n||n>MAX_PATH-20)return nullptr;
    std::wcscat(path,L"\\dbghelp.dll");return LoadLibraryW(path);
}
LONG WINAPI Chained(EXCEPTION_POINTERS*) {
    HANDLE f=CreateFileW(chainPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
    return EXCEPTION_EXECUTE_HANDLER;
}
LONG CALLBACK Handled(EXCEPTION_POINTERS* e){return e->ExceptionRecord->ExceptionCode==TestException?EXCEPTION_CONTINUE_EXECUTION:EXCEPTION_CONTINUE_SEARCH;}
void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int Child(const diag::fs::path& dir,const std::wstring& mode) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    diag::Session log;active=&log;
    bool proxy=mode.find(L"proxy")==0;
    Require(log.Open(dir,proxy?"proxy":"launcher",5),"child log");
    chainPath=(dir/L"chained.txt").wstring();
    SetUnhandledExceptionFilter(Chained);
    if(proxy){redirect::crash::Options o;o.folder=dir.c_str();o.stem=L"legacy";o.load_dbghelp=Load;o.player_stem=log.Base().c_str();o.on_unhandled=Mark;
        Require(redirect::crash::install(o),"proxy install");
        redirect::crash::hook_SetUnhandledExceptionFilter(Chained);
        for(unsigned i=0;i<200&&!redirect::crash::dump_ready();++i)Sleep(25);
        Require(redirect::crash::dump_ready(),"proxy dump ready");
    }else Require(diag::crash::Install(log),"shared dump ready");
    if(mode.find(L"handled")!=std::wstring::npos){
        AddVectoredExceptionHandler(1,Handled);RaiseException(TestException,0,0,nullptr);log.Close();return 0;
    }
    RaiseException(TestException,EXCEPTION_NONCONTINUABLE,0,nullptr);return 99;
}
void Run(const diag::fs::path& exe,const diag::fs::path& dir,const wchar_t* mode) {
    diag::fs::create_directories(dir);
    std::wstring command=L"\""+exe.wstring()+L"\" --child \""+dir.wstring()+L"\" "+mode;
    STARTUPINFOW si{};si.cb=sizeof si;PROCESS_INFORMATION pi{};
    Require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi),"create child");
    auto waited=WaitForSingleObject(pi.hProcess,10000);DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);
    if(waited!=WAIT_OBJECT_0)TerminateProcess(pi.hProcess,123);
    CloseHandle(pi.hThread);CloseHandle(pi.hProcess);Require(waited==WAIT_OBJECT_0,"bounded crash recorder");
    bool handled=std::wstring(mode).find(L"handled")!=std::wstring::npos;
    Require(handled?code==0:code!=0,"exception outcome");
    unsigned markers=0,dumps=0,logs=0;
    for(auto& f:diag::fs::directory_iterator(dir)) {
        auto ext=f.path().extension();
        if(ext==L".crash")++markers;
        if(ext==L".dmp"){
            ++dumps;std::ifstream in(f.path(),std::ios::binary);MINIDUMP_HEADER header{};in.read(reinterpret_cast<char*>(&header),sizeof header);
            Require(in.good()&&header.Signature==MINIDUMP_SIGNATURE&&header.Flags==MiniDumpNormal,"small MiniDumpNormal");
            Require(diag::fs::exists(diag::fs::path(f.path()).replace_extension(L".crash")),"dump shares marker stem");
        }
        if(ext==L".log"&&f.path().filename()!=L"legacy.log"){
            ++logs;std::ifstream in(f.path());std::string text((std::istreambuf_iterator<char>(in)),{});
            Require((text.find("session end: clean exit")!=std::string::npos)==handled,"clean vs crashed log");
        }
    }
    Require(logs==1&&markers==unsigned(!handled)&&dumps==unsigned(!handled),"unhandled-only marker and dump");
    Require(diag::fs::exists(dir/L"chained.txt")==!handled,"previous filter called");
}
}
int wmain(int argc,wchar_t** argv)try {
    if(argc==2&&std::wstring(argv[1])==L"--interop-check")return 0;
    if(argc==4&&std::wstring(argv[1])==L"--child")return Child(argv[2],argv[3]);
    wchar_t image[32768];Require(GetModuleFileNameW(nullptr,image,32768)>0,"exe path");
    auto root=diag::fs::temp_directory_path()/(L"ds-crash-fixture-"+std::to_wstring(GetCurrentProcessId()));
    diag::fs::remove_all(root);
    for(auto mode:{L"shared-handled",L"shared-crash",L"proxy-handled",L"proxy-crash"})Run(image,root/mode,mode);
    diag::fs::remove_all(root);std::cout<<"Windows crash oracles passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"oracle: "<<e.what()<<'\n';return 1;}
