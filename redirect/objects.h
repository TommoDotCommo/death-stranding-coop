#pragma once
// Named kernel objects (mutex, event, semaphore, file mapping) seen by an instance.
// The game guards against a second running copy with a named object (live 2026-11-01: a
// second instance exited before loading Steam while the first ran). Isolation gives each
// instance its own copy of exactly the names the owner listed for that slot; every other
// name, and every unnamed object, is passed through unchanged.
#include <string>
#include <vector>

namespace redirect {
struct ObjectPolicy {
    std::wstring tag;                  // the slot folder name, e.g. s0
    std::vector<std::wstring> isolate; // base names to isolate, matched case-insensitively
};
// A tag is 1-32 characters of [A-Za-z0-9_-].
bool valid_object_tag(const std::wstring&);
// Splits "a|b|c" into names; empty items are dropped. Returns false for an item longer than
// 200 characters or holding a control character or a backslash.
bool parse_isolate_list(const std::wstring& text, std::vector<std::wstring>& out);
// The name the real API receives. An optional Global\ or Local\ namespace prefix is kept;
// the base name is matched; a match becomes <prefix><base>-coop-<tag>.
std::wstring object_name(const ObjectPolicy&, const std::wstring& name);
}
