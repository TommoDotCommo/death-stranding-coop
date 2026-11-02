#include "mod.h"
#include "timing.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
int main(int argc,char** argv) {
    if(argc!=4) {std::cerr<<"usage: mod_probe ABS_DLL ABS_CONFIG SECONDS\n";return 2;}
    try {
        double seconds=std::stod(argv[3]);if(!std::isfinite(seconds)||seconds<2||seconds>120) return 2;
#ifdef _WIN32
        // argv paths are deliberately ASCII, as with redirect's path policy.
        auto module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto symbol=[&](const char* name){return module?reinterpret_cast<void*>(GetProcAddress(module,name)):nullptr;};
#else
        auto module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);auto symbol=[&](const char* name){return module?dlsym(module,name):nullptr;};
#endif
        auto init=reinterpret_cast<decltype(&mod_init)>(symbol("mod_init"));
        auto tick=reinterpret_cast<decltype(&mod_tick)>(symbol("mod_tick"));
        auto stop=reinterpret_cast<decltype(&mod_shutdown)>(symbol("mod_shutdown"));
        auto status=reinterpret_cast<decltype(&mod_status)>(symbol("mod_status"));
        auto error=reinterpret_cast<decltype(&mod_last_error)>(symbol("mod_last_error"));
        if(!init||!tick||!stop||!status||!error) {std::cerr<<"mod load/export failed\n";return 3;}
        if(!init(argv[2])) {std::cerr<<error()<<'\n';return 4;}
        [[maybe_unused]] mod::TimerResolution timerResolution;mod::Cadence cadence(10000);
        bool ok=true;auto begin=mod::nowUs(),previous=begin;
        while(double(mod::nowUs()-begin)/1000000<seconds) {
            auto now=mod::nowUs();double dt=double(now-previous)/1000000;previous=now;
            if(!tick(dt)) {std::cerr<<error()<<'\n';ok=false;break;}
            cadence.wait();
        }
        std::vector<char> json(status(nullptr,0));status(json.data(),json.size());std::cout<<json.data()<<'\n';stop();
#ifdef _WIN32
        FreeLibrary(module);
#else
        dlclose(module);
#endif
        return ok?0:5;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
