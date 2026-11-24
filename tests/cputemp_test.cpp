// CPU temperature arithmetic, the shared block checks and the scheduled task definition.
#include <cassert>
#include <cstdio>

#include "../src/cputemp.h"

using namespace cputemp;

int main() {
    // TjMax from bits 23:16 of the temperature target; an unbelievable value falls back to 100.
    assert(TjMax(0x00640000) == 100 && TjMax(0x00550000) == 85 && TjMax(0x006E0000) == 110);
    assert(TjMax(0) == 100 && TjMax(0x00100000) == 100 && TjMax(0x00FF0000) == 100);
    assert(TjMax(0xFFFF00640000FFFFULL) == 100);  // other bits do not matter

    // Package temperature: valid bit 31, degrees below TjMax in bits 22:16.
    const std::uint64_t valid = 1ULL << 31;
    assert(PackageTemperature(valid | (40ULL << 16), 100) == 60);
    assert(PackageTemperature(valid | (0ULL << 16), 100) == 100);
    assert(PackageTemperature(valid | (15ULL << 16), 85) == 70);
    assert(PackageTemperature((40ULL << 16), 100) == -1);                 // reading not valid
    assert(PackageTemperature(valid | (120ULL << 16), 100) == -1);        // below zero: not believable
    assert(PackageTemperature(valid | (0x7FULL << 16), 125) == -1);       // 127 below 125 would be -2
    assert(PackageTemperature(valid | (40ULL << 16) | 0xFFFF, 100) == 60);  // low bits are ignored

    // The block the app reads: wrong magic or version is not ours; an old one is not shown.
    Shared s;
    assert(!Valid(s) && !Fresh(s, 0));
    s.magic = kMagic;
    s.version = kVersion;
    s.tick = 1000;
    assert(Valid(s) && Fresh(s, 1000) && Fresh(s, 10000) && !Fresh(s, 11001));
    assert(!Fresh(s, 999));  // a tick from the future is not fresh
    s.version = kVersion + 1;
    assert(!Valid(s));

    // The task: this user, the highest privileges, at sign-in, the one program, and escaped text.
    std::wstring xml = TaskXml(L"S-1-5-21-1-2-3-1001", L"C:\\Program Files\\Taskbar Vitals\\TaskbarVitalsTemp.exe");
    assert(xml.find(L"<RunLevel>HighestAvailable</RunLevel>") != std::wstring::npos);
    assert(xml.find(L"<LogonTrigger>") != std::wstring::npos);
    assert(xml.find(L"<UserId>S-1-5-21-1-2-3-1001</UserId>") != std::wstring::npos);
    assert(xml.find(L"<Command>C:\\Program Files\\Taskbar Vitals\\TaskbarVitalsTemp.exe</Command>") != std::wstring::npos);
    assert(xml.find(L"<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>") != std::wstring::npos);
    assert(xml.find(L"<MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>") != std::wstring::npos);
    // exactly one Exec action
    size_t first = xml.find(L"<Exec>");
    assert(first != std::wstring::npos && xml.find(L"<Exec>", first + 1) == std::wstring::npos);
    // special characters in a path cannot break out of the element
    std::wstring tricky = TaskXml(L"S-1", L"C:\\A&B\\<x>\"y\".exe");
    assert(tricky.find(L"C:\\A&amp;B\\&lt;x&gt;&quot;y&quot;.exe") != std::wstring::npos);
    assert(tricky.find(L"<x>") == std::wstring::npos);

    // AMD Family 17h+ SMN THM_TCON_CUR_TMP constants, Tctl/Tdie arithmetic, and vendor detection.
    assert(kAmdSmnThmTconCurTmp == 0x00059800u && kAmdSmnCurTemp == 0x00059800u);
    assert(kAmdCurTempRangeSelBit == (1u << 19));
    auto encodeAmd = [](int eighthsC, bool rangeSel = false) -> std::uint32_t {
        return (static_cast<std::uint32_t>(eighthsC) << 21) | (rangeSel ? kAmdCurTempRangeSelBit : 0u);
    };
    assert(AmdTctlMilliC(0) == 0);
    assert(AmdTctlMilliC(encodeAmd(65 * 8)) == 65000);
    assert(AmdTctlMilliC(encodeAmd(65 * 8 + 4)) == 65500);
    assert(AmdTctlMilliC(encodeAmd((49 + 62) * 8, true)) == 62000);
    assert(AmdTctlOffset("AMD Ryzen 7 5800X 8-Core Processor") == 0);
    assert(AmdTctlOffset("AMD Ryzen 7 1700X Eight-Core Processor") == -20);
    assert(AmdTctlOffset("AMD Ryzen 7 2700X Eight-Core Processor") == -10);
    assert(AmdTctlOffset("AMD Ryzen Threadripper 1950X 16-Core Processor") == -27);
    assert(AmdTdieOffsetC("AMD Ryzen 5 1600X Six-Core Processor") == -20);
    assert(AmdTdieOffsetC("AMD Ryzen Threadripper 2990WX 32-Core Processor") == -27);
    assert(AmdTdieOffsetC(nullptr) == 0);
    assert(AmdPackageTemperature(50U * 8U << 21, 0) == 50);
    assert(AmdPackageTemperature(encodeAmd(65 * 8 + 3)) == 65);
    assert(AmdPackageTemperature(encodeAmd(65 * 8 + 4)) == 66);
    assert(AmdPackageTemperature(((100U * 8U) << 21) | (1U << 19), 0) == 51);  // range bit subtracts 49 C
    assert(AmdPackageTemperature((85U * 8U) << 21, -20) == 65);
    assert(AmdPackageTemperature(0xFFFFFFFFU, 0) == -1);  // out of believable range
    assert(DetectCpuVendor("GenuineIntel") == CpuVendor::Intel);
    assert(DetectCpuVendor("AuthenticAMD") == CpuVendor::Amd);
    assert(DetectCpuVendor("Other") == CpuVendor::Unknown);
    assert(DetectCpuVendor(nullptr) == CpuVendor::Unknown);

    std::puts("CPU temperature tests passed (TjMax, Intel/AMD package reading, shared block age, task definition, escaping).");
    return 0;
}
