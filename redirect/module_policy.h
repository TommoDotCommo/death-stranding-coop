#pragma once
#include "redirect.h"
#include <functional>
#include <string>
#include <vector>
namespace redirect {
enum class ModuleMode { strict, allowlist, log_only };
struct ModulePolicy {
    ModuleMode mode=ModuleMode::strict;
    std::wstring instance, windows, steam, program_data;
    // The executable's own directory (data and game files can live in different trees: a release ticket, or the
    // launcher's direct path with a separate data folder). The proxy fills it on every path; empty admits nothing.
    std::wstring release_game=L"";
    // Exact module paths the player has added for this copy (`extra-modules.txt` in the copy's data folder): software
    // Windows loads into every process, such as a third-party input method. Canonical; matched whole, never as a folder.
    std::vector<std::wstring> extra={};
};
// The player's list: one full path of a .dll per line; blank lines and lines starting with # are skipped. False (the
// proxy then refuses to start) for anything else: a relative path, a folder, a wildcard, a path that is not plain
// ASCII, more than MaxExtraModules entries.
constexpr size_t MaxExtraModules=32;
bool parse_module_extra(const std::string& text,std::vector<std::wstring>& out);
// Evidence means an absolute long path opened without reparse ancestors, whose
// final DOS path equals its lexical path. Hard links are permitted for DLL reads.
bool module_allowed(const ModulePolicy&, const std::wstring&, bool evidence);
// `api-ms-win-*` / `ext-ms-win-*` are virtual API-set names the loader maps to a host
// DLL; they never name a file. The host module is checked after the load.
bool is_api_set_name(const std::wstring& requested);
// The path the policy checks for a LoadLibrary request. `\\?\X:\...` names the same local file as `X:\...` (the NVIDIA
// driver installed in 2026-11 asks for nvwgf2um.dll this way), so it is checked in the plain form. Everything else,
// including `\\.\X:\`, UNC, GLOBALROOT and volume names, is returned unchanged and fails the policy as before.
std::wstring module_request_path(const std::wstring& requested);
// What to do with a LoadLibrary request BEFORE the policy runs, outside log-only mode.
enum class RequestAction { policy, api_set_forward, not_found };
RequestAction classify_request(const ModulePolicy&, const std::wstring& requested, bool already_loaded, bool file_exists);
// Loader flags that map an image without running or resolving it. Data-file and image-resource
// mappings execute nothing; in allowlist mode they pass the same path rules as any module.
// DONT_RESOLVE_DLL_REFERENCES (0x1) hands out an unresolved image and stays refused outside log-only.
bool loader_flags_allowed(const ModulePolicy&, unsigned long flags);
enum class ModulePhase { request, mapped, loaded };
// What a refused module means for the process. A refused request is answered like a load that failed: the module
// never enters the process and the game goes on without it (a third-party input method's DLL, loaded by Windows into
// every process with a window, used to end the game). A module that is found loaded after the fact has already run
// code here, so that still ends the process.
enum class Refusal { fail_load, end_process };
Refusal refusal(ModulePhase phase);
// Shared orchestration seam: audit BEFORE refusal; patch AFTER successful load.
bool module_event(const ModulePolicy&, const std::wstring&, bool evidence,
                  bool existing, ModulePhase,
                  const std::function<void(bool)>& audit,
                  const std::function<void()>& patch);
}
