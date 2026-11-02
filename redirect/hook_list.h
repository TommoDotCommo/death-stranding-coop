#pragma once
// The kernel32 entry points whose path argument is mapped through redirect::map_query: reads, existence
// checks, enumeration and free-space queries under a redirected root answer from the instance copy.
// redirect.cpp installs exactly this list (one source of truth) and hook_list_test.cpp pins it, so a
// removed entry fails a game-free test. PathFileExists and the shell helpers call these through their own
// import tables, which the proxy patches too; a direct ntdll import (NtQueryAttributesFile, NtCreateFile)
// is NOT covered and is listed as a known gap in redirect/README.md.
#ifdef MUTANT_NO_FIND
#define REDIRECT_FIND_HOOKS(X)
#else
#define REDIRECT_FIND_HOOKS(X) X(FindFirstFileW) X(FindFirstFileA) X(FindFirstFileExW) X(FindFirstFileExA)
#endif
#ifdef MUTANT_NO_ATTRIBUTES
#define REDIRECT_ATTRIBUTE_HOOKS(X)
#else
#define REDIRECT_ATTRIBUTE_HOOKS(X) X(GetFileAttributesW) X(GetFileAttributesA) X(GetFileAttributesExW) X(GetFileAttributesExA) \
    X(GetDiskFreeSpaceExW) X(GetDiskFreeSpaceExA)
#endif
#define REDIRECT_QUERY_HOOKS(X) REDIRECT_ATTRIBUTE_HOOKS(X) REDIRECT_FIND_HOOKS(X)
// Mutating calls that are mapped (not refused) because both or all of their paths go through the map.
#define REDIRECT_MAPPED_WRITE_HOOKS(X) X(CreateFileW) X(CreateFile2) X(CreateFileA) X(CreateDirectoryW) X(CreateDirectoryA) \
    X(CreateDirectoryExW) X(CreateDirectoryExA) X(DeleteFileW) X(DeleteFileA) X(RemoveDirectoryW) X(RemoveDirectoryA) \
    X(MoveFileExW) X(MoveFileExA) X(MoveFileW) X(MoveFileA) X(ReplaceFileW) X(ReplaceFileA) X(CopyFile2) X(CopyFileW) X(CopyFileA) \
    X(SetFileAttributesW) X(SetFileAttributesA)
