#pragma once
#include "redirect.h"
#include <functional>
namespace redirect {
enum class ModuleMode { strict, allowlist, log_only };
struct ModulePolicy {
    ModuleMode mode=ModuleMode::strict;
    std::wstring instance, windows, steam, program_data;
};
// Evidence means an absolute long path opened without reparse ancestors, whose
// final DOS path equals its lexical path. Hard links are permitted for DLL reads.
bool module_allowed(const ModulePolicy&, const std::wstring&, bool evidence);
// `api-ms-win-*` / `ext-ms-win-*` are virtual API-set names the loader maps to a host
// DLL; they never name a file. The host module is checked after the load.
bool is_api_set_name(const std::wstring& requested);
// What to do with a LoadLibrary request BEFORE the policy runs, outside log-only mode.
enum class RequestAction { policy, api_set_forward, not_found };
RequestAction classify_request(const ModulePolicy&, const std::wstring& requested, bool already_loaded, bool file_exists);
// Loader flags that map an image without running or resolving it. Data-file and image-resource
// mappings execute nothing; in allowlist mode they pass the same path rules as any module.
// DONT_RESOLVE_DLL_REFERENCES (0x1) hands out an unresolved image and stays refused outside log-only.
bool loader_flags_allowed(const ModulePolicy&, unsigned long flags);
enum class ModulePhase { request, mapped, loaded };
// Shared orchestration seam: audit BEFORE refusal; patch AFTER successful load.
bool module_event(const ModulePolicy&, const std::wstring&, bool evidence,
                  bool existing, ModulePhase,
                  const std::function<void(bool)>& audit,
                  const std::function<void()>& patch);
}
