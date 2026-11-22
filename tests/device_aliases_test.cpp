// Portable tests for custom display names and lossless REG_MULTI_SZ encoding.
#include "../src/device_aliases.h"

#include <cassert>
#include <cstdio>

int main() {
    using namespace aliases;
    const std::wstring mouse = L"logi:046d:c547";
    const std::wstring headset = L"bt:device:a";
    AliasMap names;
    assert(DisplayName(names, mouse, L"Original mouse") == L"Original mouse");
    names[mouse] = L"Moja mysz";
    names[L"bt:unicode"] = L"Słuchawki żółte";
    names[headset] = L"Headset #2";
    assert(DisplayName(names, mouse, L"Original mouse") == L"Moja mysz");
    assert(DisplayName(names, headset, L"Original headset") == L"Headset #2");
    assert(DisplayName(names, L"unknown", L"Unchanged") == L"Unchanged");

    assert(Trim(L"  Mouse  ") == L"Mouse");
    assert(Trim(L"\t Mysz \n") == L"Mysz");
    assert(!ValidName(L""));
    assert(!ValidName(L"a\nline"));
    assert(ValidName(std::wstring(kMaxNameLength, L'a')));
    assert(!ValidName(std::wstring(kMaxNameLength + 1, L'a')));

    auto encoded = Encode(names);
    assert(encoded[encoded.size() - 1] == 0 && encoded[encoded.size() - 2] == 0);
    assert(Decode(encoded.data(), encoded.size()) == names);
    auto empty = Encode(AliasMap{});
    assert(empty.size() == 2 && empty[0] == 0 && empty[1] == 0);
    assert(Decode(empty.data(), empty.size()).empty());
    assert(Decode(nullptr, 0).empty());
    assert(Decode(encoded.data(), 1).empty());
    auto broken = encoded;
    broken.pop_back();
    assert(Decode(broken.data(), broken.size()).empty());

    // The original device key is stored in full: no collisions from name, VID or hash.
    names[L"device:identical label:1"] = L"Pad";
    names[L"device:identical label:2"] = L"Pad";
    auto withDuplicates = Encode(names);
    assert(Decode(withDuplicates.data(), withDuplicates.size()).size() == 5);

    std::puts("Custom Device Names tests passed");
    return 0;
}
