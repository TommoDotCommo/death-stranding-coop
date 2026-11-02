// Game-free test of the named-object isolation rule. Mutants: OBJECT_NONE (never renames),
// OBJECT_ALL (renames every name), OBJECT_CASE (case-sensitive match), OBJECT_PREFIX (a
// Global\ or Local\ prefix hides the match). Each must fail at least one check.
#include "objects.h"
#include <cstdio>

static int failures=0;
static void check(bool ok,const char* what) { if(!ok) { ++failures; std::printf("failed: %s\n",what); } }

int main() {
    using namespace redirect;
    ObjectPolicy p{L"s1",{L"DeathStrandingSingleInstance"}};
    check(object_name(p,L"DeathStrandingSingleInstance")==L"DeathStrandingSingleInstance-coop-s1","listed name is isolated");
    check(object_name(p,L"deathstrandingsingleinstance")==L"deathstrandingsingleinstance-coop-s1","match ignores case");
    check(object_name(p,L"Local\\DeathStrandingSingleInstance")==L"Local\\DeathStrandingSingleInstance-coop-s1","Local prefix kept");
    check(object_name(p,L"Global\\DeathStrandingSingleInstance")==L"Global\\DeathStrandingSingleInstance-coop-s1","Global prefix kept");
    check(object_name(p,L"SteamMutex")==L"SteamMutex","unlisted names pass through");
    check(object_name(p,L"DeathStrandingSingleInstance2")==L"DeathStrandingSingleInstance2","no prefix matching of names");
    check(object_name(p,L"")==L"","unnamed objects stay unnamed");
    ObjectPolicy empty{L"s0",{}};
    check(object_name(empty,L"DeathStrandingSingleInstance")==L"DeathStrandingSingleInstance","observe mode renames nothing");
    ObjectPolicy other{L"s0",{L"DeathStrandingSingleInstance"}};
    check(object_name(other,L"DeathStrandingSingleInstance")!=object_name(p,L"DeathStrandingSingleInstance"),"two slots get different names");

    check(valid_object_tag(L"s0") && valid_object_tag(L"slot_15-b"),"plain tags accepted");
    check(!valid_object_tag(L"") && !valid_object_tag(L"s 0") && !valid_object_tag(L"..\\x") && !valid_object_tag(std::wstring(33,L'a')),"bad tags refused");
    std::vector<std::wstring> names;
    check(parse_isolate_list(L"A|B||C",names) && names.size()==3 && names[2]==L"C","list parsed, empty items dropped");
    check(parse_isolate_list(L"",names) && names.empty(),"empty list is observe mode");
    check(!parse_isolate_list(L"A|bad\\name",names),"backslash refused");
    check(!parse_isolate_list(std::wstring(L"x\x01y"),names),"control character refused");
    check(!parse_isolate_list(std::wstring(201,L'a'),names),"oversized name refused");
    std::printf("failures=%d\n",failures);
    return failures?1:0;
}
