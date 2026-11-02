#include "hook_list.h"
#include <iostream>
#include <set>
#include <string>
int main() {
    std::set<std::string> query, writes;
#define NAME(n) query.insert(#n);
    REDIRECT_QUERY_HOOKS(NAME)
#undef NAME
#define NAME(n) writes.insert(#n);
    REDIRECT_MAPPED_WRITE_HOOKS(NAME)
#undef NAME
    int bad=0;
    for(auto n:{"GetFileAttributesW","GetFileAttributesA","GetFileAttributesExW","GetFileAttributesExA",
                "FindFirstFileW","FindFirstFileA","FindFirstFileExW","FindFirstFileExA","GetDiskFreeSpaceExW","GetDiskFreeSpaceExA"})
        if(!query.count(n)) {++bad;std::cerr<<"query API not mapped: "<<n<<"\n";}
    for(auto n:{"CreateFileW","CreateFileA","CreateFile2","CreateDirectoryW","CreateDirectoryA","CreateDirectoryExW","CreateDirectoryExA",
                "DeleteFileW","RemoveDirectoryW","MoveFileExW","ReplaceFileW","CopyFile2","SetFileAttributesW"})
        if(!writes.count(n)) {++bad;std::cerr<<"mutating API not mapped: "<<n<<"\n";}
    for(auto& n:query) if(writes.count(n)) {++bad;std::cerr<<"listed twice: "<<n<<"\n";}
    std::cout<<"hooks: "<<query.size()<<" query, "<<writes.size()<<" mapped mutating; failures="<<bad<<"\n";
    return bad?1:0;
}
