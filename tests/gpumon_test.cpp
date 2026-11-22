// GPU counter instance names and the per-adapter use (the busiest engine type, engines of one type added up).
#include <cassert>
#include <cstdio>

#include "../src/gpumon.h"

using gpumon::AdapterUse;
using gpumon::EngineSample;
using gpumon::ParseInstance;

int main() {
    std::uint64_t luid = 0;
    std::wstring type;
    assert(ParseInstance(L"pid_1234_luid_0x00000000_0x0000C3A1_phys_0_eng_3_engtype_3D", luid, &type));
    assert(luid == 0xC3A1 && type == L"3D");
    assert(ParseInstance(L"pid_8_luid_0x00000001_0x00012345_phys_0_eng_12_engtype_VideoDecode", luid, &type));
    assert(luid == ((1ULL << 32) | 0x12345) && type == L"VideoDecode");
    // "GPU Adapter Memory" instances carry a LUID but no engine.
    assert(ParseInstance(L"luid_0x00000000_0x0000C3A1_phys_0", luid, &type) && luid == 0xC3A1 && type.empty());
    assert(ParseInstance(L"luid_0x00000000_0x0000C3A1_phys_0", luid));
    assert(!ParseInstance(L"_Total", luid, &type));
    assert(!ParseInstance(L"pid_1_luid_0xZZ", luid, &type));

    const std::uint64_t gpu = 0xC3A1, other = 0x9999;
    std::vector<EngineSample> s = {
        { gpu, L"3D", 20 }, { gpu, L"3D", 15 },  // two processes on the 3D engine: 35
        { gpu, L"Copy", 50 },                   // the copy engine alone is busier
        { gpu, L"VideoDecode", 5 },
        { other, L"3D", 90 },                   // another adapter does not count
    };
    assert(AdapterUse(s, gpu) == 50);
    s.push_back({ gpu, L"3D", 30 });  // 3D now 65
    assert(AdapterUse(s, gpu) == 65);
    assert(AdapterUse(s, other) == 90);
    assert(AdapterUse(s, 0x1) == -1);                       // no samples for that adapter
    const std::vector<EngineSample> full = { { gpu, L"3D", 70 }, { gpu, L"3D", 60 } };
    assert(AdapterUse(full, gpu) == 100);  // capped
    const std::vector<EngineSample> fraction = { { gpu, L"3D", 12.4 } };
    assert(AdapterUse(fraction, gpu) == 12);
    std::puts("GPU monitor tests passed (instance names, LUIDs, busiest engine type, other adapters, cap).");
    return 0;
}
