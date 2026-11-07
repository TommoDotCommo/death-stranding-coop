#pragma once
#include <string>
#include <vector>
namespace redirect {
enum class Action { deny, allow, redirect, device };
struct Mapping { std::wstring real, target; };
struct Policy {
    std::wstring instance, steam; std::vector<Mapping> roots;
    // Release tickets may place the co-op data tree in LocalAppData. The instance
    // exclusion is checked before any real-root mapping; lab environments keep false.
    bool allowNestedInstance=false;
};
struct Result { Action action; std::wstring path; };
// Win32 namespace prefixes. `\\?\X:\...` and `\\.\X:\...` are ordinary drive paths and are
// classified after the prefix is removed. `\\?\hid#...#{HID interface guid}` is a device
// interface (input hardware), not a file. Everything else behind a prefix (UNC, Volume{},
// GLOBALROOT, PhysicalDrive, other device classes) is refused.
enum class PathKind { plain, drive, device, refused };
struct Split { PathKind kind; std::wstring path; };
Split split_namespace(const std::wstring&);
// Roots for driver cache and driver settings locations, always inside the instance.
std::vector<Mapping> driver_roots(const std::wstring& real_local, const std::wstring& program_data,
                                  const std::wstring& instance);
// FILE_FLAG_OPEN_REPARSE_POINT opens are refused unless they only read or only touch the instance.
bool reparse_open_allowed(bool write, Action);
// Pure lexical classifier. Caller supplies reparse/alias evidence from the OS.
// No relative, UNC, device, ADS, dot, ambiguous DOS names or non-ASCII paths.
std::wstring canonical(const std::wstring&);
bool within(const std::wstring&, const std::wstring&);
Result map(const Policy&, const std::wstring&, bool write, bool unsafe=false);
// Queries (attributes, existence, enumeration, free space) are reads that must obey the same roots as
// opens: under a redirected real root the INSTANCE copy answers, so a missing copy is "not found" and the
// real folder is never read. `wildcard` allows a DOS wildcard in the last component (FindFirstFile patterns).
// Results: allow (use the caller's path unchanged: outside every root, device and other namespaces),
// redirect (path = instance path, lower-case canonical, without any \\?\ prefix), deny. A deny whose path is
// L"alias" means the path has a short (8.3, `~`) component: the caller may expand it with GetLongPathName and
// ask again; any other path that normalises into a real root but is not clean is denied, never passed through.
Result map_query(const Policy&, const std::wstring&, bool wildcard=false);
// Loader flags in the audit log: the value in hex WITHOUT a prefix (callers add "0x").
std::wstring hex_flags(unsigned long);
}
#ifdef _WIN32
void install_redirect(void* self,bool releaseSession=false);
// Writes pending DENY/DEVICE counts. Safe to call at any time, never blocks.
void flush_redirect_log();
#endif
#ifdef _WIN32
// Only after loader return, on first compiler API call.
int start_instance_mod();
void* original_compiler_export(const char* name);
// Export of the real System32 DLL (loaded by full path) that the proxy is named after.
void* original_system_export(const wchar_t* dll,const char* name);
#endif
